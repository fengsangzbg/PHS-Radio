#include <QtWidgets>
#define private public
#include "../meteor_effect.h"
#undef private
#include "../aero_widgets.h"
#include <cstdlib>

namespace {
void check(bool condition, const char *message)
{
    if (!condition) { qCritical("%s", message); std::abort(); }
}

void waitForEvents(int duration)
{
    QEventLoop events;
    QTimer::singleShot(duration, &events, &QEventLoop::quit);
    events.exec();
}

int coolPixels(const QImage &frame)
{
    int count = 0;
    for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x) {
            const QColor pixel = frame.pixelColor(x, y);
            if (pixel.alpha() > 15 && pixel.blue() > pixel.red() + 8)
                ++count;
        }
    return count;
}

int paintedArea(const QRegion &region)
{
    qint64 area = 0;
    for (const QRect &strip : region)
        area += qint64(strip.width()) * strip.height();
    return int(area);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget host;
    host.resize(920, 156);
    QPushButton underlying(QStringLiteral("Underlying control"), &host);
    underlying.setGeometry(20, 70, 140, 36);
    PlaybackMeteor meteor(&host);
    host.show();
    QCoreApplication::processEvents();
    check(meteor.isHidden() && !meteor.isRunning(),
          "The effect must be idle until playback starts.");
    check(meteor.visibleMeteorCount() == 0 && meteor.Ribbons.size() == 7
              && meteor.FireBeams.size() == 7,
          "An idle shower must keep both style profiles dormant without creating widgets.");
    check(meteor.durationMs() == PlaybackMeteor::RibbonDurationMs,
          "The default water dock must use the ribbon timeline.");
    meteor.trigger();
    check(meteor.isRunning() && meteor.progress() == 0 && meteor.geometry() == host.rect(),
          "Triggering must start a full-size transparent overlay at the beginning of the path.");
    check(meteor.testAttribute(Qt::WA_TransparentForMouseEvents) && host.childAt(60, 80) == &underlying,
          "The overlay must allow clicks to reach controls beneath it.");
    check(meteor.m_timer->timerType() == Qt::PreciseTimer
              && meteor.m_timer->interval() == Aero::animationInterval(&host),
          "The meteor timer must match the screen refresh interval.");

    const QPointF firstHead = meteor.headPosition();
    QImage frame(meteor.size(), QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    meteor.render(&frame, QPoint(), QRegion(), QWidget::DrawChildren);
    check(meteor.visibleMeteorCount() == 1,
          "The leading ribbon must be aloft before its companions.");
    check(coolPixels(frame) > 60,
          "The water dock must paint a cool glass streak, not a green laser.");

    waitForEvents(180);
    check(meteor.progress() > 0 && meteor.progress() < 1,
          "The sweep must advance through the ribbon timeline.");
    const QPointF movedHead = meteor.headPosition();
    check(QLineF(firstHead, movedHead).length() > 4,
          "A ribbon head must travel, unlike the fixed fire beam.");
    check(meteor.direction().x() < 0 && meteor.direction().y() > 0,
          "Ribbons must travel from upper right toward lower left.");
    check(meteor.frameRegion().contains(movedHead.toPoint()),
          "Localized repaint regions must include every visible head.");

    // Sample fixed phases without scheduling another timer per ribbon.
    meteor.m_progress = .03;
    check(meteor.visibleMeteorCount() == 1, "The leading ribbon must depart first.");
    meteor.m_progress = .12;
    check(meteor.visibleMeteorCount() == 2, "Companions must depart in a staggered cadence.");
    meteor.m_progress = .48;
    check(meteor.visibleMeteorCount() >= 4,
          "The middle of a transition must show several overlapping ribbons.");
    frame.fill(Qt::transparent);
    meteor.render(&frame, QPoint(), QRegion(), QWidget::DrawChildren);
    for (int index = 0; index < int(meteor.Ribbons.size()); ++index) {
        if (meteor.meteorOpacity(index) <= .01)
            continue;
        const QPointF head = meteor.headPosition(index);
        check(meteor.frameRegion().contains(head.toPoint()),
              "Localized repaint regions must include every visible ribbon.");
        check(meteor.direction(index).x() < 0 && meteor.direction(index).y() > 0,
              "Every ribbon must share the shower direction.");
    }
    if (const QString snapshotPath = qEnvironmentVariable("PHSRADIO_METEOR_SNAPSHOT");
        !snapshotPath.isEmpty()) {
        QImage snapshot(frame.size(), QImage::Format_ARGB32_Premultiplied);
        snapshot.fill(QColor(QStringLiteral("#03070d")));
        QPainter painter(&snapshot);
        painter.drawImage(QPoint(), frame);
        painter.end();
        check(snapshot.save(snapshotPath), "The requested shower preview must save successfully.");
    }
    check(meteor.Ribbons[0].headScale > meteor.Ribbons[5].headScale
              && meteor.Ribbons[0].brightness > meteor.Ribbons[5].brightness,
          "Distant ribbons must be thinner and fainter than the leading one.");
    check(paintedArea(meteor.frameRegion()) < host.width() * host.height() * 3 / 4,
          "A shower must repaint localized strips instead of the entire dock.");

    // Fire keeps the original fixed-beam flash: endpoints never move.
    meteor.setStyle(PlaybackMeteor::Style::Fire);
    check(meteor.durationMs() == PlaybackMeteor::FireDurationMs,
          "The fire dock must keep its original 1120 ms timeline.");
    meteor.m_progress = .48;
    frame.fill(Qt::transparent);
    meteor.render(&frame, QPoint(), QRegion(), QWidget::DrawChildren);
    bool warm = false;
    const QPoint head = meteor.headPosition().toPoint();
    for (int y = head.y() - 4; y <= head.y() + 4; ++y)
        for (int x = head.x() - 4; x <= head.x() + 4; ++x)
            if (frame.rect().contains(x, y)) {
                const QColor color = frame.pixelColor(x, y);
                warm |= color.alpha() > 30 && color.red() > color.blue() + 20;
            }
    check(warm, "The fire dock must render warm flame beams.");
    const QPointF fireHead = meteor.headPosition();
    meteor.m_progress = .60;
    check(meteor.headPosition() == fireHead,
          "The fire beam must flash in place instead of dragging a head.");
    check(meteor.visibleMeteorCount() == 7,
          "The original fire flash shows all seven beams together.");
    check(meteor.findChildren<QTimer *>().size() == 1,
          "Both styles must share one animation timer.");

    meteor.setStyle(PlaybackMeteor::Style::GreenLaser);
    meteor.trigger();
    check(meteor.progress() == 0 && meteor.isRunning(),
          "Repeated play events must safely restart the effect.");
    check(meteor.visibleMeteorCount() == 1 && !meteor.m_previousDirty.isEmpty(),
          "Restarting must immediately show a fresh ribbon.");
    host.resize(760, 180);
    QCoreApplication::processEvents();
    check(meteor.geometry() == host.rect() && meteor.isRunning(),
          "A running shower must adapt to dock resize without losing its animation.");
    waitForEvents(900);
    check(meteor.isRunning(), "The sweep must remain active across its overlap phase.");
    waitForEvents(700);
    check(meteor.progress() == 1 && !meteor.isRunning() && meteor.isHidden()
              && !meteor.m_timer->isActive() && meteor.visibleMeteorCount() == 0
              && meteor.m_previousDirty.isEmpty(),
          "The whole shower must fade, hide and stop all animation work afterwards.");
    meteor.trigger();
    host.hide();
    QCoreApplication::processEvents();
    check(!meteor.isRunning() && !meteor.m_timer->isActive() && meteor.m_previousDirty.isEmpty(),
          "A hidden host must stop effect work and discard all pending strips.");
    auto *temporaryHost = new QWidget;
    temporaryHost->resize(720, 156);
    QPointer<PlaybackMeteor> temporaryMeteor = new PlaybackMeteor(temporaryHost);
    temporaryMeteor->trigger();
    delete temporaryHost;
    check(temporaryMeteor.isNull(),
          "Deleting the dock must destroy its running effect and timer safely.");
    waitForEvents(40);
    return 0;
}
