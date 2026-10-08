#pragma once

#include <QObject>
#include <QImage>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVideoFrame>
#include <functional>
#include <memory>
#include "latest_video_frame.h"
#include "aero_animation.h"

class QMediaCaptureSession;
class QWindowCapture;
class QVideoSink;
class QProcess;
class QThread;
class QWidget;

// Uses a named Wallpaper Engine pop-out. Never opens or closes desktop wallpapers.
class WallpaperEngineCapture final : public QObject {
public:
    explicit WallpaperEngineCapture(QObject *parent = nullptr);
    ~WallpaperEngineCapture() override;
    void start(const QString &executable, const QString &projectPath, QSize size);
    void stop();
    void setPaused(bool paused);
    void setRenderSize(QSize pixels);
    void setFrameInterval(int milliseconds);
    // Capture/presentation budget only; the renderer still uses WE's setting.
    void setFrameRate(qreal framesPerSecond);
    // Read-only diagnostic of the current user's installed WE configuration.
    // Returns 0 when unavailable; never changes global/desktop settings.
    static int configuredFrameRate(const QString &executable, QString *error = nullptr);
    bool isRunning() const;
    bool isRequested() const { return m_requested; }
    QString windowName() const { return m_windowName; }
    void setOwnerWindow(quintptr window) { m_ownerWindow = window; }

    std::function<void(const QImage &)> onFrame;
    std::function<void(const QString &)> onError;
    std::function<void(const QString &)> onStatusChanged;

private:
    void findWindow();
    bool captureWindowReady();
    void resetCaptureCandidate();
    void handleCaptureError(int error, const QString &detail);
    void publishFrame();
    void startRgbConversion(const QVideoFrame &frame);
    void resetConvertedFrames();
    void drainSinkFrames();
    void fail(const QString &message);
    bool parkWindow();
    bool muteWallpaper();
    QMediaCaptureSession *m_session = nullptr;
    QWindowCapture *m_capture = nullptr;
    QVideoSink *m_sink = nullptr;
    QThread *m_sinkThread = nullptr;
    QProcess *m_command = nullptr;
    QObject *m_displayObserver = nullptr;
    QTimer m_findTimer;
    QTimer m_frameTimer;
    QTimer m_guardTimer;
    QTimer m_timeout;
    QElapsedTimer m_frameClock;
    Aero::FrameSchedule m_frameSchedule;
    QString m_executable;
    QString m_windowName;
    QString m_mutedProjectDirectory;
    std::shared_ptr<LatestVideoFrame> m_frames = std::make_shared<LatestVideoFrame>();
    QFutureWatcher<QImage> *m_rgbWatcher = nullptr;
    QImage m_pendingImage;
    quint64 m_conversionGeneration = 0;
    quint64 m_rgbJobGeneration = 0;
    bool m_rgbConversionBusy = false;
    QSize m_renderSize;
    quintptr m_nativeWindow = 0;
    quintptr m_ownerWindow = 0;
    quintptr m_hiddenOwner = 0;
    quintptr m_candidateWindow = 0;
    QSize m_candidateSize;
    int m_candidateStablePolls = 0;
    quint64 m_captureGeneration = 0;
    int m_captureRecoveryAttempts = 0;
    bool m_captureErrorPending = false;
    bool m_requested = false;
    bool m_paused = false;
    bool m_announced = false;
};
