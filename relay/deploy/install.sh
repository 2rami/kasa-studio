#!/bin/sh
set -eu
STAGE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
id -u kasa-face >/dev/null 2>&1 || useradd --system --home-dir /var/lib/kasa-face --shell /usr/sbin/nologin kasa-face
install -d -m 0755 /opt/kasa-face /etc/kasa-face
install -d -o kasa-face -g kasa-face -m 0700 /var/lib/kasa-face
python3 -m venv /opt/kasa-face/venv
/opt/kasa-face/venv/bin/pip install --disable-pip-version-check --no-cache-dir -r "$STAGE_DIR/requirements.txt"
install -m 0644 "$STAGE_DIR/server.py" /opt/kasa-face/server.py
install -m 0644 "$STAGE_DIR/Caddyfile" /etc/kasa-face/Caddyfile
install -m 0644 "$STAGE_DIR/kasa-face-relay.service" "$STAGE_DIR/kasa-face-edge.service" /etc/systemd/system/
/usr/local/bin/caddy validate --config /etc/kasa-face/Caddyfile --adapter caddyfile
python3 - "$STAGE_DIR" <<'PY'
from pathlib import Path
import shutil
import sys
target = Path('/etc/nginx/stream.d/kasa-sni.conf')
data = target.read_text()
if 'face.debimarlene.com ' not in data:
    assert data.count('relay-kr.debimarlene.com 127.0.0.1:8442;') == 1
    backup = Path(sys.argv[1]) / 'kasa-sni.before'
    assert not backup.exists()
    shutil.copy2(target, backup)
    data = data.replace('relay-kr.debimarlene.com 127.0.0.1:8442;', 'face.debimarlene.com 127.0.0.1:19443;\n        relay-kr.debimarlene.com 127.0.0.1:8442;')
    target.write_text(data)
PY
nginx -t
systemctl daemon-reload
systemctl enable --now kasa-face-relay.service kasa-face-edge.service
systemctl reload nginx.service
curl --retry 10 --retry-delay 1 --retry-connrefused -fsS http://127.0.0.1:19444/health
