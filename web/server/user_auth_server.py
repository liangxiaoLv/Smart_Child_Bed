#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
智能儿童床 - 用户认证服务
运行端口: 9003
Python 3.6+ 标准库实现，无第三方依赖
"""

import json
import hashlib
import secrets
import sqlite3
import os
import re
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from urllib.parse import urlparse

# ── 配置 ────────────────────────────────────────────
HOST = "0.0.0.0"
PORT = 9003
DB_DIR = "/var/www/bed"
DB_PATH = os.path.join(DB_DIR, "users.db")
TOKEN_EXPIRE_DAYS = 30  # token 有效天数（当前仅做记录，未强制过期）

# ── 数据库 ──────────────────────────────────────────


def get_db():
    """获取数据库连接"""
    os.makedirs(DB_DIR, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


def init_db():
    """初始化数据库表"""
    conn = get_db()
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS users (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            username    TEXT    UNIQUE NOT NULL,
            password_hash TEXT   NOT NULL,
            salt        TEXT    NOT NULL,
            email       TEXT    DEFAULT '',
            created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS tokens (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL,
            token       TEXT    UNIQUE NOT NULL,
            created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(id)
        );
    """)
    conn.commit()
    conn.close()


# ── 密码工具 ────────────────────────────────────────


def hash_password(password, salt=None):
    """SHA-256(password + salt)，返回 (hash_hex, salt_hex)"""
    if salt is None:
        salt = secrets.token_hex(16)
    h = hashlib.sha256((password + salt).encode("utf-8")).hexdigest()
    return h, salt


def validate_username(username):
    """验证用户名：3-20 位，字母数字下划线"""
    if not username or len(username) < 3 or len(username) > 20:
        return False
    return bool(re.match(r"^[a-zA-Z0-9_一-鿿]+$", username))


def validate_password(password):
    """验证密码：至少 6 位"""
    return password and len(password) >= 6


def validate_email(email):
    """简单邮箱格式验证"""
    if not email:
        return True  # 邮箱可选
    return bool(re.match(r"^[^@\s]+@[^@\s]+\.[^@\s]+$", email))


# ── 数据库操作 ──────────────────────────────────────


def db_register(username, password, email):
    """注册用户，返回 (success, message)"""
    if not validate_username(username):
        return False, "用户名需 3-20 位，仅支持字母、数字、下划线、中文"
    if not validate_password(password):
        return False, "密码至少 6 位"
    if not validate_email(email):
        return False, "邮箱格式不正确"

    pw_hash, salt = hash_password(password)
    conn = get_db()
    try:
        conn.execute(
            "INSERT INTO users (username, password_hash, salt, email) VALUES (?, ?, ?, ?)",
            (username, pw_hash, salt, email or ""),
        )
        conn.commit()
        return True, "注册成功"
    except sqlite3.IntegrityError:
        return False, "用户名已存在"
    finally:
        conn.close()


def db_login(username, password):
    """登录验证，返回 (success, token_or_message, username)"""
    conn = get_db()
    row = conn.execute(
        "SELECT id, username, password_hash, salt FROM users WHERE username = ?",
        (username,),
    ).fetchone()

    if not row:
        conn.close()
        return False, "用户名或密码错误", ""

    pw_hash, _ = hash_password(password, row["salt"])
    if pw_hash != row["password_hash"]:
        conn.close()
        return False, "用户名或密码错误", ""

    # 生成 token
    token = secrets.token_hex(32)
    # 删除该用户旧 token（保证单设备登录）
    conn.execute("DELETE FROM tokens WHERE user_id = ?", (row["id"],))
    conn.execute(
        "INSERT INTO tokens (user_id, token) VALUES (?, ?)",
        (row["id"], token),
    )
    conn.commit()
    conn.close()
    return True, token, row["username"]


def db_get_users():
    """获取所有注册用户列表，返回 [{id, username, email, created_at}, ...]"""
    conn = get_db()
    rows = conn.execute(
        "SELECT id, username, email, created_at FROM users ORDER BY id DESC"
    ).fetchall()
    conn.close()
    return [{"id": r["id"], "username": r["username"], "email": r["email"], "created_at": r["created_at"]} for r in rows]


def db_verify_token(token):
    """验证 token，返回 (success, username)"""
    conn = get_db()
    row = conn.execute(
        "SELECT u.username FROM tokens t JOIN users u ON t.user_id = u.id WHERE t.token = ?",
        (token,),
    ).fetchone()
    conn.close()
    if row:
        return True, row["username"]
    return False, ""


