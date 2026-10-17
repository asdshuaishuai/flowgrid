// FlowGrid deepin (DDE 25) 客户端入口。
//
// 形态：托盘常驻，无主窗口。所有界面都是按需唤出的浮板（设计稿第一原则）。
// 因此本文件只做四件事：应用身份与单实例、宿主主题同步、托盘图标、浮板调度。
//
// deepin 专属要点（来自旧客户端 git d1e71e4 的实战结论，务必保留）：
//   1. 深色主题必须问 org.deepin.dde.Appearance1 的 GlobalTheme，
//      DGuiApplicationHelper::themeType() 在 DDE 25 上读不到它；
//   2. dde-dock 通过 StatusNotifierItem 提供托盘，点击图标要触发 Activate
//      才会走 QSystemTrayIcon::activated，所以不能设 ItemIsMenu（设了就只有菜单）。
#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/hosttheme.h"
#include "core/theme.h"
#include "ui/clipboardpanel.h"
#include "ui/connectionspanel.h"
#include "ui/filespanel.h"
#include "ui/keymappanel.h"
#include "ui/latencypanel.h"
#include "ui/pairingpanel.h"
#include "ui/settingspanel.h"
#include "ui/traypopup.h"
#include "ui/icons.h"
#include "ui/widgets.h"

#include <DApplication>
#include <DGuiApplicationHelper>
#include <DPlatformTheme>

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QCommandLineOption>
#include <QGridLayout>
#include <QCommandLineParser>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QLoggingCategory>
#include <QMimeData>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>

#include <cstdlib>

Q_LOGGING_CATEGORY(lcApp, "flowgrid.app")

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

using namespace fg;

namespace {

// 三块浮板常驻复用：反复 show/hide 比每次 new 少一次布局抖动，
// 也让折叠区里已采到的柱状图在关闭再打开后仍然保留。
TrayPopup *g_trayPopup = nullptr;
ConnectionsPanel *g_connections = nullptr;
LatencyPanel *g_latency = nullptr;
SettingsPanel *g_settings = nullptr;
KeymapPanel *g_keymap = nullptr;
PairingPanel *g_pairing = nullptr;
ClipboardPanel *g_clipboard = nullptr;
FilesPanel *g_files = nullptr;
QSystemTrayIcon *g_trayIcon = nullptr;

// 托盘图标几何：托盘面板以图标为中心、上方留 8px。拿不到图标几何时
// 退化到主屏右上角，保证功能不丢。
QPoint anchorPoint()
{
    if (g_trayIcon != nullptr && QSystemTrayIcon::isSystemTrayAvailable()) {
        const QRect geo = g_trayIcon->geometry();
        if (geo.isValid() && !geo.isEmpty()) {
            return QPoint(geo.center().x(), geo.top() - 8);
        }
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return QPoint(400, 200);
    }
    const QRect avail = screen->availableGeometry();
    return QPoint(avail.right() - 32, avail.top() + 64);
}

void showPanel(QWidget *panel)
{
    if (panel == nullptr) {
        return;
    }
    // 同一时刻只留一块浮板：设计稿里浮板是平级、按需唤出的，不是叠加的。
    if (g_trayPopup != nullptr && g_trayPopup != panel) {
        g_trayPopup->hide();
    }
    if (g_connections != nullptr && g_connections != panel) {
        g_connections->hide();
    }
    if (g_latency != nullptr && g_latency != panel) {
        g_latency->hide();
    }
    for (QWidget *other : {static_cast<QWidget *>(g_settings), static_cast<QWidget *>(g_keymap),
                           static_cast<QWidget *>(g_pairing), static_cast<QWidget *>(g_clipboard),
                           static_cast<QWidget *>(g_files)}) {
        if (other != nullptr && other != panel) {
            other->hide();
        }
    }
    if (auto *p = qobject_cast<Panel *>(panel)) {
        p->popupAt(anchorPoint());
    } else if (auto *t = qobject_cast<TrayPopup *>(panel)) {
        t->popupAt(anchorPoint());
    } else {
        placePopup(panel, anchorPoint());
        animatePopupIn(panel);
    }
}

// 尚未接通的入口统一提示，而不是静默无反应。
void notYetWired(const QString &what)
{
    qCInfo(lcApp) << "尚未接通:" << what;
}

// 「写入本地」与「接收后自动写入本地」的落点：把一条剪贴板历史写回本机系统
// 剪贴板。面板只负责发 localWriteRequested 并弹成功提示，真正的 QClipboard
// 写入放在 main（与 clearHistoryRequested 的接线方式一致）。载荷按收到的
// MIME 原样透传，不重新编码——收到什么就贴什么。
void writeClipboardEntryToLocal(const ClipEntry &entry)
{
    if (entry.raw.isEmpty()) {
        return;
    }
    auto *mime = new QMimeData;   // setMimeData 接管所有权
    mime->setData(entry.mime, entry.raw);
    // 文本类再按 text/plain 暴露一份：终端、单行输入框这类消费方只认
    // text/plain，不补这一下就粘不到。
    if (entry.mime.startsWith(QLatin1String("text/"))) {
        mime->setText(QString::fromUtf8(entry.raw));
    }
    QApplication::clipboard()->setMimeData(mime);
}

QIcon appIcon()
{
    QIcon i = QIcon::fromTheme(QStringLiteral("flowgrid"));
    if (i.isNull()) {
        i = QIcon(QStringLiteral(":/icons/flowgrid.svg"));
    }
    return i;
}

} // namespace

