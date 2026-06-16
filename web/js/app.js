/* ─── 用户认证 ──────────────────────────────────── */
const AUTH_URL = 'http://60.205.235.150:9003';

/* 页面初始化：检查登录状态 */
function checkAuth() {
    var token = localStorage.getItem('bed_token');
    if (!token) {
        showAuthOverlay('login');
        return;
    }
    verifyToken(token);
}

function verifyToken(token) {
    fetch(AUTH_URL + '/api/verify_token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: token })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) {
            onLoginSuccess(data.username, token);
        } else {
            localStorage.removeItem('bed_token');
            localStorage.removeItem('bed_username');
            showAuthOverlay('login');
        }
    })
    .catch(function() {
        /* 网络错误时，信任本地 token 继续使用 */
        var username = localStorage.getItem('bed_username');
        if (username) {
            onLoginSuccess(username, token);
        } else {
            showAuthOverlay('login');
        }
    });
}

function showAuthOverlay(mode) {
    var overlay = document.getElementById('auth-overlay');
    overlay.classList.remove('hidden');

    /* 隐藏所有表单 */
    document.getElementById('auth-login').classList.remove('active');
    document.getElementById('auth-register').classList.remove('active');
    document.getElementById('auth-chpwd').classList.remove('active');

    if (mode === 'login') {
        document.getElementById('auth-login').classList.add('active');
    } else if (mode === 'register') {
        document.getElementById('auth-register').classList.add('active');
    }

    /* 清空错误提示和输入 */
    clearAuthErrors();
}

function showAuthPanel(mode) {
    showAuthOverlay(mode);
}

function hideAuthPanel() {
    document.getElementById('auth-overlay').classList.add('hidden');
}

function onLoginSuccess(username, token) {
    localStorage.setItem('bed_token', token);
    localStorage.setItem('bed_username', username);
    hideAuthPanel();
    updateUserUI(username);
}

function updateUserUI(username) {
    var btn = document.getElementById('btn-user');
    var label = document.getElementById('user-label');
    var dropdownName = document.getElementById('dropdown-username');

    if (username) {
        label.textContent = username;
        dropdownName.textContent = username;
        btn.classList.add('btn-logged-in');
    } else {
        label.textContent = '未登录';
        btn.classList.remove('btn-logged-in');
    }
}

/* ─── 登录 ──────────────────────────────────────── */
function login() {
    var username = document.getElementById('login-username').value.trim();
    var password = document.getElementById('login-password').value;
    var errEl = document.getElementById('login-error');

    if (!username || !password) {
        errEl.textContent = '请输入用户名和密码';
        errEl.className = 'auth-error';
        return;
    }

    errEl.textContent = '登录中...';
    errEl.className = 'auth-error';

    fetch(AUTH_URL + '/api/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, password: password })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) {
            onLoginSuccess(data.username, data.token);
        } else {
            errEl.textContent = data.message;
            errEl.className = 'auth-error';
        }
    })
    .catch(function() {
        errEl.textContent = '网络错误，请检查网络后重试';
        errEl.className = 'auth-error';
    });
}

/* 登录表单回车键提交 */
document.addEventListener('DOMContentLoaded', function() {
    var loginPwd = document.getElementById('login-password');
    if (loginPwd) {
        loginPwd.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { login(); }
        });
    }
    var loginName = document.getElementById('login-username');
    if (loginName) {
        loginName.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { login(); }
        });
    }
});

/* ─── 注册 ──────────────────────────────────────── */
function register() {
    var username = document.getElementById('reg-username').value.trim();
    var email    = document.getElementById('reg-email').value.trim();
    var password = document.getElementById('reg-password').value;
    var password2 = document.getElementById('reg-password2').value;
    var errEl = document.getElementById('reg-error');

    /* 客户端校验 */
    if (!username) {
        errEl.textContent = '请输入用户名';
        errEl.className = 'auth-error'; return;
    }
    if (username.length < 3 || username.length > 20) {
        errEl.textContent = '用户名需 3-20 位';
        errEl.className = 'auth-error'; return;
    }
    if (password.length < 6) {
        errEl.textContent = '密码至少 6 位';
        errEl.className = 'auth-error'; return;
    }
    if (password !== password2) {
        errEl.textContent = '两次密码输入不一致';
        errEl.className = 'auth-error'; return;
    }

    errEl.textContent = '注册中...';
    errEl.className = 'auth-error';

    fetch(AUTH_URL + '/api/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, password: password, email: email })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) {
            errEl.textContent = data.message + '，请登录';
            errEl.className = 'auth-error success';
            /* 1.5 秒后自动跳转登录 */
            setTimeout(function() { showAuthPanel('login'); }, 1500);
        } else {
            errEl.textContent = data.message;
            errEl.className = 'auth-error';
        }
    })
    .catch(function() {
        errEl.textContent = '网络错误，请检查网络后重试';
        errEl.className = 'auth-error';
    });
}