def db_change_password(token, old_password, new_password):
    """修改密码，返回 (success, message)"""
    if not validate_password(new_password):
        return False, "新密码至少 6 位"

    conn = get_db()
    row = conn.execute(
        "SELECT u.id, u.password_hash, u.salt FROM tokens t JOIN users u ON t.user_id = u.id WHERE t.token = ?",
        (token,),
    ).fetchone()

    if not row:
        conn.close()
        return False, "登录已过期，请重新登录"

    # 验证旧密码
    old_hash, _ = hash_password(old_password, row["salt"])
    if old_hash != row["password_hash"]:
        conn.close()
        return False, "原密码不正确"

    # 更新密码
    new_hash, new_salt = hash_password(new_password)
    conn.execute(
        "UPDATE users SET password_hash = ?, salt = ? WHERE id = ?",
        (new_hash, new_salt, row["id"]),
    )
    conn.commit()
    conn.close()
    return True, "密码修改成功"


# ── HTTP 请求处理 ───────────────────────────────────


class AuthHandler(BaseHTTPRequestHandler):
    """用户认证 API 请求处理器"""

    def log_message(self, format, *args):
        """自定义日志输出"""
        print(f"[AUTH] {self.client_address[0]} - {args[0]}")

    def _send_json(self, code, data):
        """发送 JSON 响应"""
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self._cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def _cors_headers(self):
        """CORS 跨域头"""
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _read_body(self):
        """读取请求体 JSON"""
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return {}
        raw = self.rfile.read(length)
        try:
            return json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return {}

    def do_OPTIONS(self):
        """预检请求"""
        self.send_response(204)
        self._cors_headers()
        self.end_headers()

    def do_GET(self):
        """处理 GET 请求"""
        path = urlparse(self.path).path

        if path == "/api/users":
            self._handle_users()
        else:
            self._send_json(404, {"success": False, "message": "接口不存在"})

    def do_POST(self):
        """处理 POST 请求，路由到对应 API"""
        path = urlparse(self.path).path
        body = self._read_body()

        if path == "/api/register":
            self._handle_register(body)
        elif path == "/api/login":
            self._handle_login(body)
        elif path == "/api/change_password":
            self._handle_change_password(body)
        elif path == "/api/verify_token":
            self._handle_verify_token(body)
        else:
            self._send_json(404, {"success": False, "message": "接口不存在"})

    def _handle_register(self, body):
        username = (body.get("username") or "").strip()
        password = (body.get("password") or "")
        email = (body.get("email") or "").strip()

        if not username or not password:
            self._send_json(400, {"success": False, "message": "用户名和密码不能为空"})
            return

        ok, msg = db_register(username, password, email)
        code = 200 if ok else 400
        self._send_json(code, {"success": ok, "message": msg})

    def _handle_login(self, body):
        username = (body.get("username") or "").strip()
        password = body.get("password") or ""

        if not username or not password:
            self._send_json(400, {"success": False, "message": "用户名和密码不能为空"})
            return

        ok, data, name = db_login(username, password)
        if ok:
            self._send_json(200, {"success": True, "token": data, "username": name})
        else:
            self._send_json(401, {"success": False, "message": data})

    def _handle_change_password(self, body):
        token = (body.get("token") or "").strip()
        old_pwd = body.get("old_password") or ""
        new_pwd = body.get("new_password") or ""

        if not token or not old_pwd or not new_pwd:
            self._send_json(400, {"success": False, "message": "参数不完整"})
            return

        ok, msg = db_change_password(token, old_pwd, new_pwd)
        code = 200 if ok else 400
        self._send_json(code, {"success": ok, "message": msg})

    def _handle_verify_token(self, body):
        token = (body.get("token") or "").strip()
        if not token:
            self._send_json(401, {"success": False, "message": "token 为空"})
            return

        ok, username = db_verify_token(token)
        if ok:
            self._send_json(200, {"success": True, "username": username})
        else:
            self._send_json(401, {"success": False, "message": "token 无效或已过期"})

    def _handle_users(self):
        """返回所有注册用户列表"""
        users = db_get_users()
        self._send_json(200, {"success": True, "users": users, "total": len(users)})


class ThreadedAuthServer(ThreadingMixIn, HTTPServer):
    """多线程认证服务器"""
    allow_reuse_address = True
    daemon_threads = True


# ── 入口 ────────────────────────────────────────────


def main():
    init_db()
    server = ThreadedAuthServer((HOST, PORT), AuthHandler)
    print(f"[AUTH] 用户认证服务启动: http://{HOST}:{PORT}")
    print(f"[AUTH] 数据库: {DB_PATH}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[AUTH] 服务已停止")
        server.shutdown()


if __name__ == "__main__":
    main()
