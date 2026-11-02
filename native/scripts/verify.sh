#!/bin/sh
set -eu
NATIVE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RUN_DIR=$(mktemp -d "$NATIVE_DIR/../artifacts/native/verify.XXXXXX")
APP="$NATIVE_DIR/build/Kasa Studio Native.app/Contents/MacOS/Kasa Studio Native"
ctest --test-dir "$NATIVE_DIR/build" --output-on-failure
KASA_STUDIO_NATIVE_DATA="$RUN_DIR/data" KASA_STUDIO_AGENT_PORT=0 "$APP" --headless --frames 90 --snapshot "$RUN_DIR/studio.png" --pet-shot "$RUN_DIR/pet.png" --report "$RUN_DIR/report.json" > "$RUN_DIR/run.log" 2>&1
python3 - "$RUN_DIR/report.json" <<'PY'
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
data = json.loads(path.read_text())
assert data['modelReady']
assert data['glError'] == data['avatarGlError'] == data['uiGlError'] == 0
assert not data['mainVisible'] and not data['petVisible']
assert data['petTransparent'] and data['petFloating'] and data['clickThroughChecked']
assert data['petAlpha']['transparentPixels'] > 100 and data['petAlpha']['visiblePixels'] > 100
assert data['petAlpha']['coloredPixels'] > 100
assert data['avatar']['texture']['coloredPixels'] > 100
assert not data['services']['error']
print('C++ 서비스·공식 SDK 렌더·투명 펫 검증 통과')
print(path.parent)
PY
python3 "$NATIVE_DIR/tests/ui_verify.py" "$APP" "$RUN_DIR"
