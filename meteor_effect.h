#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QRegion>
#include <QWidget>
#include <array>

class QTimer;

class PlaybackMeteor final : public QWidget {
public:
    explicit PlaybackMeteor(QWidget *host);
    void trigger();
    bool isRunning() const;
    qreal progress() const;
    int visibleMeteorCount() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void advance();
    void rebuildSprites();
    struct Meteor {
        QPointF start;
        QPointF end;
        int delayMs;
        int travelMs;
        qreal headScale;
        qreal tailScale;
        qreal brightness;
        qreal bend;
    };
    qreal meteorProgress(int index) const;
    qreal meteorOpacity(int index) const;
    QPointF headPosition(int index = 0) const;
    QPointF direction(int index = 0) const;
    QRegion frameRegion() const;
    QRect frameBounds() const;

    QWidget *m_host;
    QTimer *m_timer;
    QElapsedTimer m_clock;
    QPixmap m_headSprite;
    QPixmap m_tailSprite;
    QRegion m_previousDirty;
    qreal m_cachedDpr = 0;
    qreal m_progress = 0;
    qreal m_tailLength = 0;
    bool m_running = false;
    // A single clock and two shared sprites animate the complete shower.
    static constexpr int DurationMs = 1120;
    static const std::array<Meteor, 7> Meteors;
};
