#pragma once

#include "background_theme.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QWidget>
#include <functional>

class QPainter;
class QTimer;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QMovie;

// Native vector reflections keep the surface crisp at every window size.
class AeroSurface final : public QWidget {
public:
    explicit AeroSurface(QWidget *parent = nullptr);
    ~AeroSurface() override;
    void setAccentColor(const QColor &color);
    bool setBackground(const BackgroundTheme &theme, QString *error = nullptr);
    BackgroundTheme backgroundTheme() const;
    // Frames from the application's own Wallpaper Engine window, never the desktop.
    void setBackgroundFrame(const QImage &frame);
    void clearBackgroundFrame();
    // Liquid painters register their real viewport/panel once, so frame updates
    // target visible sampling widgets without walking an item model.
    void registerBackgroundConsumer(QWidget *widget);
    std::function<void(const QString &)> onBackgroundError;
    // All dynamic background backends share the same visibility/activity policy.
    std::function<void(bool)> onBackgroundActivityChanged;
    bool backgroundIsActive() const;
    void paintWater(QPainter &painter, const QRectF &bounds, const QPointF &worldOffset = {}) const;
    void paintBlurredBackground(QPainter &painter, const QRectF &bounds,
                                const QPointF &worldOffset = {}) const;
    quint64 backdropRevision() const;
    bool animationRunning() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QColor m_accent;
    QPointer<QWidget> m_window;
    bool m_windowDeactivated = false;
    bool m_backgroundActive = false;
    QTimer *m_motionTimer;
    QElapsedTimer m_frameClock;
    qreal m_phase = 0;
    quint64 m_revision = 0;
    mutable QImage m_waterFrame;
    mutable QImage m_waterTexture;
    mutable qreal m_waterFramePhase = -1;
    mutable quint64 m_waterFrameRevision = 0;
    BackgroundTheme m_background;
    QImage m_backgroundFrame;
    mutable QImage m_backgroundTexture;
    mutable qreal m_backgroundTextureDpr = 0;
    quint64 m_backgroundFrameRevision = 0;
    mutable quint64 m_backgroundTextureFrameRevision = 0;
    mutable QImage m_backgroundBlurred;
    mutable quint64 m_backgroundBlurredFrameRevision = 0;
    QMediaPlayer *m_backgroundPlayer = nullptr;
    QAudioOutput *m_backgroundAudio = nullptr;
    QVideoSink *m_backgroundSink = nullptr;
    QMovie *m_backgroundMovie = nullptr;
    QElapsedTimer m_backgroundFrameClock;
    bool m_backgroundFailed = false;
    QVector<QPointer<QWidget>> m_backgroundConsumers;
    void paintWaterFrame(QPainter &painter, const QRectF &bounds,
                         const QPointF &worldOffset) const;
    void updateAnimationState();
    void advanceWater();
    void repaintBackground();
    bool paintCustomBackground(QPainter &painter, const QRectF &bounds,
                               const QPointF &worldOffset) const;
};

class AeroPanel final : public QWidget {
public:
    explicit AeroPanel(QWidget *parent = nullptr);
    void setAccentColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_accent;
};
