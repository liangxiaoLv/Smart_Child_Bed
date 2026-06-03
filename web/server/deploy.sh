#!/bin/bash
# 用户认证服务部署脚本
# 用法: 在本地执行此脚本（Windows 需 Git Bash / WSL）
#   bash deploy.sh

set -e

SERVER="root@60.205.235.150"
SSH_KEY="$HOME/.ssh/id_ed25519_claude"
SSH="ssh -i $SSH_KEY $SERVER"
SCP="scp -i $SSH_KEY"

echo "=== 1. 上传 user_auth_server.py ==="
$SCP user_auth_server.py $SERVER:/opt/user_auth_server.py
$SSH "chmod +x /opt/user_auth_server.py"

echo ""
echo "=== 2. 创建 systemd 服务 ==="
$SSH "cat > /etc/systemd/system/user_auth.service << 'SVC_EOF'
[Unit]
Description=Smart Child Bed - User Auth Service
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt
ExecStart=/usr/bin/python3 /opt/user_auth_server.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
SVC_EOF"

echo ""
echo "=== 3. 启动服务 ==="
$SSH "systemctl daemon-reload && systemctl enable user_auth && systemctl restart user_auth"

echo ""
echo "=== 4. 检查状态 ==="
$SSH "systemctl status user_auth --no-pager"

echo ""
echo "=== 部署完成 ==="
echo "端口: 9003"
echo "测试: curl -X POST http://60.205.235.150:9003/api/register -H 'Content-Type: application/json' -d '{\"username\":\"test\",\"password\":\"123456\",\"email\":\"test@test.com\"}'"