/* ─── 修改密码 ──────────────────────────────────── */
function showChangePassword() {
    document.getElementById('auth-overlay').classList.remove('hidden');
    document.getElementById('auth-login').classList.remove('active');
    document.getElementById('auth-register').classList.remove('active');
    document.getElementById('auth-chpwd').classList.add('active');
    clearAuthErrors();
    /* 清空输入 */
    document.getElementById('chpwd-old').value = '';
    document.getElementById('chpwd-new').value = '';
    document.getElementById('chpwd-new2').value = '';
}

function hideChangePassword() {
    hideAuthPanel();
}

function changePassword() {
    var token   = localStorage.getItem('bed_token') || '';
    var oldPwd  = document.getElementById('chpwd-old').value;
    var newPwd  = document.getElementById('chpwd-new').value;
    var newPwd2 = document.getElementById('chpwd-new2').value;
    var errEl   = document.getElementById('chpwd-error');

    if (!oldPwd) {
        errEl.textContent = '请输入原密码';
        errEl.className = 'auth-error'; return;
    }
    if (newPwd.length < 6) {
        errEl.textContent = '新密码至少 6 位';
        errEl.className = 'auth-error'; return;
    }
    if (newPwd !== newPwd2) {
        errEl.textContent = '两次新密码输入不一致';
        errEl.className = 'auth-error'; return;
    }

    errEl.textContent = '提交中...';
    errEl.className = 'auth-error';

    fetch(AUTH_URL + '/api/change_password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            token: token,
            old_password: oldPwd,
            new_password: newPwd
        })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) {
            errEl.textContent = data.message;
            errEl.className = 'auth-error success';
            setTimeout(function() { hideChangePassword(); }, 1500);
        } else {
            errEl.textContent = data.message;
            errEl.className = 'auth-error';
        }
    })
    .catch(function() {
        errEl.textContent = '网络错误，请检查网络后重试';
        errEl.className = 'auth-error';
    });
}

/* ─── 退出登录 ──────────────────────────────────── */
function logout() {
    localStorage.removeItem('bed_token');
    localStorage.removeItem('bed_username');
    updateUserUI(null);
    showAuthOverlay('login');
    /* 关闭下拉菜单 */
    document.getElementById('user-dropdown').style.display = 'none';
}

/* ─── 用户菜单 ──────────────────────────────────── */
function toggleUserMenu() {
    if (!localStorage.getItem('bed_token')) {
        showAuthOverlay('login');
        return;
    }
    var dropdown = document.getElementById('user-dropdown');
    dropdown.style.display = dropdown.style.display === 'none' ? 'block' : 'none';
}

/* 点击其他地方关闭菜单 */
document.addEventListener('click', function(e) {
    var section = document.getElementById('user-section');
    var dropdown = document.getElementById('user-dropdown');
    if (section && dropdown && !section.contains(e.target)) {
        dropdown.style.display = 'none';
    }
});

/* 清空所有认证错误提示和输入 */
function clearAuthErrors() {
    var errors = document.querySelectorAll('.auth-error');
    errors.forEach(function(el) {
        el.textContent = '';
        el.className = 'auth-error';
    });
    var inputs = document.querySelectorAll('.auth-form input');
    inputs.forEach(function(el) { el.value = ''; });
}

/* ─── 服务器配置 ──────────────────────────────────── */
const WS_URL  = 'ws://60.205.235.150:9001';
const AUDIO_SOURCE_BASE = 'http://60.205.235.150:8080/source/';

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
        var d;
        try {
            d = JSON.parse(payload);
        } catch (e) {
            console.error('JSON parse error:', e, payload);
            return;
        }
        updateDisplay(d);
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

var s_lastSleepRecording = null;  /* 缓存上一次的睡眠记录状态 */

function updateDisplay(d) {
    console.log('updateDisplay called, body_temp=' + d.body_temp + ' bcg_person=' + d.bcg_person);
    setVal('env-temp', d.env_temp,  '°C');
    setVal('env-hum',  d.env_hum,   '%');
    setVal('body-temp', d.body_temp, '°C');
    setVal('aqi',      aqiText(d.aqi), '');
    setVal('tvoc',     d.tvoc,      'ppb');
    setVal('eco2',     d.eco2,      'ppm');

    /* ── BCG 体征字段 ──────────────────────── */
    setVal('bcg-person', bcgPersonText(d.bcg_person), '');
    setVal('bcg-breath', d.bcg_breath, '次/分');
    setVal('bcg-heart',  d.bcg_heart,  'bpm');
    setVal('bcg-move',   bcgMoveText(d.bcg_move), '');

    /* 睡眠记录状态更新 — 仅在状态真正变化时刷新 UI */
    if (d.hasOwnProperty('sleep_recording') && d.sleep_recording !== s_lastSleepRecording) {
        s_lastSleepRecording = d.sleep_recording;
        updateSleepRecordUI(d.sleep_recording);
    }

    /* 睡眠报告更新 */
    if (d.hasOwnProperty('sr_valid')) {
        showSleepReport(d);
    }

    /* WiFi SSID 更新 */
    if (d.ssid) {
        $wifiLabel.textContent = d.ssid;
    }
}

