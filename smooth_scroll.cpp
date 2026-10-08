#include "smooth_scroll.h"
#include "aero_widgets.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

SmoothScrollController *SmoothScrollController::attach(QAbstractItemView *view)
{
    if (!view)
        return nullptr;
    for (QObject *child : view->children())
        if (auto *controller = dynamic_cast<SmoothScrollController *>(child))
            return controller;
    return new SmoothScrollController(view);
}

SmoothScrollController::SmoothScrollController(QAbstractItemView *view)
    : QObject(view), m_view(view), m_viewport(view->viewport()), m_window(view->window()),
      m_scrollbar(view->verticalScrollBar())
{
    setObjectName(QStringLiteral("smoothScrollController"));
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->installEventFilter(this);
    m_viewport->installEventFilter(this);
    m_scrollbar->installEventFilter(this);
    if (m_window)
        m_window->installEventFilter(this);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(true);
    retime();
    m_refreshObserver = Aero::observeDisplayRefresh(view, this, [this] { retime(); });
    connect(&m_timer, &QTimer::timeout, this, [this] { advance(); });
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        // Dragging, keyboard navigation, scrollTo(), and application changes
        // belong to the native view. An old wheel target must not pull them back.
        if (!m_applying)
            cancel();
    });
    connect(view->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { cancel(); });
    connect(view->verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] { cancel(); });
    observeModel();
    cancel();
}

SmoothScrollController::~SmoothScrollController()
{
    delete m_refreshObserver;
    m_refreshObserver = nullptr;
    m_timer.stop();
    if (m_viewport)
        m_viewport->removeEventFilter(this);
    if (m_view) {
        m_view->removeEventFilter(this);
    }
    if (m_window)
        m_window->removeEventFilter(this);
    if (m_scrollbar)
        m_scrollbar->removeEventFilter(this);
}

bool SmoothScrollController::isAnimating() const { return m_running; }
qreal SmoothScrollController::targetValue() const { return m_target; }
qreal SmoothScrollController::velocity() const { return m_velocity; }

void SmoothScrollController::observeFrames(QObject *context, std::function<void(bool, bool)> frame)
{
    if (context && frame)
        m_frameObservers.append({context, std::move(frame)});
}

void SmoothScrollController::notifyFrame(bool continuing, bool cancelled)
{
    // Callbacks may destroy their receiver (or the view). Copy the bounded
    // observer list so removing a widget never invalidates this traversal.
    m_frameObservers.erase(std::remove_if(m_frameObservers.begin(), m_frameObservers.end(),
        [](const FrameObserver &observer) { return !observer.context; }), m_frameObservers.end());
    const auto observers = m_frameObservers;
    const QPointer<SmoothScrollController> guard(this);
    for (const auto &observer : observers) {
        if (observer.context)
            observer.frame(continuing, cancelled);
        if (!guard)
            return;
    }
}

void SmoothScrollController::cancel()
{
    const bool wasRunning = m_running;
    m_timer.stop();
    m_running = false;
    m_velocity = 0;
    if (m_scrollbar) {
        m_position = m_target = m_scrollbar->value();
        m_lastAppliedValue = m_scrollbar->value();
        m_minimum = m_scrollbar->minimum();
        m_maximum = m_scrollbar->maximum();
    }
    if (wasRunning)
        notifyFrame(false, true);
}

void SmoothScrollController::observeModel()
{
    if (!m_view || !dynamic_cast<QAbstractItemView *>(static_cast<QWidget *>(m_view))
        || m_model == m_view->model())
        return;
    const QPointer<SmoothScrollController> guard(this);
    cancel();
    if (!guard)
        return;
    for (const auto &connection : m_modelConnections)
        disconnect(connection);
    m_modelConnections.clear();
    m_model = m_view->model();
    if (!m_model)
        return;
    const auto stop = [this] { cancel(); };
    m_modelConnections.append(connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, stop));
    m_modelConnections.append(connect(m_model, &QAbstractItemModel::layoutAboutToBeChanged, this, stop));
    m_modelConnections.append(connect(m_model, &QAbstractItemModel::rowsAboutToBeInserted, this, stop));
    m_modelConnections.append(connect(m_model, &QAbstractItemModel::rowsAboutToBeRemoved, this, stop));
    m_modelConnections.append(connect(m_model, &QObject::destroyed, this, [this] {
        m_timer.stop();
        m_running = false;
        m_velocity = 0;
        m_model = nullptr;
    }));
}

void SmoothScrollController::retime()
{
    const qint64 now = m_clock.isValid() ? m_clock.nsecsElapsed() : 0;
    const bool scheduled = m_timer.isActive();
    m_schedule.setRefreshRate(Aero::displayRefreshRate(m_view), now);
    if (scheduled && m_running)
        m_timer.start(m_schedule.delayMs(now));
}

