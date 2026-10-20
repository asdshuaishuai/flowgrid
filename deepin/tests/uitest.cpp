// 界面自检：无头环境里跑，走真实控件与真实事件路径。
//
// 快照能看的是「长什么样」，看不出「按下去有没有反应」。这里补的就是后者：
//   · 托盘快捷键（Ctrl+K / Ctrl+, / Ctrl+1..3）按下去真的触发对应动作；
//   · 托盘上显示的键位提示，和实际注册的键位是同一套（不显示按不动的键）；
//   · Esc 真的把浮层收回去；
//   · 焦点环真的画出来了（按键边框上真的多了一圈主色像素）。
//
// 关键做法：键事件必须标成「自发」（QEvent::setSpontaneous），否则 Qt 不会
// 走快捷键表——真实键盘事件本来就是自发的，合成事件不标就等于伪造。
// 另外 offscreen 平台不会自动激活窗口，要手动 setActiveWindow + setFocus，
// 否则没有焦点部件，快捷键表根本不会被查。
//
// 验不了的（真实桌面的托盘唤出、深色主题、Wayland 全局热键）写在 README 里，
// 不在这里假装验过。
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "ui/hotkeys.h"
#include "ui/traypopup.h"
#include "ui/widgets.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPixmap>
#include <QShortcut>
#include <QWidget>

#include <DApplication>
#include <dconfigfile.h>

#include <cstdio>

#if defined(FLOWGRID_HAVE_QTTEST)
// QEvent::setSpontaneous() 在 Qt 6 里是私有的，公开的转发头在 QtTest 模块
// （纯头文件，不链接任何库）。没有它就没法造自发键事件，自检降级。
#include <QtTest/qtestspontaneevent.h>
#endif

DWIDGET_USE_NAMESPACE

