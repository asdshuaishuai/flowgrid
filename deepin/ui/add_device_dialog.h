// Add-device wizard, follows design/flowgrid-mac-add-device.html:
// three-step indicator, scanning spinner, selectable device list, success page.
#ifndef FLOWGRID_ADD_DEVICE_DIALOG_H
#define FLOWGRID_ADD_DEVICE_DIALOG_H

#include <DDialog>
#include <DSpinner>
#include <QLabel>
#include <DProgressBar>
#include <QProgressBar>
#include <QShowEvent>
#include <QTimer>
#include <QVariantList>

#include "core_client.h"

class QStackedWidget;
class QVBoxLayout;

class AddDeviceDialog : public DTK_WIDGET_NAMESPACE::DDialog {
    Q_OBJECT

public:
    explicit AddDeviceDialog(CoreClient &core, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    QWidget *buildDiscoverPage();
    QWidget *buildSuccessPage();
    void rebuildCandidates();
    void enterConnectStep();

    CoreClient &m_core;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_step1 = nullptr;
    QLabel *m_step2 = nullptr;
    QLabel *m_step3 = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_successDetail = nullptr;
    DTK_WIDGET_NAMESPACE::DSpinner *m_spinner = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    DTK_WIDGET_NAMESPACE::DProgressBar *m_progress = nullptr;
    QTimer m_scanTimer;
    QWidget *m_detailHost = nullptr;
    QVBoxLayout *m_detailLayout = nullptr;
    QString m_selectedId;
    int m_connectButtonId = -1;
};

#endif // FLOWGRID_ADD_DEVICE_DIALOG_H
