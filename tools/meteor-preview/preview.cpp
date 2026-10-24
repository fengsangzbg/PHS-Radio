// Offscreen filmstrip renderer for the dock meteor. Built against the same
// meteor_effect.cpp the player uses, so the art can be reviewed without
// launching the full app or waiting for a real playback transition.
//
//   g++ -std=c++17 -O2 preview.cpp ../../meteor_effect.cpp $(pkg-config --cflags --libs Qt6Widgets)
//   ./a.exe water water.png

#include "../../meteor_effect.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <cstdio>

// The real build takes this from aero_widgets.cpp; the preview links no UI
// library, so it pins the tick to a plain 60 Hz frame.
namespace Aero {
int animationInterval(const QWidget *) { return 16; }
}

namespace {

QImage dockBackdrop(const QSize &size, qreal scale)
{
    QImage image(QSize(int(size.width() * scale), int(size.height() * scale)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF panel = QRectF(QPointF(0, 0), QSizeF(size)).adjusted(2, 2, -2, -2);
    QLinearGradient glass(panel.topLeft(), panel.bottomRight());
    glass.setColorAt(0, QColor(70, 120, 132, 235));
    glass.setColorAt(.45, QColor(38, 62, 92, 235));
    glass.setColorAt(1, QColor(28, 40, 62, 235));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glass);
    painter.drawRoundedRect(panel, 26, 26);

    QRadialGradient key(panel.left() + panel.width() * .22, panel.top() + panel.height() * .1,
                        panel.width() * .55);
    key.setColorAt(0, QColor(180, 240, 255, 30));
    key.setColorAt(1, QColor(180, 240, 255, 0));
    painter.setBrush(key);
    painter.drawRoundedRect(panel, 26, 26);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(150, 230, 240, 70), 1.4));
    painter.drawRoundedRect(panel.adjusted(.7, .7, -.7, -.7), 26, 26);
    painter.end();
    return image;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString style = argc > 1 ? QString::fromLatin1(argv[1]) : QStringLiteral("water");
    const QString output = argc > 2 ? QString::fromLatin1(argv[2]) : QStringLiteral("meteor.png");
    const QSize size(1080, 156);
    constexpr qreal sheetScale = 1.6;

    QWidget host;
    host.setFixedSize(size);
    const QImage backdrop = dockBackdrop(size, 2.0);

    PlaybackMeteor meteor(&host);
    meteor.setGeometry(host.rect());
    meteor.setStyle(style == QStringLiteral("fire") ? PlaybackMeteor::Style::Fire
                                                    : PlaybackMeteor::Style::GreenLaser);

    static const qreal targets[8] = {0.05, 0.16, 0.29, 0.42, 0.55, 0.66, 0.77, 0.86};
    const QString frameDir = argc > 3 ? QString::fromLatin1(argv[3]) : QString();
    if (!frameDir.isEmpty())
        QDir().mkpath(frameDir);
    int frameIndex = 0;
    int tickCount = 0;
    QVector<QImage> frames;
    QVector<qreal> stamps;
    int nextTarget = 0;

    QElapsedTimer total;
    total.start();
    meteor.trigger();

    QTimer tick;
    tick.setInterval(8);
    QObject::connect(&tick, &QTimer::timeout, [&] {
        if (meteor.isRunning()) {
            const qreal progress = meteor.progress();
            if (!frameDir.isEmpty() && (tickCount++ % 2) == 0) {
                QImage layer(meteor.size(), QImage::Format_ARGB32_Premultiplied);
                layer.fill(Qt::transparent);
                meteor.render(&layer);
                QImage composed(backdrop.size(), QImage::Format_ARGB32_Premultiplied);
                composed.setDevicePixelRatio(2);
                composed.fill(Qt::transparent);
                QPainter painter(&composed);
                painter.drawImage(0, 0, backdrop);
                painter.drawImage(0, 0, layer);
                painter.end();
                composed.save(QStringLiteral("%1/frame_%2.png")
                                  .arg(frameDir)
                                  .arg(frameIndex++, 3, 10, QLatin1Char('0')));
            }
            if (nextTarget < 8 && progress >= targets[nextTarget]) {
                QImage layer(meteor.size(), QImage::Format_ARGB32_Premultiplied);
                layer.fill(Qt::transparent);
                meteor.render(&layer);
                QImage composed(backdrop.size(), QImage::Format_ARGB32_Premultiplied);
                composed.setDevicePixelRatio(2);
                composed.fill(Qt::transparent);
                QPainter painter(&composed);
                painter.drawImage(0, 0, backdrop);
                painter.drawImage(0, 0, layer);
                painter.end();
                frames.append(composed);
                stamps.append(progress);
                ++nextTarget;
            }
            return;
        }
        if (frames.isEmpty()) {
            app.quit();
            return;
        }
        tick.stop();
        const int tileW = int(size.width() * sheetScale);
        const int tileH = int(size.height() * sheetScale);
        const int pad = 12, label = 20;
        QImage sheet(tileW + pad * 2, (tileH + label) * frames.size() + pad * 2,
                     QImage::Format_ARGB32_Premultiplied);
        sheet.fill(QColor(14, 18, 26));
        QPainter sheetPainter(&sheet);
        sheetPainter.setRenderHint(QPainter::Antialiasing);
        sheetPainter.setRenderHint(QPainter::SmoothPixmapTransform);
        for (int i = 0; i < frames.size(); ++i) {
            const int y = pad + i * (tileH + label);
            sheetPainter.drawImage(QRect(pad, y, tileW, tileH), frames.at(i));
            sheetPainter.setPen(QColor(150, 170, 190));
            sheetPainter.drawText(QRect(pad, y + tileH + 2, tileW, label),
                                  QStringLiteral("%1  frame %2  progress %3")
                                      .arg(style)
                                      .arg(i)
                                      .arg(stamps.at(i), 0, 'f', 3));
        }
        sheetPainter.end();
        sheet.save(output);
        std::printf("frames=%lld elapsed=%lldms saved=%s\n", (long long)frames.size(),
                    (long long)total.elapsed(), qPrintable(output));
        app.quit();
    });
    tick.start();
    return app.exec();
}
