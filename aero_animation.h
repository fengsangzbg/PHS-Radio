#pragma once

#include <QtGlobal>
#include <functional>

class QApplication;
class QObject;
class QWidget;

namespace Aero {

qreal displayRefreshRate(const QWidget *widget);
qreal boundedRefreshRate(qreal refreshRate);

// Absolute nanosecond deadlines retain fractional milliseconds (144/240 Hz)
// and skip missed frames rather than replaying a stalled GUI's old frames.
class FrameSchedule final {
public:
    explicit FrameSchedule(qreal refreshRate = 60);
    void setRefreshRate(qreal refreshRate, qint64 nowNs);
    void reset(qint64 nowNs);
    bool advance(qint64 nowNs);
    int delayMs(qint64 nowNs) const;
    qreal refreshRate() const { return m_refreshRate; }
    qint64 periodNs() const { return m_periodNs; }

private:
    qreal m_refreshRate = 60;
    qint64 m_periodNs = 16666667;
    qint64 m_nextFrameNs = 0;
};

// Owned by context; follows the widget's current native window and screen.
QObject *observeDisplayRefresh(QWidget *widget, QObject *context,
                               std::function<void()> changed);

// Uses the same clock for Qt's spring/button/property animations.
void installAnimationDriver(QApplication &application);

}
