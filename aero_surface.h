#pragma once

#include "background_theme.h"
#include "aero_animation.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <memory>

class QPainter;
class QTimer;
class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QMovie;
class LatestVideoFrame;
template <typename T> class QFutureWatcher;

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
    // Only panels sampling the blurred wallpaper need another repaint when
    // its worker finishes. Clear song-card viewports stay on source cadence.
    void registerBlurredBackgroundConsumer(QWidget *widget);
    std::function<void(const QString &)> onBackgroundError;
    // All dynamic background backends share the same visibility/activity policy.
    std::function<void(bool)> onBackgroundActivityChanged;
    bool backgroundIsActive() const;
    // Synchronize activity after starting a backend or returning from a dialog.
    bool synchronizeBackgroundActivity();
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
    QObject *m_displayObserver = nullptr;
    bool m_windowDeactivated = false;
    bool m_backgroundActive = false;
    bool m_backgroundActivitySyncPending = false;
    QTimer *m_motionTimer;
    QElapsedTimer m_frameClock;
    QElapsedTimer m_presentationClock;
    Aero::FrameSchedule m_motionSchedule;
    Aero::FrameSchedule m_videoSchedule;
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
    QFutureWatcher<QImage> *m_backgroundBlurWatcher;
    QTimer *m_backgroundBlurTimer;
    QTimer *m_backgroundBlurPresentTimer;
    QElapsedTimer m_backgroundBlurClock;
    quint64 m_backgroundBlurGeneration = 0;
    quint64 m_backgroundBlurJobGeneration = 0;
    quint64 m_backgroundBlurJobFrameRevision = 0;
    QSize m_backgroundBlurJobSize;
    bool m_backgroundBlurBusy = false;
    bool m_backgroundBlurPending = false;
    QMediaPlayer *m_backgroundPlayer = nullptr;
    QAudioOutput *m_backgroundAudio = nullptr;
    QVideoSink *m_backgroundSink = nullptr;
    QTimer *m_videoFrameTimer;
    std::shared_ptr<LatestVideoFrame> m_videoFrames;
    QMovie *m_backgroundMovie = nullptr;
    QElapsedTimer m_backgroundFrameClock;
    bool m_backgroundFailed = false;
    QVector<QPointer<QWidget>> m_backgroundConsumers;
    QVector<QPointer<QWidget>> m_backgroundBlurConsumers;
    void paintWaterFrame(QPainter &painter, const QRectF &bounds,
                         const QPointF &worldOffset) const;
    void updateAnimationState();
    void scheduleBackgroundActivitySync();
    void updateDisplayRate();
    void publishVideoFrame();
    void advanceWater();
    void repaintBackground();
    void repaintGlassConsumers();
    void invalidateBackgroundBlur();
    void requestBackgroundBlur();
    void ensureBackgroundTexture() const;
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
