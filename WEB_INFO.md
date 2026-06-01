# Web 连接信息

## 服务器

| 项目 | 值 |
|------|-----|
| IP | `60.205.235.150` |
| 区域 | 阿里云 北京 (`cn-beijing`) |
| 实例 ID | `i-2ze1p1u7o6hdzy3d3q76` |
| 系统 | CentOS 8 (Python 3.6.8) |
| SSH | `ssh -i ~/.ssh/id_ed25519_claude root@60.205.235.150` |
| SSH 密钥 | `~/.ssh/id_ed25519_claude` |

## 服务端口

| 端口 | 协议 | 用途 | systemd 服务 |
|------|------|------|-------------|
| 8080 | HTTP | 手机 UI (`/var/www/bed/`) | `bed_web` |
| 9001 | WebSocket | MQTT ↔ 浏览器桥接 | `mqtt_ws_bridge` |
| 9002 | HTTP | 后台管理 (`/var/www/admin/`) | `admin_web` |
| 1883 | MQTT | Mosquitto MQTT Broker | `mosquitto` |

MQTT 认证: 用户名 `childbe` / 密码 `Yinta`

## Web 页面地址

| 页面 | URL |
|------|-----|
| 手机 UI | `http://60.205.235.150:8080/` |
| 后台管理 | `http://60.205.235.150:9002/` |

## 服务器文件

| 路径 | 说明 |
|------|------|
| `/var/www/bed/` | 手机 UI 静态文件 (index.html, css/, js/) |
| `/var/www/admin/` | 后台管理静态文件 (index.html) |
| `/opt/mqtt_ws_bridge.py` | WebSocket ↔ MQTT 桥接脚本 (PID 2484441) |
| `/opt/threaded_http_server.py` | 多线程 HTTP 服务器脚本 (被 bed_web/admin_web 使用) |

## 本地项目文件

| 路径 | 说明 |
|------|------|
| `web/admin.html` | 后台管理页面源码 |

## 通信协议

**桥接器 (9001) JSON 协议：**

浏览器 → 桥 → MQTT:
```json
{"topic":"bed/control","payload":"{\"cmd\":\"sleep\",\"value\":1}"}
```

MQTT → 桥 → 浏览器:
```json
{"topic":"bed/status","payload":"{\"env_temp\":25.0,...}"}
{"topic":"bed/heartbeat","payload":"\"online\""}
```

**后台场景指令：**

| 指令 | cmd | value | 说明 |
|------|-----|-------|------|
| 睡着 | `sleep` | 1 | |
| 醒来 | `sleep` | 0 | |
| 哭闹 | `cry` | 1 | |
| 喂奶提醒 | `feed` | 1 | |
| 播放故事 | `story` | 1 | |
| LED 开关 | `led_onoff` | 0/1 | |
| LED 模式 | `led_mode` | "rainbow" 等 (字符串) | |
| LED 亮度 | `led_brightness` | 0-255 | |
| 音量 | `volume` | 0-100 | |

## 注意事项

- Python HTTP 服务器使用自定义多线程脚本 `/opt/threaded_http_server.py`，因为 Python 3.6 没有内置 `ThreadingHTTPServer`
- 两个 HTTP 服务都是 systemd 托管，开机自启
- 阿里云安全组 `sg-2zeehx92ib8s95n6k8ul` 已放行 TCP 1-65535
