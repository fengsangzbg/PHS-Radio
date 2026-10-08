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

    void start(quintptr window);
    void stop();
    // Includes asynchronous initialization, until stopped or an error occurs.
    bool isActive() const;
    QImage takeLatestFrame();
    QString errorString() const;
    QString diagnosticReport() const;
    // Application exit only: disallow new workers, cancel every live worker,
    // then wait for native cleanup within one total deadline. No QThread joins.
    static bool shutdownWorkers(int timeoutMs = 1000);

private:
    std::shared_ptr<NativeWindowCaptureDetail::State> m_state;
};
