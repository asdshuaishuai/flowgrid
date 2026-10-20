#!/usr/bin/env bash
#
# FlowGrid deepin (DDE 25) 客户端 —— 开发模式脚本。
#
# 和 install.sh 的分工：
#   install.sh  装到系统位置（/usr/local + /usr/share），需要 sudo，给最终用户用。
#   dev.sh      只在开发机上跑，不装任何东西、不碰系统路径、不需要 root。
#               改完代码直接重跑，不用先安装一遍。
#
# 为什么不复用 install.sh：它会 sudo 装到 /usr/local 并覆盖系统里的那份，
# 开发过程中每改一次就装一次，既慢又容易把「系统里那份旧的」和「刚编的这份」
# 搞混。dev.sh 用独立的 build-dev 目录，两个互不干扰。
#
# 用法：
#   ./deepin/dev.sh                    # 配置 + 编译（Debug）+ 直接跑起来（Ctrl+C 停）
#   ./deepin/dev.sh --skip-build       # 跳过编译，直接跑已有产物
#   ./deepin/dev.sh --release          # 用 Release 编译（默认 Debug）
#   ./deepin/dev.sh --check            # 体检：界面自检 + 明暗快照 + 协议层只读校验
#   ./deepin/dev.sh --snapshot [目录]  # 只出无头快照（默认 /tmp/fgdev）
#   ./deepin/dev.sh --theme light|dark # 快照主题（默认明暗各出一套）
#   ./deepin/dev.sh --help             # 本段用法
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/deepin"
BUILD_DIR="${SRC_DIR}/build-dev"
BIN_NAME="flowgrid-deepin"
TEST_NAME="flowgrid-uitest"

BUILD=1
BUILD_TYPE="Debug"
MODE="run"
SNAPSHOT_DIR="/tmp/fgdev"
THEME="both"

die() { printf '\033[31m✗ %s\033[0m\n' "$*" >&2; exit 1; }
ok()  { printf '\033[32m✓\033[0m %s\n' "$*"; }
note(){ printf '\033[33m!\033[0m %s\n' "$*"; }

usage() {
    # 从第 3 行起打印开头那段注释，遇到第一个非注释行就停。写死行号的话，
    # 以后改注释就会把 set -euo pipefail 之类的行一起打出来。
    awk 'NR < 3 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "${BASH_SOURCE[0]}"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) BUILD=0 ;;
        --release)    BUILD_TYPE="Release" ;;
        --check)      MODE="check" ;;
        --snapshot)
            MODE="snapshot"
            # 目录是可选的：写成「--snapshot /tmp/x」和「--snapshot --theme dark」
            # 都能认。下一个参数只要不是选项，就当目录。
            if [ $# -gt 1 ] && [ "${2#-}" = "$2" ]; then
                SNAPSHOT_DIR="$2"
                shift
            fi
            ;;
        --theme)      THEME="${2:-both}"; shift ;;
        -h|--help)    usage ;;
        *)            die "未知参数：$1（--help 看用法）" ;;
    esac
    shift
done

case "${THEME}" in
    light|dark|both) ;;
    *) die "--theme 只认 light / dark / both" ;;
esac

# ── 0. 环境 ────────────────────────────────────────────────────────────
# 这几个变量缺一个，症状都是「编不过」或「跑起来找不到库」，报错还很难懂：
#   CC/CXX                必须指向真正的 C 编译器。本机 ~/.local/bin/gcc 是包装
#                         脚本，exec 的是 g++-13；cgo 编译 .c 会走 C++ 前端，
#                         在 -Werror 下直接致命失败。
#   FLOWGRID_DTK_SYSROOT  Qt6/DTK6 开发文件只存在于用户级 sysroot，系统目录没有。
#   LD_LIBRARY_PATH       跑 build 目录里的未安装产物时，动态链接器找不到
#                         libdtk6*.so / libQt6*.so。
#   QT_PLUGIN_PATH        同上，找 Qt 平台插件（xcb / offscreen）。
export CC=/usr/bin/gcc
export CXX=/usr/bin/g++
export PATH="${HOME}/sdk/go/bin:${PATH}"
export FLOWGRID_DTK_SYSROOT="${FLOWGRID_DTK_SYSROOT:-${HOME}/.local/dd-sysroot/usr}"
export LD_LIBRARY_PATH="${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu/qt6/plugins${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"

command -v cmake >/dev/null || die "找不到 cmake"
CMAKE="$(command -v cmake)"
[ -d "${FLOWGRID_DTK_SYSROOT}" ] || die "DTK/Qt6 sysroot 不存在：${FLOWGRID_DTK_SYSROOT}
  本机 Qt6/DTK6 开发文件只在这里，系统目录没有。可用 FLOWGRID_DTK_SYSROOT=<prefix> 覆盖。"

# ── 1. 编译 ────────────────────────────────────────────────────────────
build() {
    ok "配置 ${BUILD_TYPE}"
    "${CMAKE}" -S "${SRC_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    ok "编译（$(nproc 2>/dev/null || echo 4) 线程）"
    "${CMAKE}" --build "${BUILD_DIR}" -j"$(nproc 2>/dev/null || echo 4)"
}

if [ "${BUILD}" -eq 1 ]; then
    build