bool SmoothScrollController::wheel(QWheelEvent *event)
{
    if (!m_view || !m_scrollbar || !m_view->isEnabled() || m_scrollbar->isSliderDown())
        return false;
    const QPointer<SmoothScrollController> guard(this);
    observeModel();
    if (!guard)
        return true;
    const QPoint pixels = event->pixelDelta();
    const QPoint angles = event->angleDelta();
    if (event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::ShiftModifier)
        || (!pixels.isNull() && std::abs(pixels.x()) > std::abs(pixels.y()))
        || (pixels.isNull() && std::abs(angles.x()) > std::abs(angles.y()))) {
        cancel();
        return false;
    }
    QScrollBar *bar = m_scrollbar;
    if (!pixels.isNull()) {
        // The platform already supplies natural-scroll direction and momentum.
        // Applying another inertial spring here would lag or double the gesture.
        cancel();
        if (!guard)
            return true;
        const int destination = std::clamp(bar->value() - pixels.y(), bar->minimum(), bar->maximum());
        if (destination == bar->value())
            return false;
        m_position = m_target = destination;
        applyPosition();
        event->accept();
        return true;
    }
    if (!angles.y()) {
        if (event->phase() == Qt::ScrollBegin)
            cancel();
        return false;
    }
    if (!m_running) {
        cancel();
        if (!guard)
            return true;
    }
    const qreal distance = -angles.y() / 120.0
        * QApplication::wheelScrollLines() * std::max(1, bar->singleStep());
    const qreal destination = std::clamp(m_target + distance, qreal(bar->minimum()), qreal(bar->maximum()));
    if (qFuzzyCompare(destination + 1, m_target + 1)) {
        if (m_running) {
            event->accept();
            return true;
        }
        return false;
    }
    if ((destination - m_position) * m_velocity < 0)
        m_velocity *= .25;
    m_target = destination;
    retime();
    if (!m_running) {
        m_running = true;
        m_clock.start();
        m_previousTickNs = 0;
        m_schedule.reset(0);
        m_timer.start(m_schedule.delayMs(0));
    }
    event->accept();
    return true;
}

void SmoothScrollController::applyPosition()
{
    if (!m_scrollbar)
        return;
    QScrollBar *bar = m_scrollbar;
    const int destination = std::clamp(qRound(m_position), bar->minimum(), bar->maximum());
    m_applying = true;
    m_lastAppliedValue = destination;
    const QPointer<SmoothScrollController> guard(this);
    bar->setValue(destination);
    if (!guard)
        return;
    m_applying = false;
}

void SmoothScrollController::advance()
{
    if (!m_view || !m_scrollbar || !dynamic_cast<QAbstractItemView *>(static_cast<QWidget *>(m_view))
        || !m_view->isVisible()) {
        cancel();
        return;
    }
    const QPointer<SmoothScrollController> entryGuard(this);
    observeModel();
    if (!entryGuard)
        return;
    if (!m_running)
        return;
    QScrollBar *bar = m_scrollbar;
    if (bar->value() != m_lastAppliedValue || bar->minimum() != m_minimum || bar->maximum() != m_maximum) {
        // Application rebuilds sometimes block scrollbar signals. Detect those
        // external changes too, before writing the next animated position.
        cancel();
        return;
    }
    retime();
    const qint64 now = m_clock.nsecsElapsed();
    if (!m_schedule.advance(now)) {
        m_timer.start(m_schedule.delayMs(now));
        return;
    }
    const qreal dt = std::max<qreal>(0, (now - m_previousTickNs) / 1000000000.0);
    m_previousTickNs = now;
    // Exact critically damped spring integration remains stable at any refresh
    // rate or after a delayed GUI frame; each new notch only changes the target.
    // A long spring felt like input lag and kept expensive transparent cards
    // animating after each notch. Reach the target promptly while preserving
    // intermediate positions and velocity across consecutive wheel events.
    constexpr qreal omega = 45.0;
    const qreal displacement = m_position - m_target;
    const qreal blend = (m_velocity + omega * displacement) * dt;
    const qreal decay = std::exp(-omega * dt);
    m_position = m_target + (displacement + blend) * decay;
    m_velocity = (m_velocity - omega * blend) * decay;
    m_position = std::clamp(m_position, qreal(bar->minimum()), qreal(bar->maximum()));
    if ((m_position <= bar->minimum() && m_velocity < 0)
        || (m_position >= bar->maximum() && m_velocity > 0))
        m_velocity = 0;
    const bool settled = std::abs(m_position - m_target) < .3 && std::abs(m_velocity) < 4;
    if (settled)
        m_position = m_target;
    const QPointer<SmoothScrollController> guard(this);
    applyPosition();
    if (!guard)
        return;
    if (settled) {
        m_timer.stop();
        m_running = false;
        m_velocity = 0;
    } else if (m_running) {
        m_timer.start(m_schedule.delayMs(m_clock.nsecsElapsed()));
    }
    notifyFrame(m_running);
    // QAbstractItemView scrolls/repaints only its visible viewport itself. No
    // extra full update, row walk or per-song widget is needed here.
}

bool SmoothScrollController::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || !dynamic_cast<QAbstractItemView *>(static_cast<QWidget *>(m_view)))
        return false;
    if (watched == m_viewport && event->type() == QEvent::Wheel)
        return wheel(static_cast<QWheelEvent *>(event));
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
    case QEvent::Resize:
        cancel();
        break;
    case QEvent::Show:
    case QEvent::ParentChange:
    case QEvent::LayoutRequest:
        if (m_window != m_view->window()) {
            if (m_window)
                m_window->removeEventFilter(this);
            m_window = m_view->window();
            if (m_window)
                m_window->installEventFilter(this);
        }
        observeModel();
        break;
    case QEvent::DevicePixelRatioChange:
        retime();
        break;
    default:
        break;
    }
    return false;
}
