#include "aero_animation.h"
#include "aero_widgets.h"

#include <QAnimationDriver>
#include <QApplication>
#include <QElapsedTimer>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace Aero {

int animationInterval(const QWidget *widget)
{
    const QScreen *screen = widget ? widget->screen() : QGuiApplication::primaryScreen();
    const qreal refresh = screen && screen->refreshRate() > 0 ? screen->refreshRate() : 60.0;
    return std::max(7, qRound(1000.0 / std::clamp(refresh, 60.0, 144.0)));
}

namespace {

class DisplayAnimationDriver final : public QAnimationDriver {
public:
    explicit DisplayAnimationDriver(QObject *parent) : QAnimationDriver(parent), m_tick(this)
    {
        m_tick.setTimerType(Qt::PreciseTimer);
        connect(&m_tick, &QTimer::timeout, this, [this] { advance(); });
    }

    ~DisplayAnimationDriver() override { uninstall(); }

    qint64 elapsed() const override { return m_clock.isValid() ? m_clock.elapsed() : 0; }
    void advance() override { if (isRunning()) advanceAnimation(); }

protected:
    void start() override
    {
        m_clock.start();
        QAnimationDriver::start();
        m_tick.start(animationInterval(QApplication::activeWindow()));
    }

    void stop() override
    {
        m_tick.stop();
        QAnimationDriver::stop();
    }

private:
    QTimer m_tick;
    QElapsedTimer m_clock;
};

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
