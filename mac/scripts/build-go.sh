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

# 漂移守卫：入库的 flowgrid.h 带着 C 侧要知道的注释（Go 的文档注释不会随
# 构建过来），但**导出符号列表**必须跟生成的一致。改 cgo 导出却忘了同步这份
# 头，症状是「C 侧调不到新函数」或「声明与实现对不上」——很难查，所以在这里
# 挡住：先比，再刷。
extract_externs() {
    grep -E '^extern ' "$1" | sed 's/[[:space:]]\+/ /g; s/[[:space:]]*$//' | sort
}
if ! diff -q <(extract_externs "$OUT_DIR/libflowgrid.h") \
             <(extract_externs "$HEADER_DIR/flowgrid.h") >/dev/null; then
    echo "!! 入库的 flowgrid.h 与生成头的导出列表不一致——cgo 导出漂了。" >&2
    diff <(extract_externs "$HEADER_DIR/flowgrid.h") \
         <(extract_externs "$OUT_DIR/libflowgrid.h") | head -40 >&2
    echo "   改 cgo 导出后要同步两份手抄头：" >&2
    echo "     protocol/cgo/cgo.h" >&2
    echo "     mac/FlowGridCore/Sources/CFlowGrid/include/flowgrid.h" >&2
    exit 1
fi

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

echo "==> 剥壳版头文件已就位（本地构建用）"
echo "    注意：仓库里入库的是**带注释**的 flowgrid.h（Go 的文档注释不会随构建"
echo "    过来，C 侧要知道的都写在那份里）。提交前请把它还原："
echo "        git checkout -- mac/FlowGridCore/Sources/CFlowGrid/include/flowgrid.h"

echo "OK"
