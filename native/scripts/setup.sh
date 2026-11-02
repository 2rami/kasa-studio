#!/bin/sh
set -eu
NATIVE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$NATIVE_DIR/vendor" "$NATIVE_DIR/../artifacts/native"
if [ ! -f "$NATIVE_DIR/vendor/imgui/imgui.h" ]; then
    RUN_DIR=$(mktemp -d "$NATIVE_DIR/../artifacts/native/setup.XXXXXX")
    curl -fsSL 'https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.93.0' -o "$RUN_DIR/imgui.tar.gz"
    printf '%s  %s\n' 'f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee' "$RUN_DIR/imgui.tar.gz" | shasum -a 256 -c -
    mkdir -p "$NATIVE_DIR/vendor/imgui"
    tar -xzf "$RUN_DIR/imgui.tar.gz" --strip-components=1 -C "$NATIVE_DIR/vendor/imgui"
fi
printf '%s\n' 'C++ UI 의존성 준비 완료. 공식 Cubism Native SDK 경로는 CMake의 CUBISM_SDK_ROOT로 지정할 수 있어요.'
