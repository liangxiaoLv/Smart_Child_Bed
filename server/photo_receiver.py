"""
App 照片上传接收器
==================
- 连接本地 Mosquitto MQTT Broker
- 订阅 app/photo/meta      (JSON, 批次清单)
- 订阅 app/photo/chunk/+/+ (Binary, 分片数据)
- 订阅 app/photo/complete  (JSON, 单张传输完成)
- 重组后保存到 /var/www/bed/picture/
- 发布 app/photo/result    (JSON, 处理结果反馈)
- 一批图片全部收齐后，调用 pic2wav API 生成 WAV 存到 /var/www/bed/story/

MQTT 认证: childbe / Yinta

启动: python3 /opt/photo_receiver.py
"""

import base64
import json
import os
import logging
import sys
import threading

import requests
import paho.mqtt.client as mqtt

# ─── 配置 ───────────────────────────────────────────────────
BROKER_HOST   = "127.0.0.1"
BROKER_PORT   = 1883
BROKER_USER   = "childbe"
BROKER_PASS   = "Yinta"
TOPIC_META     = "app/photo/meta"
TOPIC_CHUNK    = "app/photo/chunk/+/+"
TOPIC_COMPLETE = "app/photo/complete"
TOPIC_RESULT   = "app/photo/result"
OUTPUT_DIR     = "/var/www/bed/picture"
STORY_DIR      = "/var/www/bed/story"
PIC2WAV_API    = "http://192.168.21.217:9801/generate"

log = logging.getLogger("photo_rx")
log.setLevel(logging.INFO)
handler = logging.StreamHandler(sys.stdout)
handler.setFormatter(logging.Formatter('%(asctime)s [%(levelname)s] %(message)s'))
log.addHandler(handler)

os.makedirs(OUTPUT_DIR, exist_ok=True)
os.makedirs(STORY_DIR,  exist_ok=True)

# ─── 状态 ───────────────────────────────────────────────────
# photo_id -> {batch_id, filename, size, total_chunks, chunks: {index: bytes}}
photos = {}

# batch_id -> {photo_count, photo_order: [photo_id,...], completed: {photo_id: save_path}}
batches = {}


def publish_result(client, batch_id, photo_id, status, message, missing_chunks=None):
    payload = {
        "batch_id": batch_id,
        "photo_id": photo_id,
        "status": status,
        "message": message,
    }
    if missing_chunks:
        payload["missing_chunks"] = missing_chunks
    client.publish(TOPIC_RESULT, json.dumps(payload, ensure_ascii=False), qos=1)
    log.info("发布结果: photo_id=%s status=%s", photo_id, status)


def on_connect(client, userdata, flags, rc):
    log.info("MQTT 已连接 (rc=%d)", rc)
    client.subscribe(TOPIC_META,     qos=1)
    client.subscribe(TOPIC_CHUNK,    qos=1)
    client.subscribe(TOPIC_COMPLETE, qos=1)
    log.info("订阅: %s, %s, %s", TOPIC_META, TOPIC_CHUNK, TOPIC_COMPLETE)


def on_message(client, userdata, msg):
    try:
        if msg.topic == TOPIC_META:
            _handle_meta(client, msg)
        elif msg.topic == TOPIC_COMPLETE:
            _handle_complete(client, msg)
        elif msg.topic.startswith("app/photo/chunk/"):
            _handle_chunk(client, msg)
    except Exception as e:
        log.error("处理消息异常 topic=%s: %s", msg.topic, e)


def _handle_meta(client, msg):
    data = json.loads(msg.payload.decode("utf-8"))
    batch_id = data.get("batch_id", "")
    photo_list = data.get("photos", [])
    log.info("收到 meta: batch_id=%s photo_count=%d", batch_id, len(photo_list))

    # 登记批次
    batches[batch_id] = {
        "photo_count": len(photo_list),
        "photo_order": [p["photo_id"] for p in photo_list],  # 保留原始顺序
        "completed":   {},
    }

    for p in photo_list:
        photo_id = p["photo_id"]
        photos[photo_id] = {
            "batch_id":     batch_id,
            "filename":     p.get("filename", f"{photo_id}.webp"),
            "size":         p.get("size", 0),
            "total_chunks": p.get("total_chunks", 0),
            "chunks":       {},
        }
        log.info("  登记图片: photo_id=%s filename=%s total_chunks=%d",
                 photo_id, p.get("filename"), p.get("total_chunks"))


