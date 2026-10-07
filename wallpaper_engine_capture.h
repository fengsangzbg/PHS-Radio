#pragma once

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVideoFrame>
#include <functional>

class QMediaCaptureSession;
class QWindowCapture;
class QVideoSink;
class QProcess;

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
    bool isRunning() const;
    bool isRequested() const { return m_requested; }
    QString windowName() const { return m_windowName; }
    void setOwnerWindow(quintptr window) { m_ownerWindow = window; }

    std::function<void(const QImage &)> onFrame;
    std::function<void(const QString &)> onError;
    std::function<void(const QString &)> onStatusChanged;

private:
    void findWindow();
    void publishFrame();
    void fail(const QString &message);
    void parkWindow();
    bool muteWallpaper();
    QMediaCaptureSession *m_session = nullptr;
    QWindowCapture *m_capture = nullptr;
    QVideoSink *m_sink = nullptr;
    QProcess *m_command = nullptr;
    QTimer m_findTimer;
    QTimer m_frameTimer;
    QTimer m_timeout;
    QString m_executable;
    QString m_windowName;
    QString m_mutedProjectDirectory;
    QVideoFrame m_latestFrame;
    QSize m_renderSize;
    quintptr m_nativeWindow = 0;
    quintptr m_ownerWindow = 0;
    quintptr m_hiddenOwner = 0;
    bool m_hasFrame = false;
    bool m_requested = false;
    bool m_paused = false;
    bool m_announced = false;
};
