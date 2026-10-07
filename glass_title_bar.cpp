#include "glass_title_bar.h"
#include "aero_widgets.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

GlassTitleBar::GlassTitleBar(QMainWindow *owner, QWidget *parent)
    : QWidget(parent ? parent : owner), m_owner(owner), m_logo(new QLabel(this)),
      m_title(new QLabel(this)), m_minimize(new JellyButton(QStringLiteral("−"), this)),
      m_maximize(new JellyButton({}, this)), m_close(new JellyButton({}, this)),
      m_accent(Aero::defaultAccent())
{
    setObjectName(QStringLiteral("glassTitleBar"));
    setFixedHeight(44);
    setAttribute(Qt::WA_TranslucentBackground); setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(13, 6, 9, 6); layout->setSpacing(8);
    m_logo->setFixedSize(24, 24);
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setMinimumWidth(0);
    m_title->setStyleSheet(QStringLiteral("color:#c9d8e8;font-size:12px;background:transparent;"));
    m_logo->setStyleSheet(QStringLiteral("background:transparent;"));
    layout->addWidget(m_logo); layout->addWidget(m_title, 1);
    m_minimize->setObjectName(QStringLiteral("windowMinimize"));
    m_maximize->setObjectName(QStringLiteral("windowMaximize"));
    m_close->setObjectName(QStringLiteral("windowClose"));
    m_minimize->setAccessibleName(QStringLiteral("最小化"));
    m_minimize->setToolTip(QStringLiteral("最小化"));
    m_close->setGlyph(JellyButton::Glyph::Close);
    m_close->setAccessibleName(QStringLiteral("关闭窗口"));
    m_close->setToolTip(QStringLiteral("关闭窗口"));
    for (JellyButton *button : {m_minimize, m_maximize, m_close}) {
        button->setFixedSize(38, 30); button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
    }
    connect(m_minimize, &QPushButton::clicked, this, [this] {
        if (m_owner) m_owner->showMinimized();
    });
    connect(m_maximize, &QPushButton::clicked, this, [this] { toggleMaximized(); });
    connect(m_close, &QPushButton::clicked, this, [this] {
        if (m_owner) m_owner->close();
    });
    if (owner) {
        owner->installEventFilter(this);
        m_title->setText(owner->windowTitle());
        m_title->setToolTip(owner->windowTitle());
        m_logo->setPixmap(owner->windowIcon().pixmap(QSize(24, 24), devicePixelRatioF()));
        connect(owner, &QWidget::windowTitleChanged, this, [this](const QString &title) {
            m_title->setText(title); m_title->setToolTip(title);
        });
        connect(owner, &QWidget::windowIconChanged, this, [this](const QIcon &icon) {
            m_logo->setPixmap(icon.pixmap(QSize(24, 24), devicePixelRatioF()));
        });
    }
    setAccentColor(m_accent); updateWindowState();
}

void GlassTitleBar::setAccentColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_accent = color;
    for (JellyButton *button : {m_minimize, m_maximize, m_close}) button->setAccentColor(color);
    update();
}

void GlassTitleBar::updateWindowState()
{
    const bool maximized = m_owner && m_owner->isMaximized();
    m_maximize->setText(maximized ? QStringLiteral("❐") : QStringLiteral("□"));
    const QString action = maximized ? QStringLiteral("还原窗口") : QStringLiteral("最大化");
    m_maximize->setToolTip(action); m_maximize->setAccessibleName(action);
}

void GlassTitleBar::toggleMaximized()
{
    if (!m_owner) return;
    if (m_owner->isMaximized()) m_owner->showNormal(); else m_owner->showMaximized();
    updateWindowState();
}

bool GlassTitleBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_owner && event->type() == QEvent::WindowStateChange) updateWindowState();
    return QWidget::eventFilter(watched, event);
}

void GlassTitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_owner) { QWidget::mousePressEvent(event); return; }
    QWindow *window = m_owner->windowHandle();
    if (!window || !window->startSystemMove()) {
        // Offscreen/non-native backends can reject native moves. The fallback
        // keeps a normal window movable without replacing the OS path on Windows.
        if (!m_owner->isMaximized()) {
            m_dragOffset = event->globalPosition().toPoint() - m_owner->frameGeometry().topLeft();
            m_fallbackDragging = true;
        }
    }
    event->accept();
}

void GlassTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_fallbackDragging && m_owner && (event->buttons() & Qt::LeftButton)) {
        m_owner->move(event->globalPosition().toPoint() - m_dragOffset); event->accept();
    } else QWidget::mouseMoveEvent(event);
}

void GlassTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_fallbackDragging = false; QWidget::mouseReleaseEvent(event);
}

void GlassTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_fallbackDragging = false; toggleMaximized(); event->accept();
    } else QWidget::mouseDoubleClickEvent(event);
}

void GlassTitleBar::paintEvent(QPaintEvent *)
{
    // Draw only a thin caption seam. The actual wallpaper/surface remains
    // visible through the bar; no screenshot, blur or animation timer is needed.
    QPainter painter(this);
    QLinearGradient seam(0, height() - 1, width(), height() - 1);
    seam.setColorAt(0, QColor(225, 240, 255, 0));
    seam.setColorAt(.35, QColor(225, 240, 255, 25));
    seam.setColorAt(.75, QColor(225, 240, 255, 16));
    seam.setColorAt(1, QColor(225, 240, 255, 0));
    painter.setPen(QPen(QBrush(seam), 1));
    painter.drawLine(12, height() - 1, width() - 12, height() - 1);
}
