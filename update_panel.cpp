#include "update_panel.h"
#include "app_updater.h"
#include "aero_surface.h"
#include "aero_widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

UpdatePanel::UpdatePanel(const QString &currentVersion, const QColor &accent,
                         AeroSurface *surface, QWidget *parent)
    : QDialog(parent), m_updater(new AppUpdater(this, currentVersion)),
      m_surface(surface), m_accent(accent), m_status(new QLabel(this)),
      m_version(new QLabel(this)), m_notes(new QPlainTextEdit(this)),
      m_progress(new QProgressBar(this)),
      m_action(new JellyButton(QStringLiteral("检查更新"), this)),
      m_close(new JellyButton(QStringLiteral("关闭"), this))
{
    setObjectName(QStringLiteral("updatePanel"));
    setWindowTitle(QStringLiteral("软件更新"));
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    resize(580, 430);
    setMinimumWidth(420);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    auto *heading = new QLabel(QStringLiteral("PHS Radio · 软件更新"), this);
    heading->setStyleSheet(QStringLiteral("font-size:20px;font-weight:600;"));
    layout->addWidget(heading);
    m_version->setText(QStringLiteral("当前版本 v%1").arg(currentVersion));
    layout->addWidget(m_version);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setAccessibleName(QStringLiteral("软件更新状态"));
    layout->addWidget(m_status);
    m_notes->setObjectName(QStringLiteral("updateNotes"));
    m_notes->setReadOnly(true);
    m_notes->setMaximumHeight(180);
    m_notes->hide();
    layout->addWidget(m_notes, 1);
    m_progress->setObjectName(QStringLiteral("updateProgress"));
    m_progress->setRange(0, 0);
    m_progress->hide();
    layout->addWidget(m_progress);
    auto *hint = new QLabel(QStringLiteral("点击“下载并更新”后，软件将在安装完成后自动重启。登录、歌单与主题设置会保留。"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#a5b8ca;font-size:12px;"));
    layout->addWidget(hint);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(m_close);
    actions->addWidget(m_action);
    layout->addLayout(actions);
    for (JellyButton *button : {m_action, m_close}) button->setAccentColor(accent);
    m_action->setObjectName(QStringLiteral("updateAction"));
    m_close->setObjectName(QStringLiteral("updateClose"));
    setStyleSheet(QStringLiteral(
        "QDialog#updatePanel { background:transparent; }"
        "QLabel { background:transparent;color:#e9f2fb; }"
        "QPlainTextEdit { color:#d3e2ef;background:rgba(8,15,24,70);border:1px solid rgba(220,240,255,35);border-radius:12px;padding:9px;font-size:12px; }"
        "QProgressBar { color:#e9f2fb;background:rgba(220,240,255,18);border:1px solid rgba(220,240,255,35);border-radius:7px;height:16px;text-align:center; }"
        "QProgressBar::chunk { background:rgba(%1,%2,%3,170);border-radius:6px; }")
        .arg(accent.red()).arg(accent.green()).arg(accent.blue()));
    if (m_surface) m_surface->registerBackgroundConsumer(this);
    connect(m_close, &QPushButton::clicked, this, &UpdatePanel::reject);
    connect(m_action, &QPushButton::clicked, this, [this] {
        if (m_nextAction == Action::Check) checkForUpdates();
        else if (m_nextAction == Action::Download) {
            setAction(Action::None, QStringLiteral("正在下载…"));
            m_progress->setRange(0, 0);
            m_progress->show();
            m_updater->downloadUpdate();
        }
    });
    connect(m_updater, &AppUpdater::statusChanged, this, [this](const QString &status) { m_status->setText(status); });
    connect(m_updater, &AppUpdater::progress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_progress->setRange(0, 100);
            m_progress->setValue(int(std::clamp<qint64>(received * 100 / total, 0, 100)));
        }
    });
    connect(m_updater, &AppUpdater::updateAvailable, this, [this, currentVersion](const QString &version, const QString &notes) {
        m_version->setText(QStringLiteral("当前 v%1 · 新版本 v%2").arg(currentVersion, version));
        m_status->setText(QStringLiteral("发现新版本，可以下载并更新。"));
        m_notes->setPlainText(notes.trimmed().isEmpty() ? QStringLiteral("此版本没有提供更新说明。") : notes);
        m_notes->show();
        setAction(Action::Download, QStringLiteral("下载并更新"));
    });
    connect(m_updater, &AppUpdater::upToDate, this, [this] {
        m_status->setText(QStringLiteral("已经是最新版本。"));
        m_progress->hide();
        setAction(Action::Check, QStringLiteral("重新检查"));
    });
    connect(m_updater, &AppUpdater::error, this, [this](const QString &error) {
        m_status->setText(error);
        m_progress->hide();
        setAction(Action::Check, QStringLiteral("重试"));
    });
    connect(m_updater, &AppUpdater::updateReady, this, [this](const QString &) {
        setAction(Action::None, QStringLiteral("正在准备更新…"));
        m_progress->setRange(0, 0);
        QTimer::singleShot(0, this, [this] { m_updater->launchInstaller(); });
    });
    connect(m_updater, &AppUpdater::installerStarted, this, [this] {
        m_committed = true;
        m_close->setEnabled(false);
        m_status->setText(QStringLiteral("更新已准备好，正在退出并重启…"));
        QDialog::accept();
        if (onInstallerStarted) onInstallerStarted();
    });
    QTimer::singleShot(0, this, [this] { checkForUpdates(); });
}

UpdatePanel::~UpdatePanel() { if (!m_committed) m_updater->cancel(); }

void UpdatePanel::checkForUpdates()
{
    if (m_committed) return;
    m_notes->hide();
    m_progress->hide();
    setAction(Action::None, QStringLiteral("正在检查…"));
    m_updater->checkForUpdates();
}

void UpdatePanel::setAction(Action action, const QString &text)
{
    m_nextAction = action;
    m_action->setText(text);
    m_action->setEnabled(action != Action::None);
}

void UpdatePanel::reject()
{
    if (m_committed) return;
    m_updater->cancel();
    QDialog::reject();
}

void UpdatePanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = QRectF(rect()).adjusted(2, 2, -2, -2);
    QPainterPath clip;
    clip.addRoundedRect(bounds, 24, 24);
    painter.save();
    painter.setClipPath(clip);
    if (m_surface) {
        const QPoint offset = m_surface->mapFromGlobal(mapToGlobal(QPoint(0, 0)));
        m_surface->paintBlurredBackground(painter, bounds, offset);
    }
    painter.fillPath(clip, QColor(7, 14, 23, 210));
    painter.restore();
    Aero::paintGlass(painter, bounds, m_accent, 24);
}