namespace {

int g_failures = 0;

void check(bool ok, const QString &what)
{
    if (ok) {
        std::printf("  [ok]   %s\n", what.toUtf8().constData());
    } else {
        std::printf("  [FAIL] %s\n", what.toUtf8().constData());
        ++g_failures;
    }
}

// 送一个自发键事件：Qt 收到自发键事件才会去查快捷键表。合成事件不标自发，
// 就会被当成程序内部投递的事件，绕开快捷键表。
bool pressKey(QWidget *target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
#if defined(FLOWGRID_HAVE_QTTEST)
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QSpontaneKeyEvent::setSpontaneous(&press);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QSpontaneKeyEvent::setSpontaneous(&release);
    QApplication::sendEvent(target, &release);
    return true;
#else
    Q_UNUSED(target);
    Q_UNUSED(key);
    Q_UNUSED(mods);
    return false;
#endif
}

// 让窗口拿到焦点：offscreen 平台不会自动激活窗口。
void focusOn(QWidget *w)
{
    // setActiveWindow 在 6.8 起被标记为过时，但 offscreen 平台下只有它能把
    // 应用的活动窗口指出来（activateWindow 在这个平台上不起作用），而没有
    // 活动窗口就没有焦点部件，Qt 根本不会去查快捷键表。
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(w);
    QT_WARNING_POP
    w->setFocus();
}

// ── DConfig meta 自检 ─────────────────────────────────────────────────
// 三件事：
//   1. 装的**位置**跟 DTK 期望的位置一致（问 DTK 自己要，别靠记）；
//   2. meta 是 **DSG 格式**（magic/version/contents）——旧 dconfig 那种
//      fileName/keys 数组，DDE 25 的 dconfig 直接拒收（"The meta magic does
//      not match"）；
//   3. 应用管理的每个键在 meta 里都有——少一个就是「读写静默失效」。
//
// 这三件事在真实桌面上的表现都是同一个：设置屏的开关看着能点，关掉进程就没了。
// 不报错，所以只能靠自检拦。
void checkDConfigMeta()
{
    const QString metaPath = QStringLiteral(FLOWGRID_META_FILE);
    const QString appId = QStringLiteral(FLOWGRID_DCONFIG_APPID);
    const QString name = QStringLiteral(FLOWGRID_DCONFIG_NAME);

    // 1. 位置：DTK 认的规范目录 vs 我们装的目标。
    //    规范目录是 /usr/share/dsg/configs/<appId>；但 /usr 在 ostree 机器上是
    //    **只读 overlay**，那个目录建不出来，install.sh 会改用 /usr/local/share/dsg
    //    并把那个根注册进守护进程的 DSG_DATA_DIRS。这种情况下两者当然不同，
    //    所以这里只硬查「布局形状」对不对，根的差异打印出来说明。
    const QString canonical = QStringLiteral(FLOWGRID_DCONFIG_CANONICAL_DIR);
    const QString installDir = QStringLiteral(FLOWGRID_META_DIR);
    const QStringList dtkDirs =
        Dtk::Core::DConfigMeta::applicationMetaDirs(QString(), appId);
    check(dtkDirs.value(0) == canonical,
          QStringLiteral("DTK 认的规范 meta 目录是 %1").arg(canonical));

    const QString suffix = QStringLiteral("/configs/") + appId;
    check(installDir.endsWith(suffix),
          QStringLiteral("安装目标是 <根>/configs/%1 的布局（%2）").arg(appId, installDir));
    // meta 文件名必须正好是 <name>.json —— 目录对了名字不对，守护进程照样找不到
    check(QFileInfo(metaPath).fileName() == name + QStringLiteral(".json"),
          QStringLiteral("meta 文件名是 %1.json").arg(name));
    if (installDir != dtkDirs.value(0)) {
        std::printf("  [注意] 安装根不是 DTK 的规范根（/usr/share/dsg）："
                    "这台机器 /usr 只读，改用了 %s；\n"
                    "         该根必须出现在 dconfig 守护进程的 DSG_DATA_DIRS 里，"
                    "否则守护进程扫不到（install.sh 负责注册）。\n",
                    installDir.toUtf8().constData());
    } else {
        check(true, QStringLiteral("安装目标就是 DTK 的规范根"));
    }

    // 2/3. 格式与键表
    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly)) {
        check(false, QStringLiteral("读得到 meta 文件 %1").arg(metaPath));
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        check(false, QStringLiteral("meta 是合法 JSON：%1").arg(err.errorString()));
        return;
    }
    const QJsonObject root = doc.object();
    check(root.value(QStringLiteral("magic")).toString()
              == QStringLiteral("dsg.config.meta"),
          QStringLiteral("meta 是 DSG 格式（magic = dsg.config.meta）"));
    check(root.contains(QStringLiteral("version")), QStringLiteral("meta 有 version 字段"));
    check(root.contains(QStringLiteral("contents")), QStringLiteral("meta 有 contents 字段"));

    const QJsonObject contents = root.value(QStringLiteral("contents")).toObject();
    check(!contents.isEmpty(), QStringLiteral("contents 非空（%1 个键）").arg(contents.size()));

    // 应用管的每个键都得在 meta 里
    const QStringList managed = fg::Settings::managedKeys();
    QStringList missing;
    for (const QString &key : managed) {
        if (!contents.contains(key)) {
            missing << key;
        }
    }
    check(missing.isEmpty(),
          missing.isEmpty()
              ? QStringLiteral("应用管理的 %1 个键在 meta 里全都有").arg(managed.size())
              : QStringLiteral("meta 缺这些键：%1").arg(missing.join(", ")));

    // meta 里有但应用不管的键：不致命，但说明键表漂了，得看一眼
    QStringList extra;
    for (auto it = contents.constBegin(); it != contents.constEnd(); ++it) {
        if (!managed.contains(it.key())) {
            extra << it.key();
        }
    }
    if (!extra.isEmpty()) {
        std::printf("  [注意] meta 里有应用不管理的键：%s\n",
                    extra.join(", ").toUtf8().constData());
    }
}

} // namespace