def _handle_chunk(client, msg):
    # topic 格式: app/photo/chunk/{photo_id}/{chunk_index}
    parts = msg.topic.split("/")
    if len(parts) != 5:
        log.warning("chunk topic 格式不对: %s", msg.topic)
        return

    photo_id    = parts[3]
    chunk_index = int(parts[4])

    if photo_id not in photos:
        log.warning("收到未登记 photo_id 的 chunk: %s, 丢弃", photo_id)
        return

    photos[photo_id]["chunks"][chunk_index] = msg.payload
    received = len(photos[photo_id]["chunks"])
    total    = photos[photo_id]["total_chunks"]
    log.info("chunk: photo_id=%s index=%d (%d/%d)", photo_id, chunk_index, received, total)


def _handle_complete(client, msg):
    data     = json.loads(msg.payload.decode("utf-8"))
    batch_id = data.get("batch_id", "")
    photo_id = data.get("photo_id", "")

    if photo_id not in photos:
        log.warning("complete 收到未登记 photo_id: %s", photo_id)
        return

    meta         = photos[photo_id]
    total_chunks = meta["total_chunks"]
    chunks       = meta["chunks"]

    # 检查分片是否齐全
    missing = [i for i in range(total_chunks) if i not in chunks]
    if missing:
        log.warning("photo_id=%s 缺少分片: %s", photo_id, missing)
        publish_result(client, batch_id, photo_id, "failed", "缺少分片", missing)
        return

    # 按序号拼接写入文件
    filename = meta["filename"]
    # 保留原始文件名，但确保扩展名是 .webp
    base = os.path.splitext(filename)[0]
    save_path = os.path.join(OUTPUT_DIR, base + ".webp")

    with open(save_path, "wb") as f:
        for i in range(total_chunks):
            f.write(chunks[i])

    actual_size = os.path.getsize(save_path)
    log.info("保存成功: %s (%d bytes)", save_path, actual_size)

    # 记录到批次完成表
    batch = batches.get(batch_id)
    if batch:
        batch["completed"][photo_id] = save_path

    # 释放内存（chunks 已写入文件）
    del photos[photo_id]

    publish_result(client, batch_id, photo_id, "success", "上传成功")

    # 检查整批是否全部完成
    if batch and len(batch["completed"]) == batch["photo_count"]:
        log.info("批次 %s 全部完成，启动 pic2wav 转换", batch_id)
        t = threading.Thread(
            target=_convert_batch_to_wav,
            args=(client, batch_id, batch),
            daemon=True,
        )
        t.start()
        del batches[batch_id]


def _convert_batch_to_wav(client, batch_id, batch):
    """按原始顺序读取本批图片，调用 pic2wav API，WAV 存到 STORY_DIR。"""
    # 按 meta 中的原始顺序排列图片路径
    ordered_paths = [batch["completed"][pid] for pid in batch["photo_order"]
                     if pid in batch["completed"]]

    log.info("pic2wav: batch_id=%s 共 %d 张图片", batch_id, len(ordered_paths))

    chapters = [[base64.b64encode(open(p, "rb").read()).decode("utf-8")
                 for p in ordered_paths]]

    payload = {
        "chapters":     chapters,
        "voice_choice": "female",
        "mode":         "general",
    }

    try:
        resp = requests.post(PIC2WAV_API, json=payload, timeout=600)
        resp.raise_for_status()
        data = resp.json()
    except Exception as e:
        log.error("pic2wav API 调用失败 batch_id=%s: %s", batch_id, e)
        return

    for ch in data.get("chapters", []):
        out_path = os.path.join(STORY_DIR, f"{batch_id}_ch{ch['chapter_index']}.wav")
        audio_bytes = base64.b64decode(ch["audio"])
        with open(out_path, "wb") as f:
            f.write(audio_bytes)
        log.info("WAV 已保存: %s (%d KB)  耗时: %.2fs",
                 out_path, len(audio_bytes) // 1024, ch.get("total_seconds", 0))


# ─── 启动 ───────────────────────────────────────────────────
client = mqtt.Client(client_id="photo_receiver")
client.username_pw_set(BROKER_USER, BROKER_PASS)
client.on_connect = on_connect
client.on_message = on_message

log.info("连接 Broker: %s:%d", BROKER_HOST, BROKER_PORT)
client.connect(BROKER_HOST, BROKER_PORT, 60)

try:
    client.loop_forever()
except KeyboardInterrupt:
    log.info("停止")
    client.disconnect()
