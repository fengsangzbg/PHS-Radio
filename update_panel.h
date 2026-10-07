#pragma once

#include <QColor>
#include <QDialog>
#include <QPointer>
#include <functional>

class AeroSurface;
class AppUpdater;
class JellyButton;
class QLabel;
class QPlainTextEdit;
class QProgressBar;

class UpdatePanel final : public QDialog {
public:
    UpdatePanel(const QString &currentVersion, const QColor &accent,
                AeroSurface *surface, QWidget *parent = nullptr);
    ~UpdatePanel() override;
    std::function<void()> onInstallerStarted;

protected:
    void paintEvent(QPaintEvent *event) override;
    void reject() override;

private:
    enum class Action { Check, Download, None };
    void checkForUpdates();
    void setAction(Action action, const QString &text);
    AppUpdater *m_updater;
    QPointer<AeroSurface> m_surface;
    QColor m_accent;
    QLabel *m_status;
    QLabel *m_version;
    QPlainTextEdit *m_notes;
    QProgressBar *m_progress;
    JellyButton *m_action;
    JellyButton *m_close;
    Action m_nextAction = Action::Check;
    bool m_committed = false;
};
