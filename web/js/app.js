/* ─── 用户认证 ─────────────────────────────────────────── */
const AUTH_URL = 'http://60.205.235.150:9003';

function checkAuth() {
    var token = localStorage.getItem('bed_token');
    if (!token) { showAuthOverlay('login'); return; }
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
        if (data.success) { onLoginSuccess(data.username, token); }
        else {
            localStorage.removeItem('bed_token');
            localStorage.removeItem('bed_username');
            showAuthOverlay('login');
        }
    })
    .catch(function() {
        var username = localStorage.getItem('bed_username');
        if (username) { onLoginSuccess(username, token); } else { showAuthOverlay('login'); }
    });
}

function showAuthOverlay(mode) {
    document.getElementById('auth-overlay').classList.remove('hidden');
    ['auth-login','auth-register','auth-chpwd'].forEach(function(id) {
        document.getElementById(id).classList.remove('active');
    });
    var map = { login: 'auth-login', register: 'auth-register' };
    if (map[mode]) document.getElementById(map[mode]).classList.add('active');
    clearAuthErrors();
}
function showAuthPanel(mode) { showAuthOverlay(mode); }
function hideAuthPanel() { document.getElementById('auth-overlay').classList.add('hidden'); }

function onLoginSuccess(username, token) {
    localStorage.setItem('bed_token', token);
    localStorage.setItem('bed_username', username);
    hideAuthPanel();
    updateUserUI(username);
}

function updateUserUI(username) {
    var label    = document.getElementById('user-label');
    var dropName = document.getElementById('dropdown-username');
    var btn      = document.getElementById('btn-user');
    if (username) {
        label.textContent = username;
        dropName.textContent = username;
        btn.classList.add('btn-logged-in');
    } else {
        label.textContent = '未登录';
        btn.classList.remove('btn-logged-in');
    }
}

function login() {
    var username = document.getElementById('login-username').value.trim();
    var password = document.getElementById('login-password').value;
    var errEl    = document.getElementById('login-error');
    if (!username || !password) { errEl.textContent = '请输入用户名和密码'; return; }
    errEl.textContent = '登录中...';
    fetch(AUTH_URL + '/api/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, password: password })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) { onLoginSuccess(data.username, data.token); }
        else { errEl.textContent = data.message; }
    })
    .catch(function() { errEl.textContent = '网络错误'; });
}

document.addEventListener('DOMContentLoaded', function() {
    ['login-password','login-username'].forEach(function(id) {
        var el = document.getElementById(id);
        if (el) el.addEventListener('keydown', function(e) { if (e.key === 'Enter') login(); });
    });
});

function register() {
    var username  = document.getElementById('reg-username').value.trim();
    var email     = document.getElementById('reg-email').value.trim();
    var password  = document.getElementById('reg-password').value;
    var password2 = document.getElementById('reg-password2').value;
    var errEl     = document.getElementById('reg-error');
    if (!username || username.length < 3 || username.length > 20) { errEl.textContent = '用户名需 3-20 位'; return; }
    if (password.length < 6) { errEl.textContent = '密码至少 6 位'; return; }
    if (password !== password2) { errEl.textContent = '两次密码不一致'; return; }
    errEl.textContent = '注册中...';
    fetch(AUTH_URL + '/api/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, password: password, email: email })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) {
            errEl.className = 'auth-error success';
            errEl.textContent = data.message + '，请登录';
            setTimeout(function() { showAuthPanel('login'); }, 1500);
        } else { errEl.textContent = data.message; }
    })
    .catch(function() { errEl.textContent = '网络错误'; });
}

function showChangePassword() {
    document.getElementById('auth-overlay').classList.remove('hidden');
    ['auth-login','auth-register'].forEach(function(id) { document.getElementById(id).classList.remove('active'); });
    document.getElementById('auth-chpwd').classList.add('active');
    clearAuthErrors();
    ['chpwd-old','chpwd-new','chpwd-new2'].forEach(function(id) { document.getElementById(id).value = ''; });
}
function hideChangePassword() { hideAuthPanel(); }

function changePassword() {
    var token   = localStorage.getItem('bed_token') || '';
    var oldPwd  = document.getElementById('chpwd-old').value;
    var newPwd  = document.getElementById('chpwd-new').value;
    var newPwd2 = document.getElementById('chpwd-new2').value;
    var errEl   = document.getElementById('chpwd-error');
    if (!oldPwd) { errEl.textContent = '请输入原密码'; return; }
    if (newPwd.length < 6) { errEl.textContent = '新密码至少 6 位'; return; }
    if (newPwd !== newPwd2) { errEl.textContent = '两次新密码不一致'; return; }
    errEl.textContent = '提交中...';
    fetch(AUTH_URL + '/api/change_password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: token, old_password: oldPwd, new_password: newPwd })
    })
    .then(function(r) { return r.json(); })
    .then(function(data) {
        if (data.success) {
            errEl.className = 'auth-error success';
            errEl.textContent = data.message;
            setTimeout(hideChangePassword, 1500);
        } else { errEl.textContent = data.message; }
    })
    .catch(function() { errEl.textContent = '网络错误'; });
}