function bcgPersonText(v) {
    return {0:'无人', 1:'有人'}[v] || '--';
}

function bcgMoveText(v) {
    return {0:'无体动', 1:'有体动'}[v] || '--';
}

function updateSleepRecordUI(recording) {
    var indicator = document.getElementById('sleep-record-indicator');
    var btnStart  = document.getElementById('btn-sleep-start');
    var btnStop   = document.getElementById('btn-sleep-stop');

    if (recording) {
        indicator.textContent = '● 睡眠记录中';
        indicator.className = 'sleep-indicator recording';
        btnStart.disabled = true;
        btnStop.disabled  = false;
    } else {
        indicator.textContent = '● 未在记录';
        indicator.className = 'sleep-indicator idle';
        btnStart.disabled = false;
        btnStop.disabled  = true;
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

function inferAudio(filename) {
    var url = AUDIO_SOURCE_BASE + encodeURIComponent(filename);
    sendCommand('classify_audio', url);
    addLog('audio', '请求推理: ' + filename);
}

function escapeHtml(s) {
    return String(s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function escapeJsArg(s) {
    return String(s)
        .replace(/\\/g, '\\\\')
        .replace(/'/g, "\\'");
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
        var nameText = escapeHtml(f.name);
        var nameArg = escapeJsArg(f.name);
        html += '<div class="audio-item">' +
                '<span class="audio-name">' + nameText + '</span>' +
                '<span class="audio-size">' + sizeKB + ' KB</span>' +
                '<div class="audio-actions">' +
                '<button class="btn btn-play" onclick="playAudio(\'' + nameArg + '\')">播放</button>' +
                '<button class="btn btn-infer" onclick="inferAudio(\'' + nameArg + '\')">推理</button>' +
                '</div>' +
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
document.getElementById('btn-warning-off').onclick = function() { sendCommand('warning', 0); };

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

/* ─── 睡眠记录 ──────────────────────────────────── */
function startSleepRecord() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        addLog('system', '未连接，无法发送指令');
        return;
    }
    sendCommand('mmwave_start_sleep', 1);
    addLog('radar', '发送: 开始睡眠记录');

    s_lastSleepRecording = true;
    updateSleepRecordUI(true);
}

function querySleepReport() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        addLog('system', '未连接，无法发送指令');
        return;
    }
    sendCommand('mmwave_query_sleep', 1);
    addLog('radar', '发送: 查询睡眠报告');
}

function showSleepReport(d) {
    var area = document.getElementById('sleep-report-area');
    if (!d.sr_valid) {
        area.style.display = 'block';
        area.innerHTML = '<div class="report-empty">暂无睡眠报告<br><small>请先结束睡眠记录后再查询</small></div>';
        return;
    }
    area.style.display = 'block';
    area.innerHTML =
        '<div class="report-table">' +
        '<div class="report-row"><span>上床</span><span>' + d.sr_bed + '</span></div>' +
        '<div class="report-row"><span>入睡</span><span>' + d.sr_sleep + '</span></div>' +
        '<div class="report-row"><span>醒来</span><span>' + d.sr_wake + '</span></div>' +
        '<div class="report-row"><span>下床</span><span>' + d.sr_up + '</span></div>' +
        '<div class="report-divider"></div>' +
        '<div class="report-row"><span>卧床时长</span><span>' + d.sr_bed_mins + ' 分钟</span></div>' +
        '<div class="report-row"><span>睡眠时长</span><span>' + d.sr_sleep_mins + ' 分钟</span></div>' +
        '<div class="report-row"><span>清醒时长</span><span>' + d.sr_awake_mins + ' 分钟</span></div>' +
        '<div class="report-row"><span>体动次数</span><span>' + d.sr_move_cnt + ' 次</span></div>' +
        '</div>';
}

function stopSleepRecord() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        addLog('system', '未连接，无法发送指令');
        return;
    }
    sendCommand('mmwave_end_sleep', 1);
    addLog('radar', '发送: 停止睡眠记录');

    s_lastSleepRecording = false;
    updateSleepRecordUI(false);
}

/* ─── 启动 ──────────────────────────────────────── */
updateClock();
setInterval(updateClock, 1000);

/* 登录状态检查完成后再连接 WebSocket */
checkAuth();

/* 监听认证完成事件，auth 通过后连接 WebSocket */
var authCheckInterval = setInterval(function() {
    var overlay = document.getElementById('auth-overlay');
    if (overlay && overlay.classList.contains('hidden')) {
        clearInterval(authCheckInterval);
        connect();
    }
}, 500);
