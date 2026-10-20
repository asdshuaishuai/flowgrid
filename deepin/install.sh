#!/usr/bin/env bash
#
# FlowGrid deepin (DDE 25) 客户端 —— 系统级安装脚本。
#
# 为什么必须系统级（不能只装到 ~/.local）：
#   DConfig（DSG）按固定约定定位 meta 文件，即
#   /usr/share/dsg/configs/<appId>/<name>.json，
#   本工程是 /usr/share/dsg/configs/flowgrid/flowgrid-deepin.json。
#   它不搜 ~/.local/share，也不搜 /usr/local/share。
#   装错位置的表现不是报错，而是 create() 返回不可用对象、设置全部静默回退
#   默认值——设置屏的开关还能点、看起来是好的，但关掉进程就没了。
#   所以下面最后会实打实校验这个文件在不在。
#
# 另外，meta 文件本身必须是 DSG 格式（顶层 magic/version/contents）。旧 dconfig
# 那种 fileName/keys 数组的写法，DDE 25 的 dconfig 直接拒收（"The meta magic
# does not match"），同样表现为设置静默失效。
#
# 用法：
#   ./deepin/install.sh                 # 配置 + 编译 + 安装（需要 sudo 装系统位置）
#   ./deepin/install.sh --skip-build    # 只安装，沿用已编译好的产物
#   ./deepin/install.sh --launch        # 装完直接拉起
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/deepin"
BUILD_DIR="${SRC_DIR}/build"
BIN_NAME="flowgrid-deepin"
# 与 src/core/settings.cpp 的 DConfig::create(appId, name) 和
# CMakeLists.txt 的 install(FILES ...) 三处必须一致。
#
# 这个路径是 DTK 自己给的，不是猜的：DConfigMeta::applicationMetaDirs("", "flowgrid")
# 返回的就是 /usr/share/dsg/configs/flowgrid，meta 文件名是 <name>.json。
# dconfig 守护进程只扫 /usr/share/dsg 与 /var/lib/linglong/entries/share/dsg
# （见 dde-dconfig-daemon.service 的 DSG_DATA_DIRS），装到 /usr/share/flowgrid/
# 或 /usr/local/share/ 下它都看不见。
DCONFIG_META="/usr/share/dsg/configs/flowgrid/flowgrid-deepin.json"

BUILD=1
LAUNCH=0

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
        --launch)     LAUNCH=1 ;;
        -h|--help)    usage ;;
        *)            die "未知参数：$1（--help 看用法）" ;;
    esac
    shift
done

# ── 0. 前置条件 ────────────────────────────────────────────────────────
command -v cmake >/dev/null || die "找不到 cmake"
CMAKE="$(command -v cmake)"

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null || die "需要 root：装 /usr/local 和 ${DCONFIG_META}"
    SUDO="sudo"          # 故意不在这里校验密码，交互时提示一次即可
fi

# 这两项直接决定 cgo 与 Qt/DTK 能不能编过，缺一个都会让后面的报错难懂十倍。
export CC=/usr/bin/gcc
export CXX=/usr/bin/g++
export PATH="${HOME}/sdk/go/bin:${PATH}"
export FLOWGRID_DTK_SYSROOT="${FLOWGRID_DTK_SYSROOT:-${HOME}/.local/dd-sysroot/usr}"
[ -d "${FLOWGRID_DTK_SYSROOT}" ] || die "DTK/Qt6 sysroot 不存在：${FLOWGRID_DTK_SYSROOT}
  本机 Qt6/DTK6 开发文件只在这里，系统目录没有。可用 FLOWGRID_DTK_SYSROOT=<prefix> 覆盖。"
command -v go >/dev/null || die "PATH 里找不到 go（已加 ${HOME}/sdk/go/bin）"

# ── 1. 编译 ────────────────────────────────────────────────────────────
if [ "${BUILD}" -eq 1 ]; then
    ok "配置 Release"
    "${CMAKE}" -S "${SRC_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
    ok "编译（$(nproc 2>/dev/null || echo 4) 线程）"
    "${CMAKE}" --build "${BUILD_DIR}" -j"$(nproc 2>/dev/null || echo 4)"
else
    [ -x "${BUILD_DIR}/${BIN_NAME}" ] || die "--skip-build 但 ${BUILD_DIR}/${BIN_NAME} 不存在"
    ok "沿用已有产物：${BUILD_DIR}/${BIN_NAME}"
fi

# ── 2. 安装 ────────────────────────────────────────────────────────────
# cmake --install 会按 CMakeLists 的 install() 规则落这几处：
#   /usr/local/bin/flowgrid-deepin
#   /usr/local/share/applications/flowgrid-deepin.desktop
#   /usr/local/share/icons/hicolor/scalable/apps/flowgrid.svg
#   /usr/share/dsg/configs/flowgrid/flowgrid-deepin.json  ← 绝对路径，dconfig 只扫这儿
#   /etc/xdg/autostart/flowgrid-deepin-autostart.desktop   ← 绝对路径，自启项只能在这
ok "安装到系统位置（会提示输入 sudo 密码）"
${SUDO} "${CMAKE}" --install "${BUILD_DIR}"

