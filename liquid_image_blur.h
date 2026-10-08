#pragma once

#include <QImage>
#include <algorithm>
#include <array>

namespace Liquid::detail {

// Pure image processing: safe on a worker thread with an owned/immutable QImage.
// Callers supply RGB32 or ARGB32_Premultiplied; edge pixels are extended exactly
// as in the original box filter. Separate strides also support padded images.
template<int Radius>
QImage boxBlurPass(const QImage &input, bool horizontal)
{
    static_assert(Radius >= 0 && Radius <= 1024);
    if (input.isNull())
        return {};
    Q_ASSERT(input.format() == QImage::Format_RGB32
             || input.format() == QImage::Format_ARGB32_Premultiplied);
    QImage result(input.size(), input.format());
    constexpr int divisor = Radius * 2 + 1;
    const int width = input.width(), height = input.height();
    const auto *source = reinterpret_cast<const QRgb *>(input.constBits());
    auto *target = reinterpret_cast<QRgb *>(result.bits());
    const qsizetype sourceStride = input.bytesPerLine() / sizeof(QRgb);
    const qsizetype targetStride = result.bytesPerLine() / sizeof(QRgb);
    if (horizontal) {
        for (int y = 0; y < height; ++y) {
            const QRgb *sourceLine = source + y * sourceStride;
            QRgb *targetLine = target + y * targetStride;
            const auto pixel = [&](int x) {
                return sourceLine[std::clamp(x, 0, width - 1)];
            };
            int red = 0, green = 0, blue = 0, alpha = 0;
            const auto add = [&](QRgb color, int sign) {
                red += sign * qRed(color);
                green += sign * qGreen(color);
                blue += sign * qBlue(color);
                alpha += sign * qAlpha(color);
            };
            for (int x = -Radius; x <= Radius; ++x)
                add(pixel(x), 1);
            for (int x = 0; x < width; ++x) {
                targetLine[x] = qRgba(red / divisor, green / divisor,
                                     blue / divisor, alpha / divisor);
                add(pixel(x - Radius), -1);
                add(pixel(x + Radius + 1), 1);
            }
        }
    } else {
        struct Sum { int red = 0, green = 0, blue = 0, alpha = 0; };
        // Small vertical strips keep sums in the CPU cache and visit each
        // input/output row contiguously instead of striding by whole columns.
        constexpr int blockSize = 128;
        for (int left = 0; left < width; left += blockSize) {
            const int right = std::min(width, left + blockSize);
            std::array<Sum, blockSize> sums{};
            for (int y = -Radius; y <= Radius; ++y) {
                const QRgb *row = source + std::clamp(y, 0, height - 1) * sourceStride;
                for (int x = left; x < right; ++x) {
                    const QRgb color = row[x];
                    auto &sum = sums[x - left];
                    sum.red += qRed(color);
                    sum.green += qGreen(color);
                    sum.blue += qBlue(color);
                    sum.alpha += qAlpha(color);
                }
            }
            for (int y = 0; y < height; ++y) {
                QRgb *row = target + y * targetStride;
                const QRgb *removed = source + std::max(0, y - Radius) * sourceStride;
                const QRgb *added = source + std::min(height - 1, y + Radius + 1) * sourceStride;
                for (int x = left; x < right; ++x) {
                    auto &sum = sums[x - left];
                    row[x] = qRgba(sum.red / divisor, sum.green / divisor,
                                   sum.blue / divisor, sum.alpha / divisor);
                    sum.red += qRed(added[x]) - qRed(removed[x]);
                    sum.green += qGreen(added[x]) - qGreen(removed[x]);
                    sum.blue += qBlue(added[x]) - qBlue(removed[x]);
                    sum.alpha += qAlpha(added[x]) - qAlpha(removed[x]);
                }
            }
        }
    }
    return result;
}

} // namespace Liquid::detail
