#pragma once

#include <QImage>

class QVideoFrame;

namespace VideoFrames {
// Checks the public frame format without mapping or invoking Qt conversion.
bool canConvertRgb(const QVideoFrame &frame);
// Direct RGB conversion only. Unsupported/failed frames return an empty image;
// this never creates a QRhi or falls back to GPU/shader conversion.
QImage toRgbImage(const QVideoFrame &frame);
// Owns the returned pixels. Plain SDR screen-capture RGB bypasses Qt's
// additional shader upload/readback; all other formats keep Qt conversion.
QImage toImage(const QVideoFrame &frame);
}