# ── 3. 清掉此前装到 ~/.local 的那份 ───────────────────────────────────
# 这些副本都是「dconfig 看不见」的位置，留着只会让人误以为 meta 装好了
# （尤其是 /usr/local/share/flowgrid/ 那个：文件确实在，dconfig 却扫不到）。
# 放在安装成功之后：万一是 sudo 失败被打断，先删就等于两头都没了。
#
# 家目录那几份是自己的文件，用普通 rm——套 sudo 会白要一次密码。
for stale in \
    "${HOME}/.local/bin/${BIN_NAME}" \
    "${HOME}/.local/share/applications/${BIN_NAME}.desktop" \
    "${HOME}/.local/share/flowgrid/${BIN_NAME}.json" \
    "${HOME}/.config/autostart/${BIN_NAME}-autostart.desktop"
do
    if [ -e "$stale" ]; then
        rm -f "$stale" && note "移除用户级残留：$stale"
    fi
done
# 系统位置那几个是 root 的，要 sudo。
for stale in \
    "/usr/local/share/flowgrid/${BIN_NAME}.json" \
    "/usr/share/flowgrid/${BIN_NAME}.json"
do
    if [ -e "$stale" ]; then
        ${SUDO} rm -f "$stale" && note "移除装错位置的残留：$stale"
    fi
done
# 装错位置时留下的空目录也一并清掉
for stale_dir in "/usr/local/share/flowgrid" "/usr/share/flowgrid"; do
    if [ -d "$stale_dir" ] && [ -z "$(ls -A "$stale_dir" 2>/dev/null)" ]; then
        ${SUDO} rmdir "$stale_dir" && note "移除空目录：$stale_dir"
    fi
done

# ── 4. 刷新桌面缓存 ────────────────────────────────────────────────────
if command -v update-desktop-database >/dev/null; then
    ${SUDO} update-desktop-database /usr/local/share/applications 2>/dev/null || true
    ok "刷新 .desktop 缓存"
else
    note "没有 update-desktop-database，菜单项可能要等桌面环境自己扫一遍"
fi
if command -v gtk-update-icon-cache >/dev/null; then
    ${SUDO} gtk-update-icon-cache -f -t /usr/local/share/icons/hicolor 2>/dev/null || true
    ok "刷新图标缓存"
fi

# ── 5. 校验：这几条任何一条不成立，装了也是坏的 ────────────────────────
[ -x "/usr/local/bin/${BIN_NAME}" ] || die "可执行文件没落到位"
ok "可执行文件：/usr/local/bin/${BIN_NAME}"

if [ -f "${DCONFIG_META}" ]; then
    ok "DConfig meta：${DCONFIG_META}"
    # 还得验格式：路径对了但格式是旧的 dconfig 那套，dconfig 照样拒收
    # （"The meta magic does not match"），表现同样是设置静默失效。
    if grep -q '"magic"[[:space:]]*:[[:space:]]*"dsg.config.meta"' "${DCONFIG_META}" 2>/dev/null; then
        ok "meta 格式：DSG（magic/version/contents）"
    else
        die "DConfig meta 的格式不对（缺少 \"magic\": \"dsg.config.meta\"）
  DDE 25 的 dconfig 只认 DSG 格式：顶层 magic/version/contents 映射。
  旧 dconfig 那种 fileName/keys 数组的写法会被直接拒收（\"The meta magic does not match\"），
  表现同样是设置静默回退默认值。检查 deepin/resources/flowgrid-deepin.json。"
    fi
else
    die "DConfig meta 不在 ${DCONFIG_META}
  设置会静默回退默认值（不报错，最难查的一种坏法）。
  检查 src/core/settings.cpp 的 DConfig::create() 与 CMakeLists.txt 的
  install(FILES resources/flowgrid-deepin.json ...) 的 appId/name 是否一致。
  正确位置由 DTK 自己给：DConfigMeta::applicationMetaDirs(\"\", \"flowgrid\")"
fi

[ -f "/etc/xdg/autostart/${BIN_NAME}-autostart.desktop" ] \
    && ok "自启项：/etc/xdg/autostart/${BIN_NAME}-autostart.desktop" \
    || note "自启项没装上，设置屏的「开机自启」开关将无处可写"

printf '\n'
ok "装好了。从应用菜单搜「FlowGrid」启动，或："
printf '    %s\n' "/usr/local/bin/${BIN_NAME}"
printf '    %s\n' "/usr/local/bin/${BIN_NAME} --snapshot /tmp/fgshot/s   # 无头渲染 8 屏验收"
printf '\n'
note "托盘常驻、无主窗口：启动后不弹窗，点托盘图标才出 300px 浮层。"
note "要确认 DConfig 真的通了，启动日志里不该出现「DConfig meta」。"

# ── 6. 可选：直接拉起 ──────────────────────────────────────────────────
if [ "${LAUNCH}" -eq 1 ]; then
    ok "启动"
    DISPLAY="${DISPLAY:-:0}" /usr/local/bin/${BIN_NAME} >/tmp/flowgrid-deepin.log 2>&1 &
    sleep 2
    if pgrep -x "${BIN_NAME}" >/dev/null; then
        ok "已在运行，日志：/tmp/flowgrid-deepin.log"
        if grep -q "DConfig meta" /tmp/flowgrid-deepin.log 2>/dev/null; then
            note "日志里仍有「DConfig meta …」——meta 装上了但没被 dconfig 认到（位置或格式不对）"
        fi
    else
        note "没能保持运行，看 /tmp/flowgrid-deepin.log"
    fi
fi
