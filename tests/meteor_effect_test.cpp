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

bool hasBlueGlowNear(const QImage &frame, const QPointF &position)
{
    const QPoint center(qRound(position.x()), qRound(position.y()));
    for (int y = center.y() - 3; y <= center.y() + 3; ++y)
        for (int x = center.x() - 3; x <= center.x() + 3; ++x) {
            if (!frame.rect().contains(x, y))
                continue;
            const QColor pixel = frame.pixelColor(x, y);
            if (pixel.alpha() > 15 && pixel.blue() > pixel.red() + 4
                && pixel.blue() >= pixel.green())
                return true;
        }
    return false;
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
    check(meteor.isHidden() && !meteor.isRunning(), "The effect must be idle until playback starts.");
    check(meteor.visibleMeteorCount() == 0 && meteor.Meteors.size() == 7,
          "An idle shower must keep its seven profiles dormant without creating extra widgets.");
    meteor.trigger();
    check(meteor.isRunning() && meteor.progress() == 0 && meteor.geometry() == host.rect(),
          "Triggering must start a full-size transparent overlay at the beginning of the path.");
    check(meteor.testAttribute(Qt::WA_TransparentForMouseEvents) && host.childAt(60, 80) == &underlying,
          "The overlay must allow clicks to reach controls beneath it.");
    check(meteor.m_timer->timerType() == Qt::PreciseTimer
              && meteor.m_timer->interval() == Aero::animationInterval(&host),
          "The meteor timer must match the screen refresh interval.");
    waitForEvents(160);
    check(meteor.progress() > 0 && meteor.progress() < 1,
          "The glowing head must move instead of remaining on its first frame.");
    const QPointF mid = meteor.headPosition();
    check(mid.x() < host.width() - 22 && mid.y() > 14, "The path must run from upper right toward lower left.");

    // Sample fixed phases without scheduling another timer per particle. The
    // rendered pixels prove that companions are drawn, rather than only counted.
    meteor.m_progress = .03;
    check(meteor.visibleMeteorCount() == 1, "The leading meteor must depart before its companions.");
    meteor.m_progress = .12;
    check(meteor.visibleMeteorCount() == 2, "Companions must depart in staggered waves.");
    meteor.m_progress = .48;
    check(meteor.visibleMeteorCount() == 7, "The middle of a play transition must show the full shower.");
    QImage frame(meteor.size(), QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    meteor.render(&frame, QPoint(), QRegion(), QWidget::DrawChildren);
    for (int index = 0; index < int(meteor.Meteors.size()); ++index) {
        const QPointF head = meteor.headPosition(index);
        check(hasBlueGlowNear(frame, head), "Every active meteor must paint a visible blue head.");
        check(meteor.direction(index).x() < 0 && meteor.direction(index).y() > 0,
              "All companions must travel toward the lower left.");
        check(meteor.frameRegion().contains(head.toPoint()),
              "Localized repaint regions must include every visible head.");
        for (int previous = 0; previous < index; ++previous)
            check(QLineF(head, meteor.headPosition(previous)).length() > 8,
                  "Meteor heads must be separated rather than drawn as overlapping copies.");
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
    check(QLineF(meteor.direction(0), meteor.direction(2)).length() > .01
              && meteor.Meteors[0].headScale > meteor.Meteors[5].headScale
              && meteor.Meteors[0].tailScale > meteor.Meteors[5].tailScale,
          "Companions must have different trajectories and shorter, smaller distant streaks.");
    qint64 dirtyArea = 0;
    for (const QRect &strip : meteor.frameRegion())
        dirtyArea += qint64(strip.width()) * strip.height();
    check(dirtyArea < qint64(host.width()) * host.height() * 3 / 4,
          "A shower must repaint localized strips instead of the entire dock.");
    meteor.m_progress = .92;
    check(meteor.visibleMeteorCount() == 1, "The last companion must trail the leading meteor's fade.");

    meteor.trigger();
    check(meteor.progress() == 0 && meteor.isRunning(), "Repeated play events must safely restart the effect.");
    check(meteor.visibleMeteorCount() == 0 && meteor.m_previousDirty.isEmpty()
              && meteor.findChildren<QTimer *>().size() == 1,
          "Restarting must clear old particle phases and retain one shared frame timer.");
    host.resize(760, 180);
    QCoreApplication::processEvents();
    check(meteor.geometry() == host.rect() && meteor.isRunning(),
          "A running meteor must adapt to dock resize without losing its animation.");
    waitForEvents(PlaybackMeteor::DurationMs + 110);
    check(meteor.progress() == 1 && !meteor.isRunning() && meteor.isHidden()
              && !meteor.m_timer->isActive() && meteor.visibleMeteorCount() == 0
              && meteor.m_previousDirty.isEmpty(),
          "The whole shower must fade, hide and stop all animation work after its last companion.");
    meteor.trigger();
    host.hide();
    QCoreApplication::processEvents();
    check(!meteor.isRunning() && !meteor.m_timer->isActive() && meteor.m_previousDirty.isEmpty(),
          "A hidden host must stop effect work and discard all pending particle strips.");
    auto *temporaryHost = new QWidget;
    temporaryHost->resize(720, 156);
    QPointer<PlaybackMeteor> temporaryMeteor = new PlaybackMeteor(temporaryHost);
    temporaryMeteor->trigger();
    delete temporaryHost;
    check(temporaryMeteor.isNull(), "Deleting the dock must destroy its running effect and timer safely.");
    waitForEvents(40);
    return 0;
}
