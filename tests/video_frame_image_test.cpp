#include "../video_frame_image.h"

#include <QGuiApplication>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAbstractVideoBuffer>
#endif

namespace {
void check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

QImage pixels(QSize size, QImage::Format format)
{
    QImage image(size, format);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            image.setPixelColor(x, y, QColor((x * 53 + 11) % 256, (y * 77 + 19) % 256,
                                            (x * 31 + y * 13 + 173) % 256,
                                            image.hasAlphaChannel() ? (x * 23 + y * 41 + 97) % 256 : 255));
    return image;
}

QVideoFrame softwareFrame(const QImage &image)
{
    QVideoFrame frame(QVideoFrameFormat(image.size(),
        QVideoFrameFormat::pixelFormatFromImageFormat(image.format())));
    check(frame.map(QVideoFrame::WriteOnly), "A software fixture must be writable.");
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(frame.bits(0) + y * frame.bytesPerLine(0), image.constScanLine(y), image.width() * 4);
    frame.unmap();
    return frame;
}

void ownedSoftwareTest()
{
    check(VideoFrames::toImage({}).isNull(), "An invalid frame must yield an empty image.");
    check(!VideoFrames::canConvertRgb({}) && VideoFrames::toRgbImage({}).isNull(),
          "The direct-only API must reject invalid frames without trying Qt conversion.");
    for (const auto format : {QImage::Format_RGBX8888, QImage::Format_RGB32,
                             QImage::Format_RGBA8888, QImage::Format_ARGB32_Premultiplied}) {
        const QImage original = pixels(QSize(7, 5), format);
        QVideoFrame frame = softwareFrame(original);
        check(VideoFrames::canConvertRgb(frame), "Plain SDR software RGB must be eligible for direct conversion.");
        const QImage result = VideoFrames::toImage(frame);
        const auto expectedFormat = original.hasAlphaChannel()
            ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        check(result.format() == expectedFormat && result == original.convertToFormat(expectedFormat),
              "RGB conversion must preserve every original pixel and use a raster-friendly format.");
        check(!frame.isMapped(), "The helper must release its mapping before returning.");
        check(frame.map(QVideoFrame::WriteOnly), "The source must remain independently reusable.");
        std::memset(frame.bits(0), 0, frame.mappedBytes(0));
        frame.unmap();
        frame = {};
        check(result == original.convertToFormat(expectedFormat),
              "Returned pixels must survive source reuse and frame destruction.");
    }

    for (const auto format : {QImage::Format_RGBX8888, QImage::Format_RGB32}) {
        const QImage expected = pixels(QSize(7, 5), format).convertToFormat(QImage::Format_RGB32);
        QVideoFrame frame = softwareFrame(pixels(QSize(7, 5), format));
        check(frame.map(QVideoFrame::WriteOnly), "An opaque fixture must allow modifying its unused X bytes.");
        for (int y = 0; y < frame.height(); ++y) {
            uchar *row = frame.bits(0) + y * frame.bytesPerLine(0);
            for (int x = 0; x < frame.width(); ++x) {
                const int padding = format == QImage::Format_RGBX8888 || Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? 3 : 0;
                row[x * 4 + padding] = uchar(x * 11);
            }
        }
        frame.unmap();
        const QImage result = VideoFrames::toImage(frame);
        check(result == expected,
              "Opaque RGBX/BGRX padding must never become transparency or change captured RGB colors.");
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x)
                check(qAlpha(result.pixel(x, y)) == 255,
                      "Opaque captures must have an actual 255 alpha byte for Qt backing-store composition.");
    }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
struct BufferState {
    int maps = 0;
    int unmaps = 0;
    int destroyed = 0;
    bool poisonOnUnmap = false;
    bool failMap = false;
};

class PaddedBuffer final : public QAbstractVideoBuffer {
public:
    PaddedBuffer(const QImage &image, const std::shared_ptr<BufferState> &state,
                 QVideoFrameFormat format = {}, int reportedBytes = -1)
        : m_state(state), m_format(format.isValid() ? format
            : QVideoFrameFormat(image.size(), QVideoFrameFormat::pixelFormatFromImageFormat(image.format()))),
          m_stride(image.width() * 4 + 20), m_bytes(m_stride * image.height(), '\x5a'),
          m_reportedBytes(reportedBytes < 0 ? m_bytes.size() : reportedBytes)
    {
        for (int y = 0; y < image.height(); ++y)
            std::memcpy(m_bytes.data() + y * m_stride, image.constScanLine(y), image.width() * 4);
    }
    ~PaddedBuffer() override { ++m_state->destroyed; }
    MapData map(QVideoFrame::MapMode mode) override
    {
        if (mode != QVideoFrame::ReadOnly)
            return {};
        ++m_state->maps;
        if (m_state->failMap)
            return {};
        MapData data;
        data.planeCount = 1;
        data.bytesPerLine[0] = m_stride;
        data.data[0] = reinterpret_cast<uchar *>(m_bytes.data());
        data.dataSize[0] = m_reportedBytes;
        return data;
    }
    void unmap() override
    {
        ++m_state->unmaps;
        if (m_state->poisonOnUnmap)
            m_bytes.fill('\x3c');
    }
    QVideoFrameFormat format() const override { return m_format; }
private:
    std::shared_ptr<BufferState> m_state;
    QVideoFrameFormat m_format;
    int m_stride;
    QByteArray m_bytes;
    int m_reportedBytes;
};

void paddedMappingTest()
{
    for (const auto format : {QImage::Format_RGBX8888, QImage::Format_RGB32,
                             QImage::Format_RGBA8888, QImage::Format_ARGB32_Premultiplied}) {
        const QImage original = pixels(QSize(9, 4), format);
        auto state = std::make_shared<BufferState>();
        state->poisonOnUnmap = true;
        QVideoFrame frame(std::make_unique<PaddedBuffer>(original, state));
        check(VideoFrames::canConvertRgb(frame) && state->maps == 0,
              "Eligibility must inspect only metadata, without mapping the source.");
        const QImage result = VideoFrames::toRgbImage(frame);
        const auto output = original.hasAlphaChannel()
            ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        check(result == original.convertToFormat(output),
              "Padded strides and odd-width color channels must match the original, without padding pixels.");
        check(state->maps == 1 && state->unmaps == 1 && !frame.isMapped(),
              "The direct path must map exactly once and release the GPU/CPU mapping synchronously.");
        frame = {};
        check(state->destroyed == 1 && result == original.convertToFormat(output),
              "Output must retain independent pixels after unmap poisons storage and the source is destroyed.");
    }
    const QImage image = pixels(QSize(9, 4), QImage::Format_RGBX8888);
    auto state = std::make_shared<BufferState>();
    QVideoFrame shortFrame(std::make_unique<PaddedBuffer>(image, state, QVideoFrameFormat{}, 20));
    check(VideoFrames::toImage(shortFrame).isNull() && state->maps == 1 && state->unmaps == 1,
          "An undersized mapping must fail without a second unsafe conversion or leaked map.");

    state = std::make_shared<BufferState>();
    const int stride = image.width() * 4 + 20;
    const int missingLastRowPadding = stride * (image.height() - 1) + image.width() * 4;
    QVideoFrame noPadding(std::make_unique<PaddedBuffer>(image, state, QVideoFrameFormat{}, missingLastRowPadding));
    check(VideoFrames::toRgbImage(noPadding).isNull() && state->maps == 1 && state->unmaps == 1,
          "Even complete visible pixels must be rejected if the borrowed QImage's last-row padding is unavailable.");

    state = std::make_shared<BufferState>();
    state->failMap = true;
    QVideoFrame unavailable(std::make_unique<PaddedBuffer>(image, state));
    check(VideoFrames::toRgbImage(unavailable).isNull() && state->maps == 1 && state->unmaps == 0,
          "A failed direct mapping must not invoke Qt fallback or unmap an unsuccessful mapping.");

    state = std::make_shared<BufferState>();
    QVideoFrame mapped(std::make_unique<PaddedBuffer>(image, state));
    check(mapped.map(QVideoFrame::ReadOnly), "The caller must own a valid initial mapping.");
    check(!VideoFrames::canConvertRgb(mapped) && VideoFrames::toRgbImage(mapped).isNull()
        && state->maps == 1 && state->unmaps == 0,
          "Direct-only conversion must reject another owner's mapping without touching its mapping or Qt fallback.");
    const QImage result = VideoFrames::toImage(mapped);
    check(!result.isNull() && mapped.isMapped() && state->maps == 1 && state->unmaps == 0,
          "Fallback for an already mapped frame must leave the caller's mapping intact.");
    mapped.unmap();
    check(state->unmaps == 1, "Only the caller must release its original mapping.");
}

void transformedAndColorFallbackTest()
{
    const QImage image = pixels(QSize(9, 4), QImage::Format_RGBX8888);
    const auto compare = [&](QVideoFrameFormat format) {
        auto state = std::make_shared<BufferState>();
        QVideoFrame frame(std::make_unique<PaddedBuffer>(image, state, format));
        check(!VideoFrames::canConvertRgb(frame) && VideoFrames::toRgbImage(frame).isNull()
            && state->maps == 0 && state->unmaps == 0,
              "Unsupported orientation/color metadata must never map or run Qt conversion in the direct-only API.");
        const QImage reference = frame.toImage();
        const int originalMaps = state->maps;
        const QImage result = VideoFrames::toImage(frame);
        check(!reference.isNull() && result == reference && state->maps == originalMaps,
              "Unsupported orientation/color metadata must use Qt's existing cached conversion unchanged.");
        check(!frame.isMapped(), "A transformed fallback must not retain a mapping.");
    };
    QVideoFrameFormat format(image.size(), QVideoFrameFormat::Format_RGBX8888);
    format.setRotation(QtVideo::Rotation::Clockwise90);
    compare(format);
    format.setRotation(QtVideo::Rotation::None);
    format.setMirrored(true);
    compare(format);
    format.setMirrored(false);
    format.setScanLineDirection(QVideoFrameFormat::BottomToTop);
    compare(format);
    format.setScanLineDirection(QVideoFrameFormat::TopToBottom);
    format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    compare(format);
    format.setColorRange(QVideoFrameFormat::ColorRange_Full);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_BT2020);
    compare(format);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_Undefined);
    format.setColorTransfer(QVideoFrameFormat::ColorTransfer_ST2084);
    compare(format);
}

