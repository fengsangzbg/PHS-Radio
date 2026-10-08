#include "../liquid_image_blur.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <cstdlib>
#include <vector>

namespace {
QImage reference(const QImage &input, bool horizontal)
{
    QImage result(input.size(), input.format());
    for (int y = 0; y < input.height(); ++y)
        for (int x = 0; x < input.width(); ++x) {
            int red = 0, green = 0, blue = 0, alpha = 0;
            for (int offset = -3; offset <= 3; ++offset) {
                const int sourceX = std::clamp(x + (horizontal ? offset : 0), 0, input.width() - 1);
                const int sourceY = std::clamp(y + (horizontal ? 0 : offset), 0, input.height() - 1);
                const QRgb color = reinterpret_cast<const QRgb *>(input.constScanLine(sourceY))[sourceX];
                red += qRed(color);
                green += qGreen(color);
                blue += qBlue(color);
                alpha += qAlpha(color);
            }
            reinterpret_cast<QRgb *>(result.scanLine(y))[x] =
                qRgba(red / 7, green / 7, blue / 7, alpha / 7);
        }
    return result;
}

void check(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::abort();
    }
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    check(Liquid::detail::boxBlurPass<3>({}, true).isNull(), "An empty blur must remain empty.");
    QRandomGenerator random(1947);
    for (QImage::Format format : {QImage::Format_RGB32, QImage::Format_ARGB32_Premultiplied})
        for (QSize size : {QSize(1, 1), QSize(1, 19), QSize(17, 1), QSize(53, 29),
                          QSize(129, 31), QSize(257, 83), QSize(640, 360), QSize(1280, 80)}) {
            // External storage deliberately has padding, including a width that
            // crosses a 128-pixel tile boundary. Padding is not part of the image.
            const int stride = size.width() * 4 + 12;
            std::vector<quint32> storage(stride / 4 * size.height(), 0xdeadbeef);
            QImage input(reinterpret_cast<uchar *>(storage.data()), size.width(), size.height(), stride, format);
            for (int y = 0; y < size.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(input.scanLine(y));
                for (int x = 0; x < size.width(); ++x) {
                    const QRgb color = random.generate();
                    row[x] = format == QImage::Format_RGB32 ? (color | 0xff000000) : qPremultiply(color);
                }
            }
            const auto untouched = storage;
            for (bool horizontal : {false, true}) {
                const QImage actual = Liquid::detail::boxBlurPass<3>(input, horizontal);
                check(actual == reference(input, horizontal),
                      "The optimized blur must preserve every edge, color and premultiplied alpha pixel.");
                check(actual.format() == format, "The optimized blur must preserve the source pixel format.");
            }
            QImage actual = input, expected = input;
            for (int pass = 0; pass < 2; ++pass)
                for (bool horizontal : {true, false}) {
                    actual = Liquid::detail::boxBlurPass<3>(actual, horizontal);
                    expected = reference(expected, horizontal);
                }
            check(actual == expected, "Repeated blur passes must retain the original glass texture.");
            check(storage == untouched, "Blurring must not mutate the source or its padding.");
        }
    qInfo("Liquid image blur tests passed.");
}
