#include "../aero_animation.h"
#include "../aero_widgets.h"

#include <QAnimationDriver>
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QScreen>
#include <QWidget>
#include <QWindow>
#include <QVariantAnimation>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) { qCritical("%s", message); std::abort(); }
}
void waitForEvents(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void frameBudgetTest()
{
    for (qreal rate : {30.0, 59.94, 60.0, 120.0, 144.0, 165.0, 240.0, 360.0}) {
        Aero::FrameSchedule schedule(rate);
        schedule.reset(0);
        qint64 now = 0;
        int frames = 0;
        while (now < 1000000000) {
            now += qint64(schedule.delayMs(now)) * 1000000;
            if (now > 1000000000) break;
            if (schedule.advance(now)) ++frames;
            check(!schedule.advance(now), "A single time instant must never advance the frame budget twice.");
        }
        check(std::abs(frames - rate) <= 1.01,
              "Fractional millisecond deadlines must sustain the real 30-360 Hz budget without rounding drift.");
        check(schedule.advance(3000000000), "A stalled GUI must resume on the latest frame.");
        check(!schedule.advance(3000000000) && schedule.delayMs(3000000000) <= 34,
              "A stalled GUI must skip missed frames instead of catching up in an unbounded burst.");
    }
    Aero::FrameSchedule schedule(60);
    schedule.reset(0);
    schedule.setRefreshRate(240, 1000000);
    check(schedule.delayMs(1000000) == 5 && !schedule.advance(5000000)
        && schedule.advance(6000000), "Moving to a 240 Hz screen must replace the previous deadline promptly.");
    schedule.setRefreshRate(240, 6000000);
    check(schedule.delayMs(6000000) <= 4, "An unchanged refresh rate must preserve the fractional frame phase.");
    check(Aero::boundedRefreshRate(1000) == 360 && Aero::boundedRefreshRate(1) == 30
        && Aero::boundedRefreshRate(0) == 60
        && Aero::boundedRefreshRate(std::numeric_limits<qreal>::quiet_NaN()) == 60,
          "Invalid monitor values require a safe fallback and supported high refresh values require a bounded budget.");

    Aero::FrameSchedule recovery(60);
    recovery.reset(0);
    check(recovery.advance(17000000) && recovery.advance(49000000),
          "The fixture must reproduce a 60 Hz frame delayed by a busy GUI.");
    check(!recovery.advance(51000000) && recovery.delayMs(49000000) == 17
        && recovery.advance(66000000),
          "After an overloaded 49 ms frame, a 51 ms tick must not enqueue another render before input can recover.");
    recovery.setRefreshRate(144, 0);
    recovery.reset(0);
    check(recovery.advance(7000000) && recovery.advance(27000000)
        && !recovery.advance(28000000) && recovery.advance(34000000),
          "A delayed 144 Hz frame must also avoid an immediate 1 ms recovery render.");
}

void displayObserverTest()
{
    QWidget window;
    window.show();
    waitForEvents(20);
    int changed = 0;
    QObject context;
    QObject *observer = Aero::observeDisplayRefresh(&window, &context, [&] { ++changed; });
    QEvent screenChange(QEvent::ScreenChangeInternal);
    QCoreApplication::sendEvent(&window, &screenChange);
    check(changed > 0, "Display migration events must retarget animation schedules without a resize.");
    if (QScreen *screen = window.screen()) {
        const int previous = changed;
        check(QMetaObject::invokeMethod(screen, "refreshRateChanged", Qt::DirectConnection,
                                       Q_ARG(qreal, screen->refreshRate())),
              "The real monitor refresh notification must be callable in the fixture.");
        check(changed == previous + 1, "Refresh-rate changes must update existing schedules immediately.");
        delete observer;
        const int deleted = changed;
        QMetaObject::invokeMethod(screen, "refreshRateChanged", Qt::DirectConnection,
                                 Q_ARG(qreal, screen->refreshRate()));
        check(changed == deleted, "Destroying the observer must disconnect its monitor callbacks.");
    }
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    frameBudgetTest();
    displayObserverTest();
    Aero::installAnimationDriver(app);
    const auto drivers = app.findChildren<QAnimationDriver *>();
    check(drivers.size() == 1, "A single display animation clock must serve the application.");
    Aero::installAnimationDriver(app);
    check(app.findChildren<QAnimationDriver *>().size() == 1, "Repeated installation must not add duplicate timers.");
    check(Aero::animationInterval(nullptr) >= 3 && Aero::animationInterval(nullptr) <= 34,
          "The target clock must support actual display rates between 30 and 360 Hz.");

    QVariantAnimation animation;
    animation.setDuration(240);
    animation.setStartValue(0.0);
    animation.setEndValue(1.0);
    int frames = 0;
    bool finished = false;
    QObject::connect(&animation, &QVariantAnimation::valueChanged, [&] { ++frames; });
    QObject::connect(&animation, &QVariantAnimation::finished, [&] { finished = true; });
    animation.start();
    waitForEvents(65);
    check(drivers.first()->isRunning() && frames > 1, "The clock must actually advance visible animations.");
    animation.pause();
    const QVariant paused = animation.currentValue();
    waitForEvents(85);
    check(animation.currentValue() == paused, "Paused animations must retain their visual state.");
    animation.resume();
    waitForEvents(280);
    check(finished && animation.currentValue().toDouble() == 1.0,
          "Resuming the high refresh clock must finish at the correct animation endpoint.");
    waitForEvents(30);
    check(!drivers.first()->isRunning(), "The animation clock must stop polling after the last animation settles.");

    finished = false;
    animation.start();
    waitForEvents(40);
    animation.stop();
    const int stoppedFrames = frames;
    waitForEvents(100);
    check(!finished && frames == stoppedFrames, "Stopping a spring must cancel subsequent visual updates.");
    return 0;
}
