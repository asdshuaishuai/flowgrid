#include "hotkeys.h"
#include "core/settings.h"

#include <QShortcut>
#include <QWidget>

namespace fg {
namespace {

// 修饰键跟着设置屏的「切换热键」走（1=Ctrl / 2=Alt）。托盘浮层里显示的键位
// 提示用的是同一个修饰键（core/settings.h 的 switchModifierName），这里注册
// 的和那里显示的必须是同一套。
Qt::KeyboardModifier hotkeyModifier()
{
    return Settings::instance().switchHotkey() == 2 ? Qt::AltModifier : Qt::ControlModifier;
}

} // namespace

TrayHotkeys::TrayHotkeys(QWidget *window, SwitchFunc onSwitch, ActionFunc onKeymap,
                         ActionFunc onSettings, ActionFunc onQuit)
    : onSwitch_(std::move(onSwitch)), onKeymap_(std::move(onKeymap)),
      onSettings_(std::move(onSettings)), onQuit_(std::move(onQuit))
{
    attachTo(window);
}

void TrayHotkeys::attachTo(QWidget *window)
{
    if (window == nullptr || window == host_) {
        return;
    }
    // 换窗口：旧的删掉重建。QShortcut 属于「它挂在哪个窗口」——挂在旧窗口上
    // 的快捷键，在新窗口里根本不会被查到。
    for (const Binding &b : bindings_) {
        delete b.sc;
    }
    bindings_.clear();
    host_ = window;

    // Ctrl+1/2/3：切到热键序号对应的那台设备。序号由托盘的快速切换按钮给出，
    // 那里显示的键位和这里是同一套。
    for (int slot = 1; slot <= 3; ++slot) {
        auto *sc = new QShortcut(host_);
        sc->setContext(Qt::ApplicationShortcut);
        const int s = slot;
        QObject::connect(sc, &QShortcut::activated, host_, [this, s] { onSwitch_(s); });
        bindings_ << Binding{sc, Qt::Key(Qt::Key_1 + s - 1)};
    }

    const auto add = [this](Qt::Key key, const ActionFunc &fn) {
        auto *sc = new QShortcut(host_);
        sc->setContext(Qt::ApplicationShortcut);
        QObject::connect(sc, &QShortcut::activated, host_, fn);
        bindings_ << Binding{sc, key};
    };
    add(Qt::Key_K, onKeymap_);
    add(Qt::Key_Comma, onSettings_);
    add(Qt::Key_Q, onQuit_);

    rebind();
}

void TrayHotkeys::rebind()
{
    for (Binding &b : bindings_) {
        b.sc->setKey(QKeySequence(hotkeyModifier() | b.key));
    }
}

QKeySequence TrayHotkeys::keyFor(Qt::Key key) const
{
    for (const Binding &b : bindings_) {
        if (b.key == key) {
            return b.sc->key();
        }
    }
    return QKeySequence();
}

} // namespace fg