static QWidget *buildIconSheet();

int main(int argc, char *argv[])
{
    DApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("flowgrid"));
    app.setApplicationName(QStringLiteral("FlowGrid"));
    app.setApplicationDisplayName(QStringLiteral("FlowGrid"));
    app.setApplicationVersion(QStringLiteral(FLOWGRID_VERSION));
    app.setProductIcon(appIcon());
    // 托盘常驻：关掉最后一个窗口不等于退出进程。
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("FlowGrid — 跨平台键鼠共享（deepin / DDE 25 客户端）"));
    parser.addHelpOption();
    parser.addVersionOption();
    // 截图钩子：无头环境下无法靠肉眼验收，--snapshot 直接落 PNG。
    QCommandLineOption snapshotOpt(QStringLiteral("snapshot"),
                                   QStringLiteral("把当前浮板渲染到 <path>.png 后退出"),
                                   QStringLiteral("path"));
    parser.addOption(snapshotOpt);
    // 明暗强制开关：无头环境没有 DTK 平台主题插件，无法靠改设置中心验证两套配色，
    // 截图对比与 QA 都需要它。
    QCommandLineOption themeOpt(
        QStringLiteral("theme"),
        QStringLiteral("强制主题：auto（问 Appearance1）/ light / dark"),
        QStringLiteral("mode"), QStringLiteral("auto"));
    parser.addOption(themeOpt);
    parser.process(app);

    // 单实例：DTK6 的 setSingleInstance 返回 false 表示已有一份在跑。
    // 必须在创建任何长驻对象之前调用，否则会多起一套采样定时器。
    if (!app.setSingleInstance(QStringLiteral("flowgrid-deepin"))) {
        return 0;
    }

    // 主题：先定明暗，再让 Theme 解析具体颜色。顺序不能反——Theme 以
    // HostTheme 的判定为准（见 theme.cpp），HostTheme 晚于 Theme 初始化就会
    // 拿到一个错的 isDark。
    const QString forcedTheme = parser.value(themeOpt);
    if (forcedTheme == QLatin1String("light") || forcedTheme == QLatin1String("dark")) {
        HostTheme::instance().setForced(forcedTheme == QLatin1String("dark"), true);
    }
    HostTheme::instance().probe();   // 内部已订阅 Appearance1 的 PropertiesChanged
    Theme::instance().refresh();

    auto &manager = DeviceManager::instance();
    manager.setSource(makeLocalSampleSource(&app));

    g_trayPopup = new TrayPopup;
    g_connections = new ConnectionsPanel;
    g_latency = new LatencyPanel;
    g_settings = new SettingsPanel;
    g_keymap = new KeymapPanel;
    g_pairing = new PairingPanel;
    g_clipboard = new ClipboardPanel;
    g_files = new FilesPanel;

    // context 用 sender 自己而非 &app：本 TU 里 DApplication 到 const QObject*
    // 的隐式转换不成立，而连接本来也该随浮板一起消失。
    QObject::connect(g_trayPopup, &TrayPopup::openConnections, g_trayPopup,
                     [] { showPanel(g_connections); });
    QObject::connect(g_trayPopup, &TrayPopup::openLatency, g_trayPopup, [] { showPanel(g_latency); });
    QObject::connect(g_trayPopup, &TrayPopup::openPairing, g_trayPopup,
                     [] { showPanel(g_pairing); });
    QObject::connect(g_trayPopup, &TrayPopup::openKeymap, g_trayPopup,
                     [] { showPanel(g_keymap); });
    QObject::connect(g_trayPopup, &TrayPopup::openSettings, g_trayPopup,
                     [] { showPanel(g_settings); });
    QObject::connect(g_trayPopup, &TrayPopup::openClipboard, g_trayPopup,
                     [] { g_clipboard->reloadHistory(); showPanel(g_clipboard); });
    QObject::connect(g_trayPopup, &TrayPopup::openFiles, g_trayPopup,
                     [] { showPanel(g_files); });
    QObject::connect(g_trayPopup, &TrayPopup::quitRequested, g_trayPopup, [] { std::exit(0); });

    // 拖放区的「点击选择」：这半条链路是完整的（QFileDialog → FilesPanel::offerFile
    // → Go 的 FileSender），不需要等传输层。
    QObject::connect(g_files, &FilesPanel::fileChosen, g_files, [] {
        const QString path = QFileDialog::getOpenFileName(
            g_files, QObject::tr("选择要发送的文件"), QDir::homePath());
        if (!path.isEmpty()) {
            g_files->offerFile(path);
        }
    });
    // 跨屏跳转一律交给 main：浮板互斥逻辑只有一处，避免各处各 hide 一遍。
    QObject::connect(g_connections, &ConnectionsPanel::settingsRequested, g_connections,
                     [] { showPanel(g_settings); });
    // 连接屏顶栏的「添加设备」与托盘那一行是同一个入口，都进配对浮板。此前这里
    // 漏了接线，而按钮内部用的是 QMetaObject::invokeMethod(this, "addRequested")
    // ——按字符串找方法，编不过编译器，那个方法也从没声明过，点下去只有一条
    // 运行时警告，界面毫无反应。
    QObject::connect(g_connections, &ConnectionsPanel::addRequested, g_connections,
                     [] { showPanel(g_pairing); });
    QObject::connect(g_settings, &SettingsPanel::keymapRequested, g_settings,
                     [] { showPanel(g_keymap); });
    QObject::connect(g_connections, &ConnectionsPanel::keymapRequested, g_connections,
                     [] { showPanel(g_keymap); });
    QObject::connect(g_connections, &ConnectionsPanel::latencyRequested, g_connections,
                     [] { showPanel(g_latency); });
    QObject::connect(g_connections, &ConnectionsPanel::clipboardRequested, g_connections,
                     [] { g_clipboard->reloadHistory(); showPanel(g_clipboard); });
    QObject::connect(g_connections, &ConnectionsPanel::filesRequested, g_connections,
                     [] { showPanel(g_files); });
    // 连接屏的剪贴板 / 文件传输折叠区只读对应浮板的状态，不另开会话。
    g_connections->attachPanels(g_clipboard, g_files);
    // 托盘那两个徽标同样只读别人的数据：谁变了就推一次，不在托盘里另存一份。
    auto syncTrayCounts = [tray = g_trayPopup, clip = g_clipboard, files = g_files] {
        tray->setCounts(clip->historyCount(), files->activeCount());
    };
    QObject::connect(g_clipboard, &ClipboardPanel::transfersChanged, g_trayPopup,
                     [syncTrayCounts] { syncTrayCounts(); });
    QObject::connect(g_files, &FilesPanel::transfersChanged, g_trayPopup,
                     [syncTrayCounts] { syncTrayCounts(); });
    QObject::connect(g_pairing, &PairingPanel::doneRequested, g_pairing,
                     [] { showPanel(g_connections); });
    QObject::connect(g_clipboard, &ClipboardPanel::clearHistoryRequested, g_clipboard, [] {
        // 历史目前只活在 ClipboardPanel 进程内，清完就真没了；等接上持久化后
        // 这里要改成删盘上的那份，这里先明确记一笔而不是悄悄无事发生。
        qCInfo(lcApp) << "已清空剪贴板同步历史（当前仅内存副本）";
    });
    // 点「写入本地」/ 历史条目的复制按钮，以及「接收后自动写入本地」，都从这里
    // 真正落到系统剪贴板。此前这个信号没人接，面板却已经弹了「已写入本地剪贴板」
    // ——弹了成功提示却什么都没发生。
    QObject::connect(g_clipboard, &ClipboardPanel::localWriteRequested, g_clipboard,
                     [](const ClipEntry &e) { writeClipboardEntryToLocal(e); });
    // 下面两处要等传输层 / 键位映射编辑器落地，先明确告诉用户而不是点了没反应。
    QObject::connect(g_pairing, &PairingPanel::pairRequested, g_pairing,
                     [](const QString &name) { notYetWired(QObject::tr("向 %1 发起配对").arg(name)); });
    QObject::connect(g_keymap, &KeymapPanel::editRequested, g_keymap,
                     [] { notYetWired(QObject::tr("键位映射规则编辑")); });

    // 浮板内容随采样实时刷新
    QObject::connect(&manager, &DeviceManager::devicesChanged, g_trayPopup, &TrayPopup::refresh);
    QObject::connect(&manager, &DeviceManager::sampled, g_trayPopup, &TrayPopup::refresh);

    g_trayIcon = new QSystemTrayIcon(appIcon(), &app);
    g_trayIcon->setToolTip(QStringLiteral("FlowGrid"));
    // 不设 ItemIsMenu：dde-dock 对菜单型条目只会弹菜单，而设计稿要的是
    // 自绘的 300px 浮层，必须走 Activate 路径。
    g_trayIcon->setProperty("Title", QStringLiteral("FlowGrid"));
    g_trayIcon->setProperty("Id", QStringLiteral("flowgrid-deepin"));
    QObject::connect(g_trayIcon, &QSystemTrayIcon::activated, g_trayIcon,
            [](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) {
                    if (g_trayPopup != nullptr && g_trayPopup->isVisible()) {
                        g_trayPopup->hide();
                    } else {
                        showPanel(g_trayPopup);
                    }
                }
            });

    const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    if (trayAvailable) {
        g_trayIcon->show();
    } else {
        qCWarning(lcApp) << "系统托盘不可用，托盘浮层无法唤出";
    }

    // 托盘底部那几行快捷键提示（Ctrl+1/2/3 切设备、Ctrl+K 键位映射、Ctrl+,
    // 设置、Ctrl+Q 退出）是设计稿 ui.css .tray__kbd 的一部分，界面照设计留着；
    // 但客户端一处全局快捷键都还没注册，这些提示目前按下去不会有反应。
    // 全局热键要 X11 抓键或独立热键服务，不在 UI 范围内，先把边界写清楚，
    // 别让「界面上写着 Ctrl+Q」被当成已经能用。
    qCInfo(lcApp) << "托盘快捷键提示为设计稿占位，尚未注册全局热键（Ctrl+1/2/3、K、,、Q 均未生效）";

    // 截图模式：依次渲染八块界面并退出，供无头环境验收（文件传输屏多出一张
    // 轮询后的画面，理由见下面那张 shot）。
    if (parser.isSet(snapshotOpt)) {
        const QString base = parser.value(snapshotOpt);
        QTimer::singleShot(600, &app, [&app, base, syncTrayCounts] {
            // 走真实的 pop-up 定位（showPanel 内部会算 anchorPoint），而不是手调
            // 几何：截图与实际唤出同一条路径，否则「截图好看、实际错位」这类问题
            // 在无头环境里根本查不出来。
            const auto shot = [&base](QWidget *w, const QString &suffix) {
                if (w == nullptr) {
                    return;
                }
                w->setAttribute(Qt::WA_DontShowOnScreen, true);
                showPanel(w);
                w->ensurePolished();
                const QString path =
                    suffix.isEmpty() ? base : QStringLiteral("%1-%2.png").arg(base, suffix);
                w->grab().save(path);
                qCInfo(lcApp) << "snapshot ->" << path
                              << w->width() << "x" << w->height();

                // 真实唤出会被 placePopup 按「屏幕可用高度 - 48」夹断（对应设计稿
                // tokens.css 的 --panel-max-h: calc(100vh - 48px)），所以上面这张
                // 只看得到折叠线以上。设置屏、连接屏内容都超过一屏，光靠它没法验收
                // 末行有没有对齐——这里再出一张不夹屏、按内容全高的图。
                const int natural = w->sizeHint().height();
                const int clamped = w->height();
                if (natural > clamped) {
                    w->resize(w->width(), natural);
                    w->ensurePolished();
                    const QString full =
                        QStringLiteral("%1-%2-full.png").arg(base, suffix);
                    w->grab().save(full);
                    qCInfo(lcApp) << "snapshot ->" << full
                                  << w->width() << "x" << w->height();
                    w->resize(w->width(), clamped);   // 复位，免得污染下一张
                }
                w->hide();
            };
            // 连接屏的文件传输折叠区读的是 FilesPanel 的快照，而 FilesPanel
            // 在下面才播种——顺序反了这一区会拍成「暂无传输」。
            g_files->seedForSnapshot();
            g_clipboard->seedForSnapshot();
            syncTrayCounts();
            shot(g_trayPopup, QStringLiteral("tray"));
            shot(g_connections, QStringLiteral("connections"));
            shot(g_latency, QStringLiteral("latency"));
            shot(g_settings, QStringLiteral("settings"));
            shot(g_keymap, QStringLiteral("keymap"));
            shot(g_pairing, QStringLiteral("pairing"));
            shot(g_clipboard, QStringLiteral("clipboard"));
            shot(g_files, QStringLiteral("files"));

            // 图标总览：图标集是一整套造型语言，靠逐屏截图是审不完的——
            // 一屏里往往只出现三五枚，其余的错了也没人看得见。这里把所有图标
            // 按 16 / 13 / 10px 三个实际使用尺寸排成一张，明暗各出一张。
            if (QWidget *sheet = buildIconSheet()) {
                sheet->setAttribute(Qt::WA_DontShowOnScreen, true);
                sheet->ensurePolished();
                const QString sheetPath = QStringLiteral("%1-icons.png").arg(base);
                sheet->grab().save(sheetPath);
                qCInfo(lcApp) << "snapshot ->" << sheetPath
                              << sheet->width() << "x" << sheet->height();
                sheet->deleteLater();
            }

            // Toast 是独立顶层窗，上面的 shot(g_*) 全都抓不到它。它自己出过一次
            // 「药丸被阴影内缩压没」的版（底色等于没画、文字被裁半截），浮板截图
            // 又照不到，所以单独出一张。
            toast(QStringLiteral("已写入本地剪贴板"), ToastKind::Ok);
            // base 必须按值捕获：外层 singleShot 返回后它就析构了，按引用捕获
            // 会拼出乱码文件名（顺带在下一轮跑出 std::bad_alloc）。
            QTimer::singleShot(tokens::kDurToast + 120, &app, [&app, base] {
                if (QWidget *t = activeToast(); t != nullptr) {
                    t->ensurePolished();
                    const QString toastPath =
                        QStringLiteral("%1-toast.png").arg(base);
                    t->grab().save(toastPath);
                    qCInfo(lcApp) << "snapshot ->" << toastPath
                                  << t->width() << "x" << t->height();
                }
                // 文件传输屏再拍一张：它的会话轮询每 500ms 就按 Go 侧报的数字改写
                // 已播种的条目（进度、终态、大小）。只拍唤出瞬间那一张的话，
                // 「轮询把状态改坏」这类问题在无头环境里根本看不见——实测漏过
                // 「空闲轮询把那条 4.2 GB 的记录刷成 0 B」。
                QTimer::singleShot(700, &app, [&app, base] {
                    if (QWidget *w = g_files) {
                        w->setAttribute(Qt::WA_DontShowOnScreen, true);
                        showPanel(w);
                        w->ensurePolished();
                        const QString p = QStringLiteral("%1-files-polled.png").arg(base);
                        w->grab().save(p);
                        qCInfo(lcApp) << "snapshot ->" << p
                                      << w->width() << "x" << w->height();
                    }
                    app.quit();
                });
            });
        });
    } else {
        // 托盘常驻但首屏不主动弹浮层：设计稿的入口只有托盘图标。
        QTimer::singleShot(0, &app, [&] {
            if (!trayAvailable) {
                // 没有托盘就无处唤起，直接把主入口摆出来，总比打不开强。
                showPanel(g_trayPopup);
            }
        });
    }

    return app.exec();
}

