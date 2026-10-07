#include "playlist_drawer.h"
#include "aero_widgets.h"
#include "liquid_backdrop.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

PlaylistDrawer::PlaylistDrawer(QWidget *host)
    : QWidget(host), m_host(host), m_contentLayout(new QVBoxLayout(this)),
      m_slide(new QPropertyAnimation(this, "geometry", this)),
      m_pointerTimer(new QTimer(this)), m_leaveTimer(new QTimer(this)), m_accent(Aero::defaultAccent())
{
    Q_ASSERT(host);
    setObjectName(QStringLiteral("playlistDrawerPanel"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    updateGlassStyles();
    m_contentLayout->setContentsMargins(20, 20, 20, 20);
    m_contentLayout->setSpacing(12);
    m_contentLayout->setSizeConstraint(QLayout::SetNoConstraint);
    m_slide->setDuration(180);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QPropertyAnimation::finished, this, [this] {
        if (!m_expanded)
            hide();
    });
    m_leaveTimer->setSingleShot(true);
    m_leaveTimer->setInterval(500);
    connect(m_leaveTimer, &QTimer::timeout, this, [this] {
        if (m_expanded && !m_pinned && !hasPanelFocus())
            setExpanded(false);
    });
    m_pointerTimer->setInterval(75);
    connect(m_pointerTimer, &QTimer::timeout, this, [this] { pollPointer(); });
    m_host->installEventFilter(this);
    setGeometry(hiddenPanelRect());
    hide();
    m_pointerTimer->start();
}

QVBoxLayout *PlaylistDrawer::contentLayout() const
{
    return m_contentLayout;
}

bool PlaylistDrawer::isExpanded() const
{
    return m_expanded;
}

bool PlaylistDrawer::isPinned() const
{
    return m_pinned;
}

void PlaylistDrawer::setExpanded(bool expanded)
{
    m_leaveTimer->stop();
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    if (!expanded && hasPanelFocus())
        QApplication::focusWidget()->clearFocus();
    animateToTarget();
}

void PlaylistDrawer::setPinned(bool pinned)
{
    m_pinned = pinned;
    m_leaveTimer->stop();
    if (pinned)
        setExpanded(true);
}

void PlaylistDrawer::toggle()
{
    setExpanded(!m_expanded);
}

void PlaylistDrawer::setTopInset(int pixels)
{
    m_topInset = std::max(10, pixels);
    adjustToHost();
}

void PlaylistDrawer::setAccentColor(const QColor &accent)
{
    m_accent = accent.isValid() ? accent : Aero::defaultAccent();
    updateGlassStyles();
    update();
}

QColor PlaylistDrawer::accentColor() const
{
    return m_accent;
}

void PlaylistDrawer::updateGlassStyles()
{
    setStyleSheet(QStringLiteral(
        "QWidget { background: transparent; color: #eaf3fa; }"
        "QWidget#playlistDrawerPanel { border: 0; }"
        "QLineEdit, QComboBox { background: rgba(9,14,23,52); color: #eaf3fa;"
        " border: 1px solid rgba(255,255,255,42); border-radius: 11px; padding: 9px; }"
        "QLineEdit:focus { border-color: %1; background: rgba(255,255,255,15); }"
        "QComboBox QAbstractItemView { background: #1b232d; color: #eaf3fa; }"
        "QListWidget { background: transparent; border: 0; outline: 0; color: #eaf3fa; }"
        "QListWidget::item { padding: 9px 8px; border-radius: 10px; }"
        "QListWidget::item { background: rgba(255,255,255,6); border: 1px solid rgba(255,255,255,16); }"
        "QListWidget::item:hover { background: rgba(255,255,255,13); border-color: rgba(255,255,255,40); }"
        "QListWidget::item:selected { background: rgba(255,255,255,22); border-color: rgba(255,255,255,76); color: #f5fbff; }"
        "QPushButton { background: rgba(255,255,255,11); border: 1px solid rgba(255,255,255,34);"
        " border-radius: 11px; padding: 7px 10px; color: #eaf3fa; }"
        "QPushButton:hover { background: rgba(255,255,255,20); }"
        "QPushButton:checked { background: rgba(255,255,255,27); border-color: %1; color: #f5fbff; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(234,243,250,63);"
        " border-radius: 3px; min-height: 26px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(m_accent.name(QColor::HexRgb)));
}

QRect PlaylistDrawer::targetPanelRect() const
{
    const int top = std::min(m_topInset, std::max(10, m_host->height() - 100));
    return QRect(10, top, std::min(380, std::max(1, m_host->width() - 20)),
                 std::max(1, m_host->height() - top - 10));
}

QRect PlaylistDrawer::hiddenPanelRect() const
{
    QRect panel = targetPanelRect();
    panel.moveLeft(-panel.width() - 12);
    return panel;
}

bool PlaylistDrawer::hasPanelFocus() const
{
    const QWidget *focused = QApplication::focusWidget();
    if (!focused || !isAncestorOf(focused))
        return false;
    // Lists and buttons retain focus after a click; only active text editing keeps the drawer open.
    if (const auto *input = qobject_cast<const QLineEdit *>(focused))
        return !input->isReadOnly();
    if (const auto *input = qobject_cast<const QPlainTextEdit *>(focused))
        return !input->isReadOnly();
    if (const auto *input = qobject_cast<const QTextEdit *>(focused))
        return !input->isReadOnly();
    if (const auto *input = qobject_cast<const QAbstractSpinBox *>(focused))
        return !input->isReadOnly();
    if (const auto *input = qobject_cast<const QComboBox *>(focused))
        return input->isEditable();
    return false;
}

void PlaylistDrawer::animateToTarget()
{
    m_slide->stop();
    const QRect destination = m_expanded ? targetPanelRect() : hiddenPanelRect();
    if (m_expanded) {
        show();
        raise();
    }
    m_slide->setStartValue(geometry());
    m_slide->setEndValue(destination);
    m_slide->start();
}

void PlaylistDrawer::adjustToHost()
{
    const bool moving = m_slide->state() == QAbstractAnimation::Running;
    m_slide->stop();
    const QRect target = m_expanded ? targetPanelRect() : hiddenPanelRect();
    if (moving) {
        QRect current = geometry();
        current.setSize(target.size());
        current.moveTop(target.top());
        current.moveLeft(std::clamp(current.left(), hiddenPanelRect().left(), targetPanelRect().left()));
        setGeometry(current);
        animateToTarget();
    } else {
        setGeometry(target);
        if (m_expanded)
            raise();
    }
}

void PlaylistDrawer::updatePointer(const QPoint &hostPosition, bool insideWindow)
{
    const bool insideHost = insideWindow && m_host->rect().contains(hostPosition);
    const bool atEdge = insideHost && hostPosition.x() <= 16;
    // Use the expanded destination while the panel is sliding into place.
    const bool overPanel = insideWindow && targetPanelRect().contains(hostPosition);
    if (m_expanded && overPanel)
        update(); // Follow the pointer with the glass highlight without moving the hit area.
    if (atEdge) {
        setExpanded(true);
        return;
    }
    if (!m_expanded || m_pinned || overPanel || hasPanelFocus()) {
        m_leaveTimer->stop();
        return;
    }
    if (!m_leaveTimer->isActive())
        m_leaveTimer->start();
}

void PlaylistDrawer::pollPointer()
{
    if (!m_host->isVisible())
        return;
    QWidget *hostWindow = m_host->window();
    if (!hostWindow->isActiveWindow()) {
        m_leaveTimer->stop();
        if (!m_pinned)
            setExpanded(false);
        return;
    }
    const QPoint globalPosition = QCursor::pos();
    updatePointer(m_host->mapFromGlobal(globalPosition),
                  hostWindow->rect().contains(hostWindow->mapFromGlobal(globalPosition)));
}

bool PlaylistDrawer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            adjustToHost();
        else if (event->type() == QEvent::Hide) {
            m_leaveTimer->stop();
            if (!m_pinned)
                setExpanded(false);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlaylistDrawer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    const QRectF panel = QRectF(rect()).adjusted(6, 6, -6, -6);
    Liquid::paintBackdrop(painter, this, 24);
    Aero::paintGlass(painter, panel, m_accent, 24);
}
