#pragma once

class QApplication;

namespace Aero {

// Uses the same clock for Qt's spring/button/property animations.
void installAnimationDriver(QApplication &application);

}