// 图标总览图：每行一枚图标，同一枚按 16 / 13 / 10px 排三遍，外加名字。
// 三个尺寸都取自真实调用点——列表行首 16、折叠区头 16、键位映射 13、页脚 14、
// 空态 12、提示条 14 的下限落在 10 附近，再小就只剩形状没有细节了。
static QWidget *buildIconSheet()
{
    const fg::Palette &p = fg::Theme::instance().palette();
    auto *sheet = new QWidget;
    // 必须是不透明底：透明 widget 上 grab 出来的文字会被次像素抗锯齿糊成
    // 满屏横条（第一版就把整张总览图毁在这上面）。
    sheet->setAutoFillBackground(true);
    QPalette qp = sheet->palette();
    qp.setColor(QPalette::Window, p.surface);
    qp.setColor(QPalette::WindowText, p.text2);
    sheet->setPalette(qp);

    auto *root = new QVBoxLayout(sheet);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);

    const QStringList list = fg::icons::names();
    constexpr int kCols = 6;
    for (int i = 0; i < list.size(); i += kCols) {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        for (int c = 0; c < kCols; ++c) {
            const int idx = i + c;
            if (idx >= list.size()) {
                break;
            }
            auto *cell = new QWidget(sheet);
            auto *col = new QVBoxLayout(cell);
            col->setContentsMargins(0, 0, 0, 0);
            col->setSpacing(2);
            auto *icons = new QHBoxLayout;
            icons->setSpacing(6);
            icons->setContentsMargins(0, 0, 0, 0);
            for (int s : {16, 13, 10}) {
                auto *box = new QWidget(cell);
                auto *bl = new QHBoxLayout(box);
                bl->setContentsMargins(0, 0, 0, 0);
                bl->setAlignment(Qt::AlignCenter);
                auto *lab = new QLabel(box);
                lab->setFixedSize(s, s);
                lab->setPixmap(fg::icons::icon(list.at(idx), s, p.text2).pixmap(s, s));
                bl->addWidget(lab);
                icons->addWidget(box);
            }
            col->addLayout(icons);
            auto *name = new QLabel(list.at(idx), cell);
            QFont nf = QApplication::font();
            nf.setPixelSize(10);
            name->setFont(nf);
            {
                QColor c = p.text3;
                name->setStyleSheet(QStringLiteral("color:%1")
                                        .arg(c.alpha() == 255 ? c.name(QColor::HexRgb)
                                                              : c.name(QColor::HexArgb)));
            }
            name->setAlignment(Qt::AlignCenter);
            col->addWidget(name);
            row->addWidget(cell);
        }
        root->addLayout(row);
    }
    return sheet;
}
