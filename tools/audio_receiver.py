"""
ESP32 音频上传接收器
====================
- 连接本地 Mosquitto MQTT Broker
- 订阅 bed/audio_upload_start (元数据)
- 订阅 bed/audio_upload_chunk (二进制数据块)
- 拼接保存到 /var/www/bed/audio/

MQTT 认证: childbe / Yinta

启动: python3 /opt/audio_receiver.py
"""

import json
import os
import logging
import sys

import paho.mqtt.client as mqtt

# ─── 配置 ───────────────────────────────────────────────────
BROKER_HOST  = "127.0.0.1"
BROKER_PORT  = 1883
BROKER_USER  = "childbe"
BROKER_PASS  = "Yinta"
TOPIC_META   = "bed/audio_upload_start"
TOPIC_CHUNK  = "bed/audio_upload_chunk"
OUTPUT_DIR   = "/var/www/bed/audio"

log = logging.getLogger("audio_rx")
log.setLevel(logging.INFO)
handler = logging.StreamHandler(sys.stdout)
handler.setFormatter(logging.Formatter('%(asctime)s [%(levelname)s] %(message)s'))
log.addHandler(handler)

# ─── 状态 ───────────────────────────────────────────────────
current_meta  = None   # {"name":"...", "size": N}
received      = 0       # 已接收字节数
fout          = None    # 输出文件句柄

os.makedirs(OUTPUT_DIR, exist_ok=True)


def on_connect(client, userdata, flags, rc):
    log.info("MQTT 已连接 (rc=%d)", rc)
    client.subscribe(TOPIC_META,  qos=1)
    client.subscribe(TOPIC_CHUNK, qos=1)
    log.info("订阅: %s, %s", TOPIC_META, TOPIC_CHUNK)


def on_message(client, userdata, msg):
    global current_meta, received, fout

    try:
        if msg.topic == TOPIC_META:
            text = msg.payload.decode("utf-8", errors="replace")
            data = json.loads(text)
            msg_type = data.get("type")

            if msg_type == "start":
                name   = data.get("id", "unknown")
                size   = data.get("size", 0)
                fname  = f"{name}.wav"
                fpath  = os.path.join(OUTPUT_DIR, fname)

                if fout:
                    fout.close()
                    fout = None

                fout = open(fpath, "wb")
                current_meta = {"name": fname, "size": size}
                received = 0
                log.info("开始接收: %s (%d bytes)", fname, size)

            elif msg_type == "end":
                if fout:
                    fout.close()
                    fout = None
                if current_meta:
                    nam = current_meta["name"]
                    sz  = os.path.getsize(os.path.join(OUTPUT_DIR, nam))
                    log.info("接收完成: %s (%d bytes)", nam, sz)
                current_meta = None
                received = 0

            else:
                log.warning("未知 meta 类型: %s", msg_type)

        elif msg.topic == TOPIC_CHUNK:
            if not fout or not current_meta:
                log.warning("收到 chunk 但无活跃会话, 丢弃")
                return

            fout.write(msg.payload)
            received += len(msg.payload)

            pct = (received * 100) // max(current_meta["size"], 1)
            if pct % 10 == 0 and received > 0:
                # 粗略进度, 不频繁打印
                pass

    except Exception as e:
        log.error("处理消息异常: %s", e)


# ─── 启动 ───────────────────────────────────────────────────
client = mqtt.Client(client_id="audio_receiver")
client.username_pw_set(BROKER_USER, BROKER_PASS)
client.on_connect = on_connect
client.on_message = on_message

log.info("连接 Broker: %s:%d", BROKER_HOST, BROKER_PORT)
client.connect(BROKER_HOST, BROKER_PORT, 60)

try:
    client.loop_forever()
except KeyboardInterrupt:
    log.info("停止")
    if fout:
        fout.close()
    client.disconnect()
