#include "video_frame_image.h"

#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <limits>

namespace {
bool hasPlainRgbPixels(const QVideoFrame &frame)
{
    if (frame.handleType() != QVideoFrame::NoHandle || frame.isMapped())
        return false;
    const auto format = frame.surfaceFormat();
    if (format.scanLineDirection() != QVideoFrameFormat::TopToBottom || format.isMirrored())
        return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    if (format.rotation() != QtVideo::Rotation::None)
        return false;
#endif
    // WGC delivers full-range RGBX with undefined color metadata. Explicit
    // color conversion / HDR / limited-range streams remain Qt's responsibility.
    if (format.colorSpace() != QVideoFrameFormat::ColorSpace_Undefined
        || format.colorTransfer() != QVideoFrameFormat::ColorTransfer_Unknown
        || (format.colorRange() != QVideoFrameFormat::ColorRange_Unknown
            && format.colorRange() != QVideoFrameFormat::ColorRange_Full))
        return false;
    switch (frame.pixelFormat()) {
    case QVideoFrameFormat::Format_RGBX8888:
    case QVideoFrameFormat::Format_BGRX8888:
    case QVideoFrameFormat::Format_XRGB8888:
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
    case QVideoFrameFormat::Format_RGBA8888:
        return QVideoFrameFormat::imageFormatFromPixelFormat(frame.pixelFormat())
            != QImage::Format_Invalid;
    default:
        return false;
    }
}

struct FrameUnmapper {
    QVideoFrame &frame;
    ~FrameUnmapper() { frame.unmap(); }
};
} // namespace

namespace VideoFrames {
bool canConvertRgb(const QVideoFrame &frame)
{
    return frame.isValid() && !frame.size().isEmpty() && hasPlainRgbPixels(frame);
}

QImage toRgbImage(const QVideoFrame &frame)
{
    if (!canConvertRgb(frame))
        return {};
    QVideoFrame mapped = frame;
    if (!mapped.map(QVideoFrame::ReadOnly))
        return {};
    FrameUnmapper unmapper{mapped};
    const qint64 rowBytes = qint64(mapped.width()) * 4;
    const int stride = mapped.bytesPerLine(0);
    // A borrowed QImage describes all stride*height bytes, including padding
    // in its last row. Validate that complete allocation before constructing it.
    const qint64 required = qint64(stride) * mapped.height();
    if (mapped.planeCount() != 1 || !mapped.bits(0) || stride < rowBytes
        || rowBytes > std::numeric_limits<int>::max()
        || required > mapped.mappedBytes(0)) {
        // A malformed layout must not be handed to another converter that
        // could read beyond the backend allocation.
        return {};
    }
    const QImage::Format format = QVideoFrameFormat::imageFormatFromPixelFormat(mapped.pixelFormat());
    const QImage borrowed(static_cast<const uchar *>(mapped.bits(0)), mapped.width(),
                          mapped.height(), stride, format);
    const QImage::Format rasterFormat = borrowed.hasAlphaChannel()
        ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
    if (format == QImage::Format_RGB32) {
        // BGRX/XRGB padding is not a meaningful alpha value. Qt's RGB32->RGB32
        // copy would retain arbitrary X bytes, while its RGB video shader
        // forces opacity. The matching ARGB layout lets Qt mask alpha once
        // without altering color channel order or allocating an extra copy.
        const QImage opaqueSource(static_cast<const uchar *>(mapped.bits(0)), mapped.width(),
                                  mapped.height(), stride, QImage::Format_ARGB32);
        return opaqueSource.convertToFormat(QImage::Format_RGB32);
    }
    // convertToFormat may share storage when no conversion is needed. copy()
    // in that case is essential: no mapped texture escapes the unmap guard.
    return format == rasterFormat ? borrowed.copy() : borrowed.convertToFormat(rasterFormat);
}

QImage toImage(const QVideoFrame &frame)
{
    if (!frame.isValid() || frame.size().isEmpty())
        return {};
    return canConvertRgb(frame) ? toRgbImage(frame) : frame.toImage();
}
} // namespace VideoFrames
