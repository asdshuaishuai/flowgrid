#!/usr/bin/env bash
# 把 protocol/cgo 编成 c-archive（macOS 上的 FFI 面）。
# 产物：
#   mac/Vendor/libflowgrid.a                          — 供 app/测试 target 链接（gitignore，按需重建）
#   mac/FlowGridCore/Sources/CFlowGrid/include/flowgrid.h — C 头（随构建刷新，提交入库）
# 不触碰 protocol/cgo/cgo.h（仓库约定：仅导出变更时才需重新生成）。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$ROOT/mac/Vendor"
HEADER_DIR="$ROOT/mac/FlowGridCore/Sources/CFlowGrid/include"
CGO_DIR="$ROOT/protocol/cgo"

mkdir -p "$OUT_DIR" "$HEADER_DIR"

echo "==> go build -buildmode=c-archive (protocol/cgo)"
(cd "$CGO_DIR" && CGO_ENABLED=1 go build -buildmode=c-archive -o "$OUT_DIR/libflowgrid.a" .)

echo "==> refresh C header"
cp "$OUT_DIR/libflowgrid.h" "$HEADER_DIR/flowgrid.h"

echo "==> artifacts"
ls -l "$OUT_DIR/libflowgrid.a"
echo "    header: $HEADER_DIR/flowgrid.h"

echo "==> smoke link check"
TMP="$(mktemp -d)"
cat > "$TMP/main.c" <<'EOF'
#include "flowgrid.h"
#include <stdio.h>
int main(void) {
    size_t n = 0;
    unsigned char *buf = fg_frame_encode(0x08, 0, 0, NULL, 0, &n);
    if (!buf) return 1;
    printf("frame bytes=%zu\n", n);
    fg_free_buffer(buf);
    return 0;
}
EOF
cc -I"$HEADER_DIR" "$TMP/main.c" "$OUT_DIR/libflowgrid.a" -o "$TMP/a.out" -framework CoreFoundation
"$TMP/a.out"
rm -rf "$TMP"

echo "OK"
