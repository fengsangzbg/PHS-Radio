#include "../aero_animation.h"
#include "../aero_widgets.h"

#include <QAnimationDriver>
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVariantAnimation>
#include <cstdlib>

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
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Aero::installAnimationDriver(app);
    const auto drivers = app.findChildren<QAnimationDriver *>();
    check(drivers.size() == 1, "A single display animation clock must serve the application.");
    Aero::installAnimationDriver(app);
    check(app.findChildren<QAnimationDriver *>().size() == 1, "Repeated installation must not add duplicate timers.");
    check(Aero::animationInterval(nullptr) >= 7 && Aero::animationInterval(nullptr) <= 17,
          "The target clock must align with a display rate between 60 and 144 Hz.");

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