else
    [ -x "${BUILD_DIR}/${BIN_NAME}" ] || die "--skip-build 但 ${BUILD_DIR}/${BIN_NAME} 不存在"
    ok "沿用已有产物：${BUILD_DIR}/${BIN_NAME}"
fi
# 自检目标在 --check 模式下要用，缺了就早说，别等跑到那一步才炸。
if [ "${MODE}" = "check" ] && [ ! -x "${BUILD_DIR}/${TEST_NAME}" ]; then
    die "--check 需要 ${BUILD_DIR}/${TEST_NAME}，但它不存在（先不带 --skip-build 跑一次）"
fi

# ── 2. 快照 ────────────────────────────────────────────────────────────
# 无头渲染八屏 + 焦点样张 + 图标总览，落到 PNG 供人眼比对。
#
# 注意 --snapshot 给的是**文件前缀**（最终落成 <前缀>-<屏名>.png），不是目录。
# 落盘之后必须数一遍 PNG：少了就得报错，不能像以前那样「命令返回 0 就算过」。
# 快照目录清理：只删这一轮会重写的 PNG，不整目录 rm -rf。
# 理由：--snapshot 后面接的目录是用户给的，整目录删一旦参数手滑
# （--snapshot ~/Documents）就是一场事故。删 PNG 足够——每次快照的
# 屏名集合是固定的，重跑覆盖同名文件，不会留旧图干扰计数。
clean_theme_dir() {
    local d="$1"
    [ -d "${d}" ] || { mkdir -p "${d}"; return 0; }
    if [ -L "${d}" ]; then
        die "拒绝清理符号链接目录：${d}"
    fi
    find "${d}" -maxdepth 1 -type f -name '*.png' -delete
}

snapshot() {
    local theme="$1" dir="$2"
    mkdir -p "${dir}"
    local count=0
    if [ "${theme}" = "both" ]; then
        clean_theme_dir "${dir}/light"
        clean_theme_dir "${dir}/dark"
        # --theme 必须显式传：不传的话应用会去问宿主的 Appearance1，
        # 宿主是浅色时这张「深色快照」画出来的就是浅色，看着像快照坏了。
        QT_QPA_PLATFORM=offscreen "${BUILD_DIR}/${BIN_NAME}" --snapshot "${dir}/light/s" --theme light \
            >"${dir}/light.log" 2>&1
        QT_QPA_PLATFORM=offscreen "${BUILD_DIR}/${BIN_NAME}" --snapshot "${dir}/dark/s" --theme dark \
            >"${dir}/dark.log" 2>&1
        count=$(find "${dir}" -name '*.png' | wc -l)
        ok "浅色快照：${dir}/light/    日志：${dir}/light.log"
        ok "深色快照：${dir}/dark/    日志：${dir}/dark.log"
    else
        clean_theme_dir "${dir}/${theme}"
        QT_QPA_PLATFORM=offscreen "${BUILD_DIR}/${BIN_NAME}" --snapshot "${dir}/${theme}/s" --theme "${theme}" \
            >"${dir}/${theme}.log" 2>&1
        count=$(find "${dir}" -name '*.png' | wc -l)
        ok "${theme} 快照：${dir}/${theme}/    日志：${dir}/${theme}.log"
    fi
    # 八屏 + 两张全高 + hover + icons + focus + toast + files-polled，明暗各十余张。
    if [ "${count}" -lt 8 ]; then
        die "只落了 ${count} 张 PNG（预期至少 8 张），快照没成功。看上面的日志。"
    fi
    ok "落盘 ${count} 张 PNG"
}

# ── 3. 各模式 ──────────────────────────────────────────────────────────
case "${MODE}" in
    check)
        # 一次跑完三件事：界面行为（真实事件路径）、界面长相（明暗两套）、
        # 协议层只读（vet + 单测）。协议层只读——它有独立的问题台账
        # docs/PROTOCOL-INTEGRATION.md，不在 UI 这条线上改。
        ok "界面自检（真实控件 + 真实事件路径）"
        "${BUILD_DIR}/${TEST_NAME}"

        ok "无头快照"
        snapshot "${THEME}" "${SNAPSHOT_DIR}"

        ok "协议层只读校验（go vet + go test）"
        if command -v go >/dev/null; then
            ( cd "${REPO_ROOT}/protocol" && go vet ./... && go test -count=1 ./... )
        else
            note "PATH 里找不到 go，跳过协议层校验"
        fi

        printf '\n'
        ok "体检通过"
        note "协议层若报问题，写进 docs/PROTOCOL-ISSUES.md 台账，不要在 UI 这条线上顺手改"
        ;;

    snapshot)
        snapshot "${THEME}" "${SNAPSHOT_DIR}"
        ;;

    run)
        printf '\n'
        ok "开发模式启动：${BUILD_DIR}/${BIN_NAME}"
        note "Ctrl+C 停。托盘常驻、无主窗口——启动后不弹窗，点托盘图标才出 300px 浮层。"
        note "DConfig 不可用（meta 固定在 /usr/share/flowgrid/，要 root 才能装），"
        note "所以设置屏的改动只在本进程内有效；想要跨进程保留，先跑 ./deepin/install.sh。"
        exec "${BUILD_DIR}/${BIN_NAME}" "$@"
        ;;
esac