void directWorkerTest()
{
    const QImage original = pixels(QSize(19, 13), QImage::Format_RGBX8888);
    auto state = std::make_shared<BufferState>();
    state->poisonOnUnmap = true;
    QVideoFrame frame(std::make_unique<PaddedBuffer>(original, state));
    QImage converted;
    std::unique_ptr<QThread> worker(QThread::create([frame, &converted] {
        converted = VideoFrames::toRgbImage(frame);
    }));
    worker->start();
    check(worker->wait(5000), "Pure RGB direct conversion must complete in a worker without processing GUI events.");
    worker.reset();
    check(converted == original.convertToFormat(QImage::Format_RGB32)
        && state->maps == 1 && state->unmaps == 1 && !frame.isMapped(),
          "Worker RGB conversion must preserve colors and release its mapping before delivering independent pixels.");
}
#endif

void yuvFallbackTest()
{
    QVideoFrame frame(QVideoFrameFormat(QSize(8, 6), QVideoFrameFormat::Format_NV12));
    check(frame.map(QVideoFrame::WriteOnly), "A YUV fallback fixture must be writable.");
    std::memset(frame.bits(0), 97, frame.mappedBytes(0));
    std::memset(frame.bits(1), 128, frame.mappedBytes(1));
    frame.unmap();
    check(!VideoFrames::canConvertRgb(frame) && VideoFrames::toRgbImage(frame).isNull(),
          "The worker's direct-only API must reject YUV without invoking QRhi conversion.");
    const QImage reference = frame.toImage();
    check(!reference.isNull() && VideoFrames::toImage(frame) == reference,
          "YUV must keep Qt color conversion rather than reinterpret luminance as packed RGB.");
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ownedSoftwareTest();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    paddedMappingTest();
    transformedAndColorFallbackTest();
    directWorkerTest();
#endif
    yuvFallbackTest();
    std::fprintf(stderr, "Direct RGB frame conversion passed: exact colors, padded rows, owned pixels, balanced mapping, Qt fallback.\n");
    return 0;
}
