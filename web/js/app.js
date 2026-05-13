/* ─── 服务器配置 ──────────────────────────────────── */
const WS_URL  = 'ws://60.205.235.150:9001';
const MQTT_USER = 'childbe';
const MQTT_PASS = 'Yinta';

/* ─── MQTT 话题 ───────────────────────────────────────── */
const TOPIC_STATUS    = 'bed/status';
const TOPIC_HEARTBEAT = 'bed/heartbeat';
const TOPIC_CONTROL   = 'bed/control';

/* ─── DOM 引用 ────────────────────────────────────────── */
const $dot       = document.getElementById('online-dot');
const $onlineTxt = document.getElementById('online-text');
const $wifiSsid  = document.getElementById('wifi-ssid');
const $logList   = document.getElementById('log-list');

/* ─── 数据元素映射 ────────────────────────────────────── */
const valIds = ['presence', 'env-temp', 'env-hum', 'aqi', 'tvoc', 'eco2', 'breath', 'heart'];

/* ─── MQTT 连接（通过 WebSocket 桥接）────────────────── */
let ws = null;
let heartbeatTimer = null;

function connect() {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
        setOnline(true);
        addLog('system', 'WebSocket 已连接');
    };

    ws.onclose = () => {
        setOnline(false);
        addLog('system', 'WebSocket 断开，3 秒后重连...');
        setTimeout(connect, 3000);
    };

    ws.onerror = () => {
        addLog('system', 'WebSocket 连接错误');
    };

    ws.onmessage = (evt) => {
        try {
            const data = JSON.parse(evt.data);
            handleMessage(data.topic, data.payload);
        } catch (e) {
            // 非 JSON 消息，忽略
        }
    };
}

/* ─── 消息处理 ────────────────────────────────────────── */
function handleMessage(topic, payload) {
    addLog(topic, payload);

    if (topic === TOPIC_STATUS) {
        try {
            const d = JSON.parse(payload);
            updateDisplay(d);
        } catch (e) {}
    } else if (topic === TOPIC_HEARTBEAT) {
        resetHeartbeat();
    }
}

function aqiText(aqi) {
    const map = {1:'优', 2:'良', 3:'中等', 4:'差', 5:'劣'};
    return map[aqi] || '--';
}

function updateDisplay(d) {
    setVal('presence', d.presence ? '有人' : '无人', '');
    setVal('env-temp', d.env_temp,  '°C');
    setVal('env-hum',  d.env_hum,   '%');
    setVal('aqi',      aqiText(d.aqi), '');
    setVal('tvoc',     d.tvoc,      'ppb');
    setVal('eco2',     d.eco2,      'ppm');
    setVal('breath',   d.breath,    '次/分');
    setVal('heart',    d.heart,     'bpm');
}

function setVal(id, val, unit) {
    const el = document.getElementById('val-' + id);
    if (!el) return;
    if (val === undefined || val === null) {
        el.textContent = '-- ' + unit;
    } else {
        el.textContent = val + ' ' + unit;
    }
}

/* ─── 发送控制指令 ────────────────────────────────────── */
function sendCommand(cmd, value) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        addLog('system', '未连接，无法发送指令');
        return;
    }
    const payload = JSON.stringify({ cmd: cmd, value: value });
    ws.send(JSON.stringify({ topic: TOPIC_CONTROL, payload: payload }));
    addLog(TOPIC_CONTROL, payload);
}

/* ─── 在线状态管理 ────────────────────────────────────── */
function setOnline(on) {
    $dot.className = 'dot ' + (on ? 'online' : 'offline');
    $onlineTxt.textContent = on ? '已连接' : '未连接';
}

function resetHeartbeat() {
    setOnline(true);
    if (heartbeatTimer) clearTimeout(heartbeatTimer);
    heartbeatTimer = setTimeout(() => setOnline(false), 15000);
}

/* ─── 日志 ────────────────────────────────────────────── */
function addLog(topic, msg) {
    const div = document.createElement('div');
    const now = new Date();
    const ts = now.toTimeString().slice(0, 8);
    div.innerHTML = '<span class="time">' + ts + '</span>' +
                    '<span class="topic">[' + topic + ']</span>' + msg;
    $logList.prepend(div);
    if ($logList.children.length > 50) $logList.lastChild.remove();
}

/* ─── 按钮绑定 ────────────────────────────────────────── */
document.getElementById('btn-led-on').onclick  = () => sendCommand('led_onoff', 1);
document.getElementById('btn-led-off').onclick = () => sendCommand('led_onoff', 0);

document.querySelectorAll('.ctrl-btn.mode').forEach(btn => {
    btn.onclick = () => sendCommand('led_mode', btn.dataset.mode);
});

const slider = document.getElementById('slider-brightness');
slider.oninput = () => {
    document.getElementById('val-brightness').textContent = slider.value;
};
slider.onchange = () => sendCommand('led_brightness', parseInt(slider.value));

/* ─── 启动 ────────────────────────────────────────────── */
connect();