int main(int argc, char **argv)
{
    // offscreen：不起真窗口，不在桌面上弹东西。
    qputenv("QT_QPA_PLATFORM", "offscreen");
    DApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("flowgrid"));
    app.setApplicationName(QStringLiteral("FlowGrid"));
    app.setQuitOnLastWindowClosed(false);

    std::printf("FlowGrid 界面自检\n");

    // ── 1. 托盘快捷键 ────────────────────────────────────────────────
    std::printf("托盘快捷键\n");
    int switchedSlot = 0;
    bool keymapOpened = false;
    bool settingsOpened = false;
    bool quitCalled = false;
    // 托盘浮层：真实控件，焦点落在它里面——快捷键要在「浮层开着」时才按得动。
    auto *tray = new fg::TrayPopup;
    tray->setAttribute(Qt::WA_DontShowOnScreen, true);
    tray->show();
    focusOn(tray);

    // 快捷键挂在托盘浮层下面：Qt 的快捷键表按窗口分，挂在别的窗口上，
    // 在这里按是没反应的。
    fg::TrayHotkeys hotkeys(
        tray,
        [&switchedSlot](int slot) { switchedSlot = slot; },
        [&keymapOpened] { keymapOpened = true; },
        [&settingsOpened] { settingsOpened = true; },
        [&quitCalled] { quitCalled = true; });

    const auto mod = fg::Settings::instance().switchHotkey() == 2 ? Qt::AltModifier
                                                                  : Qt::ControlModifier;
    check(hotkeys.keyFor(Qt::Key_K) == QKeySequence(mod | Qt::Key_K),
          QStringLiteral("Ctrl+K 绑定的是「打开键位映射」"));
    check(hotkeys.keyFor(Qt::Key_Comma) == QKeySequence(mod | Qt::Key_Comma),
          QStringLiteral("Ctrl+, 绑定的是「打开设置」"));
    check(hotkeys.keyFor(Qt::Key_Q) == QKeySequence(mod | Qt::Key_Q),
          QStringLiteral("Ctrl+Q 绑定的是「退出」"));
    check(hotkeys.keyFor(Qt::Key_1) == QKeySequence(mod | Qt::Key_1),
          QStringLiteral("Ctrl+1 绑定的是「切到 1 号热键设备」"));

    // 派发检查需要自发键事件；没有 QtTest 头时只核对绑定，如实说明跳过。
    const bool canDispatch = pressKey(tray, Qt::Key_K, mod);
    if (canDispatch) {
        check(keymapOpened, QStringLiteral("按 Ctrl+K 打开键位映射"));

        pressKey(tray, Qt::Key_Comma, mod);
        check(settingsOpened, QStringLiteral("按 Ctrl+, 打开设置"));

        pressKey(tray, Qt::Key_2, mod);
        check(switchedSlot == 2, QStringLiteral("按 Ctrl+2 切到 2 号热键设备"));

        pressKey(tray, Qt::Key_Q, mod);
        check(quitCalled, QStringLiteral("按 Ctrl+Q 退出"));

        // 没有修饰键的裸按键不该触发（否则在输入框里打 K 就会跳走）。
        keymapOpened = false;
        pressKey(tray, Qt::Key_K);
        check(!keymapOpened, QStringLiteral("裸 K 不触发快捷键"));
    } else {
        std::printf("  [跳过] 快捷键派发检查：缺 QtTest 头，无法造自发键事件\n");
    }

    // ── 2. Esc 收起浮层 ──────────────────────────────────────────────
    std::printf("Esc 收起浮层\n");
    check(tray->isVisible(), QStringLiteral("托盘浮层此刻是可见的"));
    pressKey(tray, Qt::Key_Escape);
    check(!tray->isVisible(), QStringLiteral("按 Esc 收起托盘浮层"));

    // ── 3. 焦点环真的画出来 ──────────────────────────────────────────
    std::printf("焦点环\n");
    const fg::Palette &p = fg::Theme::instance().palette();
    // 次要按钮：实色（主色）按钮的焦点环用的是 onSolid（白），验主色环要用
    // 底色浅的那种。
    auto *button = new fg::FgButton(QStringLiteral("取消"), fg::FgButton::Variant::Secondary);
    button->setAttribute(Qt::WA_DontShowOnScreen, true);
    button->show();

    const auto accentPixels = [&p](const QPixmap &pm) {
        int n = 0;
        const QImage img = pm.toImage();
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (std::abs(c.red() - p.accent.red()) < 24 && std::abs(c.green() - p.accent.green()) < 24
                    && std::abs(c.blue() - p.accent.blue()) < 24) {
                    ++n;
                }
            }
        }
        return n;
    };

    // 不能要求「未聚焦时主色像素数为 0」：文字是次像素（LCD）抗锯齿画的，
    // 笔画边缘带彩色边纹，偶尔会撞上主色的容差。改成看增量——焦点环是整圈
    // 2px 的主色，增量是几十上百个像素量级，跟边纹不是一个数量级。
    const int before = accentPixels(button->grab());
    // 先聚焦再补一发 focusIn：offscreen 平台不一定把焦点变化派发到位。
    focusOn(button);
    QFocusEvent focusIn(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(button, &focusIn);
    const int after = accentPixels(button->grab());
    std::printf("  （主色像素 %d -> %d）\n", before, after);
    check(after >= before + 20, QStringLiteral("按钮获得焦点后画出了主色焦点环"));

    // ── 4. DConfig meta：路径、格式、键表 ──────────────────────────────
    // 这一段守的是「设置静默失效」那个坑。meta 装错位置或用了旧 dconfig 格式，
    // dconfig 都不报错，只是当这个配置不存在——设置屏的开关看着能点，关掉进程
    // 就没了。装到哪儿、什么格式，在这里问 DTK 自己，别靠记。
    std::printf("DConfig meta\n");
    checkDConfigMeta();

    std::printf(g_failures == 0 ? "\n全部通过\n" : "\n有 %d 项未通过\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
