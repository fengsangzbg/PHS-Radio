#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QRegion>
#include <QWidget>
#include <array>

class QTimer;
class QPainter;

// Playback flourish for the dock. Two deliberately different languages:
//   * GreenLaser (the default water dock) draws long glass ribbons that sweep
//     one at a time, lit like Frutiger Aero glass with a spectral rim.
//   * Fire keeps the original fixed-beam flash, unchanged.
class PlaybackMeteor final : public QWidget {
public:
    explicit PlaybackMeteor(QWidget *host);
    enum class Style { GreenLaser, Fire };
    void setStyle(Style style);
    Style style() const { return m_style; }
    void trigger();
    bool isRunning() const;
    qreal progress() const;
    int visibleMeteorCount() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
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

    int durationMs() const;
    int beamCount() const;
    const Meteor &beam(int index) const;
    qreal meteorProgress(int index) const;
    qreal meteorOpacity(int index) const;
    QPointF mapped(const QPointF &normalized) const;
    QPointF pointAt(int index, qreal travel) const;
    QPointF headPosition(int index = 0) const;
    QPointF tailPosition(int index = 0) const;
    QPointF direction(int index = 0) const;
    QRegion frameRegion() const;
    QRect frameBounds() const;
    void advance();
    void ensureSprites();
    void dropSprites();
    void paintRibbons(QPainter &painter);
    void paintLegacyFire(QPainter &painter);

    QWidget *m_host;
    QTimer *m_timer;
    QElapsedTimer m_clock;
    QRegion m_previousDirty;
    qreal m_progress = 0;
    bool m_running = false;
    Style m_style = Style::GreenLaser;
    qreal m_spriteDpr = 0;
    QImage m_tailSprite;
    QImage m_headSprite;

    static constexpr int RibbonDurationMs = 1290;
    static constexpr int FireDurationMs = 1120;
    static constexpr qreal TailSpan = .70;
    static const std::array<Meteor, 7> Ribbons;
    static const std::array<Meteor, 7> FireBeams;
};
