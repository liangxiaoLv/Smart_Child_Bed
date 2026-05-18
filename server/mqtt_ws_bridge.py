"""
WebSocket ↔ MQTT 双向桥接 + 音频文件服务
============================================
- Mosquitto MQTT Broker: localhost:1883
- WebSocket 监听: 0.0.0.0:9001
- 音频目录: /source/

协议:
  浏览器 → 桥:
    {"topic":"bed/control","payload":"{...}"}  控制指令 (转发到 MQTT)
    {"type":"list_audio"}                       请求音频文件列表
    {"type":"play_audio","file":"xxx.wav"}      请求播放音频

  桥 → 浏览器:
    {"topic":"bed/...","payload":"..."}         MQTT 消息 (转发到浏览器)
    {"type":"audio_list","files":[...]}          音频文件列表响应

  桥 → MQTT:
    bed/control       控制指令
    bed/audio_start   音频元数据 {"name":"...","size":N}
    bed/audio         音频数据块 (原始二进制, 每个 4096 字节)
"""

import asyncio
import json
import os
import logging
import time

import paho.mqtt.client as mqtt
import websockets

# ─── 配置 ───────────────────────────────────────────────
MQTT_BROKER     = "127.0.0.1"
MQTT_PORT       = 1883
MQTT_USER       = "childbe"
MQTT_PASS       = "Yinta"
MQTT_TOPICS     = ["bed/#"]
WS_HOST         = "0.0.0.0"
WS_PORT         = 9001
AUDIO_DIR       = "/source"
CHUNK_SIZE      = 7168

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("bridge")

# ─── MQTT 客户端 ────────────────────────────────────────
mqtt_cli = mqtt.Client(client_id="ws_bridge")
mqtt_cli.username_pw_set(MQTT_USER, MQTT_PASS)

# 已连接的 WebSocket 集合 + 事件循环引用
ws_clients = set()
event_loop = None

async def _ws_send_all(data):
    """发送数据给所有已连接的 WebSocket 客户端"""
    stale = set()
    for ws in ws_clients.copy():
        try:
            await ws.send(data)
        except websockets.exceptions.ConnectionClosed:
            stale.add(ws)
    ws_clients.difference_update(stale)

def on_mqtt_connect(client, userdata, flags, rc):
    log.info("MQTT 已连接 (rc=%d)", rc)
    for t in MQTT_TOPICS:
        client.subscribe(t, qos=1)
        log.info("订阅: %s", t)

def on_mqtt_message(client, userdata, msg):
    """MQTT → WebSocket: 转发消息给所有浏览器（过滤二进制音频数据）"""
    if msg.topic == "bed/audio":
        return
    text = msg.payload.decode("utf-8", errors="replace")
    data = json.dumps({"topic": msg.topic, "payload": text})
    if event_loop:
        asyncio.run_coroutine_threadsafe(_ws_send_all(data), event_loop)

mqtt_cli.on_connect = on_mqtt_connect
mqtt_cli.on_message = on_mqtt_message

# ─── 音频文件操作 ────────────────────────────────────────
def scan_audio_files():
    """扫描 /source 目录下的 .wav 文件"""
    files = []
    try:
        for f in sorted(os.listdir(AUDIO_DIR)):
            if f.lower().endswith(".wav"):
                path = os.path.join(AUDIO_DIR, f)
                files.append({"name": f, "size": os.path.getsize(path)})
    except FileNotFoundError:
        log.warning("音频目录不存在: %s", AUDIO_DIR)
    return files

def send_audio_via_mqtt(filename):
    """读取 WAV 文件，通过 MQTT 分块发送"""
    path = os.path.join(AUDIO_DIR, filename)
    if not os.path.isfile(path):
        log.error("文件不存在: %s", path)
        return False

    fsize = os.path.getsize(path)
    log.info("发送音频: %s (%d bytes)", filename, fsize)

    # 1. 发送元数据
    meta = json.dumps({"name": filename, "size": fsize})
    mqtt_cli.publish("bed/audio_start", meta, qos=1)
    time.sleep(0.2)  # 等 ESP32 分配好 PSRAM 缓冲区

    # 2. 分块发送原始二进制 (QoS 0, 全速)
    with open(path, "rb") as f:
        seq = 0
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break
            mqtt_cli.publish("bed/audio", chunk, qos=0)
            seq += 1

    log.info("音频发送完成: %s (%d 块)", filename, seq)
    return True

# ─── WebSocket 处理 ──────────────────────────────────────
async def ws_send(ws, data):
    try:
        await ws.send(data)
    except websockets.exceptions.ConnectionClosed:
        ws_clients.discard(ws)

async def ws_handler(ws, path):
    ws_clients.add(ws)
    peer = ws.remote_address
    log.info("WebSocket 连接: %s", peer)

    # 发送当前音频列表
    files = scan_audio_files()
    await ws_send(ws, json.dumps({"type": "audio_list", "files": files}))

    try:
        async for raw in ws:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                log.warning("无效 JSON: %s", raw[:100])
                continue

            msg_type = msg.get("type", "")

            if msg_type == "list_audio":
                # 浏览器请求刷新音频列表
                files = scan_audio_files()
                await ws_send(ws, json.dumps({"type": "audio_list", "files": files}))

            elif msg_type == "play_audio":
                # 浏览器请求播放音频 → 通过 MQTT 发送到 ESP32
                filename = msg.get("file", "")
                if filename:
                    log.info("收到播放请求: %s (来自 %s)", filename, peer)
                    send_audio_via_mqtt(filename)

            elif "topic" in msg and "payload" in msg:
                # 控制指令: 转发到 MQTT
                mqtt_cli.publish(msg["topic"], msg["payload"], qos=1)
                log.info("→ MQTT [%s]: %s", msg["topic"], msg["payload"])

            else:
                log.warning("未知消息类型: %s", raw[:100])

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        ws_clients.discard(ws)
        log.info("WebSocket 断开: %s", peer)

# ─── 主循环 ──────────────────────────────────────────────
def main():
    global event_loop

    # 启动 MQTT
    mqtt_cli.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=30)
    mqtt_cli.loop_start()

    # 启动 WebSocket (Python 3.6 兼容写法)
    event_loop = asyncio.get_event_loop()
    server = websockets.serve(ws_handler, WS_HOST, WS_PORT)
    event_loop.run_until_complete(server)
    log.info("WebSocket 监听: %s:%d", WS_HOST, WS_PORT)

    try:
        event_loop.run_forever()
    except KeyboardInterrupt:
        pass
    finally:
        log.info("正在关闭...")
        mqtt_cli.loop_stop()
        mqtt_cli.disconnect()
        event_loop.close()

if __name__ == "__main__":
    main()
