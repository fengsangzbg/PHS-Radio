#pragma once

#include <QImage>
#include <QString>
#include <QtGlobal>
#include <memory>

namespace NativeWindowCaptureDetail { struct State; }

// An independent Windows Graphics Capture path. All native resources and pixel
// readback belong to an MTA worker; the owner only consumes the latest image.
// Use the public methods from one owner thread. stop() never waits for the GPU.
class NativeWindowCapture final {
public:
    NativeWindowCapture();
    ~NativeWindowCapture();
    NativeWindowCapture(const NativeWindowCapture &) = delete;
    NativeWindowCapture &operator=(const NativeWindowCapture &) = delete;

    // verificationWindow is used only after target-item creation fails. It
    // must be a visible top-level window belonging to this process; no frames
    // are captured from it. monitorWindow optionally selects the display/GPU
    // for an intended destination window in this process; it is never captured
    // and need not be visible. An invalid hint falls back to the target window.
    void start(quintptr window, quintptr verificationWindow = 0, quintptr monitorWindow = 0);
    void stop();
    // Includes asynchronous initialization, until stopped or an error occurs.
    bool isActive() const;
    QImage takeLatestFrame();
    QString errorString() const;
    // True only when creating the target capture item finally returned
    // E_INVALIDARG. Device, frame-pool and rendering errors leave this false.
    bool targetWindowRejected() const;
    // Target capture item has been created, before device or frame-pool setup.
    // A stopped/cancelled capture never reports readiness.
    bool targetItemReady() const;
    QString diagnosticReport() const;
    // Application exit only: disallow new workers, cancel every live worker,
    // then wait for native cleanup within one total deadline. No QThread joins.
    static bool shutdownWorkers(int timeoutMs = 1000);

private:
    std::shared_ptr<NativeWindowCaptureDetail::State> m_state;
};
