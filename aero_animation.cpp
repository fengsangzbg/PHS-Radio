#include "aero_animation.h"
#include "aero_widgets.h"

#include <QAnimationDriver>
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace Aero {

qreal boundedRefreshRate(qreal refreshRate)
{
    return std::isfinite(refreshRate) && refreshRate > 0
        ? std::clamp(refreshRate, 30.0, 360.0) : 60.0;
}

qreal displayRefreshRate(const QWidget *widget)
{
    const QScreen *screen = widget ? widget->screen() : QGuiApplication::primaryScreen();
    return boundedRefreshRate(screen ? screen->refreshRate() : 60.0);
}

int animationInterval(const QWidget *widget)
{
    // Legacy callers use an integer delay. Precise frame producers use the
    // varying FrameSchedule delay below instead of permanently rounding Hz.
    return std::max(1, int(std::ceil(1000.0 / displayRefreshRate(widget))));
}

FrameSchedule::FrameSchedule(qreal refreshRate)
{
    m_refreshRate = boundedRefreshRate(refreshRate);
    m_periodNs = qRound64(1000000000.0 / m_refreshRate);
}

void FrameSchedule::setRefreshRate(qreal refreshRate, qint64 nowNs)
{
    refreshRate = boundedRefreshRate(refreshRate);
    if (qFuzzyCompare(m_refreshRate, refreshRate))
        return;
    m_refreshRate = refreshRate;
    m_periodNs = qRound64(1000000000.0 / m_refreshRate);
    reset(nowNs);
}

void FrameSchedule::reset(qint64 nowNs) { m_nextFrameNs = nowNs + m_periodNs; }

bool FrameSchedule::advance(qint64 nowNs)
{
    if (nowNs < m_nextFrameNs)
        return false;
    // A late GUI frame must leave room for input and painting. Keeping the
    // old phase after a stall can put another deadline only 1 ms away and
    // immediately queue more work on an already overloaded event loop.
    if (nowNs - m_nextFrameNs >= m_periodNs / 2)
        m_nextFrameNs = nowNs + m_periodNs;
    else
        m_nextFrameNs += m_periodNs;
    return true;
}

int FrameSchedule::delayMs(qint64 nowNs) const
{
    const qint64 remaining = std::max<qint64>(1, m_nextFrameNs - nowNs);
    return int(std::max<qint64>(1, (remaining + 999999) / 1000000));
}

namespace {

class DisplayRefreshObserver final : public QObject {
public:
    DisplayRefreshObserver(QWidget *widget, QObject *context, std::function<void()> changed)
        : QObject(context), m_widget(widget), m_changed(std::move(changed))
    {
        if (widget)
            widget->installEventFilter(this);
        rebind();
    }

private:
    void rebind()
    {
        if (!m_widget)
            return;
        QWidget *topLevel = m_widget->window();
        if (m_topLevel != topLevel) {
            if (m_topLevel && m_topLevel != m_widget)
                m_topLevel->removeEventFilter(this);
            m_topLevel = topLevel;
            if (topLevel != m_widget)
                topLevel->installEventFilter(this);
        }
        QWindow *nativeWindow = topLevel->windowHandle();
        if (m_nativeWindow != nativeWindow) {
            disconnect(m_windowConnection);
            m_nativeWindow = nativeWindow;
            if (nativeWindow)
                m_windowConnection = connect(nativeWindow, &QWindow::screenChanged, this,
                    [this](QScreen *) { rebind(); if (m_widget && m_changed) m_changed(); });
        }
        QScreen *screen = m_widget->screen();
        if (m_screen != screen) {
            disconnect(m_screenConnection);
            m_screen = screen;
            if (screen)
                m_screenConnection = connect(screen, &QScreen::refreshRateChanged, this,
                    [this](qreal) { if (m_widget && m_changed) m_changed(); });
        }
    }

    bool eventFilter(QObject *, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::ParentChange:
        case QEvent::WinIdChange:
        case QEvent::ScreenChangeInternal:
        case QEvent::DevicePixelRatioChange:
            rebind();
            if (m_widget && m_changed) m_changed();
            break;
        default:
            break;
        }
        return false;
    }

    QPointer<QWidget> m_widget;
    QPointer<QWidget> m_topLevel;
    QPointer<QWindow> m_nativeWindow;
    QPointer<QScreen> m_screen;
    QMetaObject::Connection m_windowConnection;
    QMetaObject::Connection m_screenConnection;
    std::function<void()> m_changed;
};

class DisplayAnimationDriver final : public QAnimationDriver {
public:
    explicit DisplayAnimationDriver(QObject *parent) : QAnimationDriver(parent), m_tick(this)
    {
        m_tick.setTimerType(Qt::PreciseTimer);
        m_tick.setSingleShot(true);
        connect(&m_tick, &QTimer::timeout, this, [this] { advance(); });
        connect(qApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow *) {
            observeWindow();
            if (isRunning()) scheduleNext();
        });
    }

    ~DisplayAnimationDriver() override { delete m_observer; uninstall(); }

    qint64 elapsed() const override { return m_clock.isValid() ? m_clock.elapsed() : 0; }
    void advance() override
    {
        if (!isRunning()) return;
        m_schedule.setRefreshRate(displayRefreshRate(QApplication::activeWindow()), m_clock.nsecsElapsed());
        if (m_schedule.advance(m_clock.nsecsElapsed()))
            advanceAnimation();
        if (isRunning()) scheduleNext();
    }

protected:
    void start() override
    {
        m_clock.start();
        m_schedule.setRefreshRate(displayRefreshRate(QApplication::activeWindow()), 0);
        m_schedule.reset(0);
        observeWindow();
        QAnimationDriver::start();
        scheduleNext();
    }

    void stop() override
    {
        m_tick.stop();
        QAnimationDriver::stop();
    }

private:
    void observeWindow()
    {
        QWidget *window = QApplication::activeWindow();
        if (window == m_observedWindow) return;
        delete m_observer;
        m_observer = nullptr;
        m_observedWindow = window;
        if (window)
            m_observer = observeDisplayRefresh(window, this, [this] {
                if (isRunning()) scheduleNext();
            });
    }

    void scheduleNext()
    {
        if (!m_clock.isValid()) return;
        const qint64 now = m_clock.nsecsElapsed();
        m_schedule.setRefreshRate(displayRefreshRate(QApplication::activeWindow()), now);
        m_tick.start(m_schedule.delayMs(now));
    }

    QTimer m_tick;
    QElapsedTimer m_clock;
    FrameSchedule m_schedule;
    QPointer<QWidget> m_observedWindow;
    QObject *m_observer = nullptr;
};

}

QObject *observeDisplayRefresh(QWidget *widget, QObject *context, std::function<void()> changed)
{
    return new DisplayRefreshObserver(widget, context, std::move(changed));
}

void installAnimationDriver(QApplication &application)
{
    static DisplayAnimationDriver *driver = nullptr;
    if (!driver) {
        driver = new DisplayAnimationDriver(&application);
        QObject::connect(driver, &QObject::destroyed, &application, [] { driver = nullptr; });
        driver->install();
    }
}

}