function logout() {
    localStorage.removeItem('bed_token');
    localStorage.removeItem('bed_username');
    updateUserUI(null);
    showAuthOverlay('login');
    document.getElementById('user-dropdown').style.display = 'none';
}

function toggleUserMenu() {
    if (!localStorage.getItem('bed_token')) { showAuthOverlay('login'); return; }
    var d = document.getElementById('user-dropdown');
    d.style.display = d.style.display === 'none' ? 'block' : 'none';
}

document.addEventListener('click', function(e) {
    var section  = document.getElementById('user-section');
    var dropdown = document.getElementById('user-dropdown');
    if (section && dropdown && !section.contains(e.target)) dropdown.style.display = 'none';
});

function clearAuthErrors() {
    document.querySelectorAll('.auth-error').forEach(function(el) { el.textContent = ''; el.className = 'auth-error'; });
}

/* ─── 服务器配置 ─────────────────────────────────────── */
const WS_URL            = 'ws://60.205.235.150:9001';
const AUDIO_SOURCE_BASE = 'http://60.205.235.150:8080/source/';
const TOPIC_STATUS      = 'bed/status';
const TOPIC_HEARTBEAT   = 'bed/heartbeat';
const TOPIC_CONTROL     = 'bed/control';
const TOPIC_BCG         = 'bed/bcg';

/* ─── DOM ────────────────────────────────────────────── */
const $dot      = document.getElementById('online-dot');
const $wifiLabel= document.getElementById('wifi-label');
const $logList  = document.getElementById('log-list');
const $clock    = document.getElementById('clock');
/* ─── WebSocket ──────────────────────────────────────── */
let ws = null;
let heartbeatTimer = null;

function connect() {
    ws = new WebSocket(WS_URL);
    ws.onopen    = function() { setOnline(true); addLog('system', 'WebSocket 已连接'); requestAudioList(); };
    ws.onclose   = function() { setOnline(false); addLog('system', 'WebSocket 断开，3s 后重连...'); setTimeout(connect, 3000); };
    ws.onerror   = function() { addLog('system', 'WebSocket 错误'); };
    ws.onmessage = function(evt) { try { handleRawMessage(JSON.parse(evt.data)); } catch(e) {} };
}

function handleRawMessage(data) {
    if (data.type === 'audio_list') { handleAudioList(data.files); }
    else if (data.topic !== undefined) { handleMessage(data.topic, data.payload); }
}

function handleMessage(topic, payload) {
    addLog(topic, payload);
    if (topic === TOPIC_STATUS) {
        var d; try { d = JSON.parse(payload); } catch(e) { return; }
        updateEnvDisplay(d);
        if (d.ssid) $wifiLabel.textContent = d.ssid;
    } else if (topic === TOPIC_BCG) {
        var b; try { b = JSON.parse(payload); } catch(e) { return; }
        updateBcgDisplay(b);
    } else if (topic === TOPIC_HEARTBEAT) {
        resetHeartbeat();
    }
}

/* ─── 环境监测更新 ───────────────────────────────────── */
function aqiText(v) { return {1:'优',2:'良',3:'中等',4:'差',5:'劣'}[v] || '--'; }

function updateEnvDisplay(d) {
    setCard('env-temp',  d.env_temp,        '°C');
    setCard('env-hum',   d.env_hum,         '%');
    setCard('body-temp', d.body_temp,       '°C');
    setCard('aqi',       aqiText(d.aqi),    '');
    setCard('tvoc',      d.tvoc,            'ppb');
    setCard('eco2',      d.eco2,            'ppm');
}

/* ─── BCG 体征更新 ───────────────────────────────────── */
function sleepStateText(v) { return {0:'觉醒',1:'浅睡',2:'深睡',3:'快速眼动',4:'离床'}[v] || '--'; }
function fatigueText(v) {
    if (v === undefined || v === null) return '--';
    if (v === 0) return '离床';
    if (v >= 36) return '清醒 (' + v + ')';
    if (v >= 20) return '轻度疲劳 (' + v + ')';
    return '重度疲劳 (' + v + ')';
}
function breathHoldText(v) { return {0:'正常呼吸',1:'憋气',4:'离床'}[v] || '--'; }
function stressText(v) {
    if (v === undefined || v === null) return '--';
    if (v < 50) return '放松 (' + v + ')';
    if (v <= 200) return '中等应激 (' + v + ')';
    return '高度应激 (' + v + ')';
}

