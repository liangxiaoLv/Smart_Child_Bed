/* ─── 服务器配置 ──────────────────────────────────── */
const WS_URL  = 'ws://60.205.235.150:9001';

/* ─── MQTT 话题 ──────────────────────────────────── */
const TOPIC_STATUS    = 'bed/status';
const TOPIC_HEARTBEAT = 'bed/heartbeat';
const TOPIC_CONTROL   = 'bed/control';

/* ─── DOM 引用 ───────────────────────────────────── */
const $dot        = document.getElementById('online-dot');
const $wifiLabel  = document.getElementById('wifi-label');
const $logList    = document.getElementById('log-list');
const $clock      = document.getElementById('clock');
const $modeBtns   = document.querySelectorAll('.btn-mode');

/* ─── MQTT 连接（通过 WebSocket 桥接）────────────── */
let ws = null;
let heartbeatTimer = null;

function connect() {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
        setOnline(true);
        addLog('system', 'WebSocket 已连接');
        requestAudioList();
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
            handleRawMessage(data);
        } catch (e) {}
    };
}

/* ─── 消息处理 ──────────────────────────────────── */
function handleMessage(topic, payload) {
    addLog(topic, payload);

    if (topic === TOPIC_STATUS) {
        try {
            updateDisplay(JSON.parse(payload));
        } catch (e) {}
    } else if (topic === TOPIC_HEARTBEAT) {
        resetHeartbeat();
    }
}

function handleRawMessage(data) {
    /* 处理带 type 字段的消息（音频列表等） */
    if (data.type === 'audio_list') {
        handleAudioList(data.files);
    } else if (data.topic !== undefined) {
        handleMessage(data.topic, data.payload);
    }
}

function aqiText(aqi) {
    return {1:'优', 2:'良', 3:'中等', 4:'差', 5:'劣'}[aqi] || '--';
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

    /* WiFi SSID 更新 */
    if (d.ssid) {
        $wifiLabel.textContent = d.ssid;
    }
}

function setVal(id, val, unit) {
    var el = document.getElementById('val-' + id);
    if (!el) return;
    if (val === undefined || val === null) {
        el.textContent = '-- ' + unit;
    } else {
        el.textContent = val + ' ' + unit;
    }
}

/* ─── 发送控制指令 ──────────────────────────────── */
function sendCommand(cmd, value) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        addLog('system', '未连接，无法发送指令');
        return;
    }
    var payload = JSON.stringify({ cmd: cmd, value: value });
    ws.send(JSON.stringify({ topic: TOPIC_CONTROL, payload: payload }));
    addLog(TOPIC_CONTROL, payload);
}

/* ─── 音频播放 ──────────────────────────────────── */
function requestAudioList() {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ type: 'list_audio' }));
}

function playAudio(filename) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        addLog('system', '未连接，无法播放音频');
        return;
    }
    ws.send(JSON.stringify({ type: 'play_audio', file: filename }));
    addLog('audio', '请求播放: ' + filename);
}

function handleAudioList(files) {
    var list = document.getElementById('audio-list');
    if (!files || files.length === 0) {
        list.innerHTML = '<div class="audio-empty">/source 目录下没有 WAV 文件</div>';
        return;
    }
    var html = '';
    files.forEach(function(f) {
        var sizeKB = (f.size / 1024).toFixed(1);
        html += '<div class="audio-item">' +
                '<span class="audio-name">' + f.name + '</span>' +
                '<span class="audio-size">' + sizeKB + ' KB</span>' +
                '<button class="btn btn-play" onclick="playAudio(\'' + f.name + '\')">播放</button>' +
                '</div>';
    });
    list.innerHTML = html;
}

/* ─── 在线状态管理 ──────────────────────────────── */
function setOnline(on) {
    $dot.className = 'dot ' + (on ? 'online' : 'offline');
    if (!on) {
        $wifiLabel.textContent = '未连接';
    }
}

function resetHeartbeat() {
    setOnline(true);
    if (heartbeatTimer) clearTimeout(heartbeatTimer);
    heartbeatTimer = setTimeout(function() { setOnline(false); }, 15000);
}

/* ─── 实时时钟 ──────────────────────────────────── */
function updateClock() {
    var now = new Date();
    var h = String(now.getHours()).padStart(2, '0');
    var m = String(now.getMinutes()).padStart(2, '0');
    var s = String(now.getSeconds()).padStart(2, '0');
    $clock.textContent = h + ':' + m + ':' + s;
}

/* ─── 日志 ──────────────────────────────────────── */
function addLog(topic, msg) {
    var div = document.createElement('div');
    var now = new Date();
    var ts = now.toTimeString().slice(0, 8);
    div.innerHTML = '<span class="time">' + ts + '</span>' +
                    '<span class="topic">[' + topic + ']</span>' + msg;
    $logList.prepend(div);
    if ($logList.children.length > 50) $logList.lastChild.remove();
}

/* ─── 按钮绑定 ──────────────────────────────────── */
document.getElementById('btn-led-on').onclick  = function() { sendCommand('led_onoff', 1); };
document.getElementById('btn-led-off').onclick = function() { sendCommand('led_onoff', 0); };

$modeBtns.forEach(function(btn) {
    btn.onclick = function() {
        $modeBtns.forEach(function(b) { b.classList.remove('active'); });
        btn.classList.add('active');
        sendCommand('led_mode', btn.dataset.mode);
    };
});

var brightnessSlider = document.getElementById('slider-brightness');
brightnessSlider.oninput = function() {
    document.getElementById('val-brightness').textContent = brightnessSlider.value + '%';
};
brightnessSlider.onchange = function() { sendCommand('led_brightness', parseInt(brightnessSlider.value)); };

var volumeSlider = document.getElementById('slider-volume');
volumeSlider.oninput = function() {
    document.getElementById('val-volume').textContent = volumeSlider.value + '%';
};
volumeSlider.onchange = function() { sendCommand('volume', parseInt(volumeSlider.value)); };

document.getElementById('btn-clear-log').onclick = function() {
    $logList.innerHTML = '';
};

document.getElementById('btn-refresh-audio').onclick = function() {
    requestAudioList();
    addLog('audio', '刷新音频列表');
};

/* ─── 启动 ──────────────────────────────────────── */
updateClock();
setInterval(updateClock, 1000);
connect();