function updateBcgDisplay(b) {
    if (b.hr      !== undefined) setCard('bcg-hr',          b.hr,                    'bpm');
    if (b.rr      !== undefined) setCard('bcg-rr',          b.rr,                    '次/分');
    if (b.sleep   !== undefined) setId('val-bcg-sleep',     sleepStateText(b.sleep));
    if (b.fatigue !== undefined) setId('val-bcg-fatigue',   fatigueText(b.fatigue));
    if (b.breath  !== undefined) setId('val-bcg-breath-hold', breathHoldText(b.breath));
    if (b.stress  !== undefined) setId('val-bcg-stress',    stressText(b.stress));
    /* 更新时间戳 */
    var now = new Date();
    setId('val-bcg-time',
        now.getHours().toString().padStart(2,'0') + ':' +
        now.getMinutes().toString().padStart(2,'0') + ':' +
        now.getSeconds().toString().padStart(2,'0'));
    resetHeartbeat();
}

/* 写 card-value，并同步更新紧邻的 card-unit */
function setCard(id, val, unit) {
    var valEl = document.getElementById('val-' + id);
    if (!valEl) return;
    valEl.textContent = (val === undefined || val === null) ? '--' : val;
    var unitEl = valEl.nextElementSibling;
    if (unitEl && unitEl.classList.contains('card-unit')) unitEl.textContent = unit;
}

function setId(id, text) {
    var el = document.getElementById(id);
    if (el) el.textContent = text;
}

/* ─── 发送控制指令 ────────────────────────────────────── */
function sendCommand(cmd, value) {
    if (!ws || ws.readyState !== WebSocket.OPEN) { addLog('system', '未连接'); return; }
    var payload = JSON.stringify({ cmd: cmd, value: value });
    ws.send(JSON.stringify({ topic: TOPIC_CONTROL, payload: payload }));
    addLog(TOPIC_CONTROL, payload);
}

/* ─── 音频 ───────────────────────────────────────────── */
function requestAudioList() {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ type: 'list_audio' }));
}
function playAudio(filename) {
    if (!ws || ws.readyState !== WebSocket.OPEN) { addLog('system', '未连接'); return; }
    ws.send(JSON.stringify({ type: 'play_audio', file: filename }));
    addLog('audio', '请求播放: ' + filename);
}
function inferAudio(filename) {
    sendCommand('classify_audio', AUDIO_SOURCE_BASE + encodeURIComponent(filename));
    addLog('audio', '请求推理: ' + filename);
}

function escHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}
function escJs(s) { return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'"); }

function handleAudioList(files) {
    var list = document.getElementById('audio-list');
    if (!files || files.length === 0) { list.innerHTML = '<div class="empty-hint">没有 WAV 文件</div>'; return; }
    var html = '';
    files.forEach(function(f) {
        var sz = (f.size / 1024).toFixed(1);
        html += '<div class="audio-item">' +
                '<span class="audio-name">' + escHtml(f.name) + '</span>' +
                '<span class="audio-size">' + sz + ' KB</span>' +
                '<div class="audio-actions">' +
                '<button class="btn-play" onclick="playAudio(\'' + escJs(f.name) + '\')">播放</button>' +
                '<button class="btn-infer" onclick="inferAudio(\'' + escJs(f.name) + '\')">推理</button>' +
                '</div></div>';
    });
    list.innerHTML = html;
}

/* ─── 在线状态 ───────────────────────────────────────── */
function setOnline(on) {
    $dot.className = 'dot ' + (on ? 'online' : 'offline');
    if (!on) $wifiLabel.textContent = '未连接';
}
function resetHeartbeat() {
    setOnline(true);
    if (heartbeatTimer) clearTimeout(heartbeatTimer);
    heartbeatTimer = setTimeout(function() { setOnline(false); }, 15000);
}

/* ─── 实时时钟 ───────────────────────────────────────── */
function updateClock() {
    var now = new Date();
    $clock.textContent = now.getHours().toString().padStart(2,'0') + ':' +
                         now.getMinutes().toString().padStart(2,'0') + ':' +
                         now.getSeconds().toString().padStart(2,'0');
}

/* ─── 日志 ───────────────────────────────────────────── */
function addLog(topic, msg) {
    var div = document.createElement('div');
    var ts  = new Date().toTimeString().slice(0,8);
    div.innerHTML = '<span class="log-time">'  + ts                                + '</span>' +
                    '<span class="log-topic">[' + topic                            + ']</span>' +
                    '<span class="log-msg">'   + escHtml(String(msg).slice(0,200)) + '</span>';
    $logList.prepend(div);
    if ($logList.children.length > 60) $logList.lastChild.remove();
}

/* ─── 按钮绑定 ───────────────────────────────────────── */
document.getElementById('btn-clear-log').onclick = function() { $logList.innerHTML = ''; };
document.getElementById('btn-refresh-audio').onclick = function() { requestAudioList(); addLog('audio', '刷新列表'); };

/* ─── 启动 ───────────────────────────────────────────── */
updateClock();
setInterval(updateClock, 1000);
checkAuth();
var authCheckInterval = setInterval(function() {
    if (document.getElementById('auth-overlay').classList.contains('hidden')) {
        clearInterval(authCheckInterval);
        connect();
    }
}, 500);
