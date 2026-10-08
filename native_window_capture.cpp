#include "native_window_capture.h"

#include <QThread>
#include <QStringList>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cwchar>
#include <exception>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#endif

namespace NativeWindowCaptureDetail {
struct State {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> active{false};
    // active can become false before COM cleanup. Only the worker wrapper sets
    // finished after runCapture returns, with all its native resources released.
    std::atomic<bool> finished{true};
    mutable std::mutex mutex;
    std::condition_variable wake;
    QImage latest;
    QString error;
    QStringList diagnostic;
};

namespace {
struct WorkerRegistry {
    std::mutex mutex;
    std::vector<std::weak_ptr<State>> states;
    bool shuttingDown = false;
};

WorkerRegistry &workerRegistry()
{
    // Workers never access this registry during cleanup. Its destruction has
    // no QObject, QThread, event-loop, or native-resource lifetime dependency.
    static WorkerRegistry registry;
    return registry;
}

bool registerWorker(const std::shared_ptr<State> &state)
{
    auto &registry = workerRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (registry.shuttingDown)
        return false;
    auto entry = registry.states.begin();
    while (entry != registry.states.end()) {
        const auto live = entry->lock();
        if (!live || live->finished.load(std::memory_order_acquire))
            entry = registry.states.erase(entry);
        else
            ++entry;
    }
    state->finished.store(false, std::memory_order_release);
    registry.states.emplace_back(state);
    return true;
}

void cancel(const std::shared_ptr<State> &state)
{
    state->cancelled.store(true, std::memory_order_release);
    state->active.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->latest = {};
    }
    state->wake.notify_all();
}

void finishWorker(const std::shared_ptr<State> &state)
{
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->active.store(false, std::memory_order_release);
        state->finished.store(true, std::memory_order_release);
    }
    state->wake.notify_all();
}

void record(const std::shared_ptr<State> &state, const QString &message)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->diagnostic.size() >= 128)
        state->diagnostic.removeFirst();
    state->diagnostic.append(message);
}

void fail(const std::shared_ptr<State> &state, const QString &message)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->cancelled.load(std::memory_order_acquire)) {
        state->error = message;
        state->diagnostic.append(message);
    }
    state->latest = {};
    state->active.store(false, std::memory_order_release);
}

bool waitFor(const std::shared_ptr<State> &state, int milliseconds)
{
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->wake.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] {
        return state->cancelled.load(std::memory_order_acquire);
    });
}

#ifdef Q_OS_WIN
namespace Capture = ABI::Windows::Graphics::Capture;
namespace DirectX = ABI::Windows::Graphics::DirectX;
namespace Direct3D = ABI::Windows::Graphics::DirectX::Direct3D11;
using DxgiAccess = Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;
using CaptureSize = ABI::Windows::Graphics::SizeInt32;
constexpr auto pixelFormat = DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized;
constexpr int bufferCount = 2;

template<class T> class ComPtr final {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ComPtr(ComPtr &&other) noexcept : m_pointer(std::exchange(other.m_pointer, nullptr)) { }
    ComPtr &operator=(ComPtr &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_pointer = std::exchange(other.m_pointer, nullptr);
        }
        return *this;
    }
    T *get() const { return m_pointer; }
    T *operator->() const { return m_pointer; }
    explicit operator bool() const { return m_pointer != nullptr; }
    T **put() { reset(); return &m_pointer; }
    void **putVoid() { return reinterpret_cast<void **>(put()); }
    void reset()
    {
        if (T *pointer = std::exchange(m_pointer, nullptr))
            pointer->Release();
    }
    template<class U> HRESULT as(ComPtr<U> &other) const
    {
        return m_pointer ? m_pointer->QueryInterface(__uuidof(U), other.putVoid()) : E_POINTER;
    }
private:
    T *m_pointer = nullptr;
};

QString hresultText(HRESULT result)
{
    const QString code = QStringLiteral("0x%1").arg(quint32(result), 8, 16, QLatin1Char('0'));
    wchar_t *buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(result), 0,
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    const QString detail = count && buffer ? QString::fromWCharArray(buffer, int(count)).trimmed() : QString();
    if (buffer)
        LocalFree(buffer);
    return detail.isEmpty() ? code : code + QStringLiteral(" ") + detail;
}

struct CaptureFailure { QString message; };

void check(HRESULT result, const QString &stage)
{
    if (FAILED(result))
        throw CaptureFailure{stage + QStringLiteral(": ") + hresultText(result)};
}

class HString final {
public:
    explicit HString(const wchar_t *value)
    {
        check(WindowsCreateString(value, UINT32(std::wcslen(value)), &m_value),
              QStringLiteral("WindowsCreateString"));
    }
    ~HString() { WindowsDeleteString(m_value); }
    HSTRING get() const { return m_value; }
private:
    HSTRING m_value = nullptr;
};

template<class T> ComPtr<T> factory(const wchar_t *className, const QString &stage)
{
    HString name(className);
    ComPtr<T> result;
    check(RoGetActivationFactory(name.get(), __uuidof(T), result.putVoid()), stage);
    return result;
}

void closeObject(IUnknown *object)
{
    if (!object)
        return;
    ComPtr<ABI::Windows::Foundation::IClosable> closable;
    if (SUCCEEDED(object->QueryInterface(__uuidof(ABI::Windows::Foundation::IClosable), closable.putVoid()))
        && closable)
        closable->Close();
}

struct CloseFrame final {
    Capture::IDirect3D11CaptureFrame *frame;
    ~CloseFrame() { closeObject(frame); }
};

struct CaptureResources final {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<Direct3D::IDirect3DDevice> captureDevice;
    ComPtr<Capture::IGraphicsCaptureItem> item;
    ComPtr<Capture::IDirect3D11CaptureFramePool> pool;
    ComPtr<Capture::IGraphicsCaptureSession> session;
    ComPtr<ID3D11Texture2D> staging;
    CaptureSize poolSize{};
    QSize stagingSize;
    ~CaptureResources()
    {
        // Close before releasing any device; this destructor runs only on the
        // same MTA worker that created these objects, even on partial startup.
        closeObject(session.get());
        closeObject(pool.get());
    }
};

ComPtr<IDXGIAdapter1> monitorAdapter(HWND window, const std::shared_ptr<State> &state)
{
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> dxgi;
    HRESULT result = CreateDXGIFactory1(__uuidof(IDXGIFactory1), dxgi.putVoid());
    if (FAILED(result)) {
        record(state, QStringLiteral("Monitor adapter/CreateDXGIFactory1: ") + hresultText(result));
        return {};
    }
    for (UINT index = 0; !state->cancelled.load(std::memory_order_acquire); ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        result = dxgi->EnumAdapters1(index, adapter.put());
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(result)) {
            record(state, QStringLiteral("Monitor adapter/EnumAdapters1: ") + hresultText(result));
            break;
        }
        for (UINT outputIndex = 0; ; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            result = adapter->EnumOutputs(outputIndex, output.put());
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(result)) {
                record(state, QStringLiteral("Monitor adapter/EnumOutputs: ") + hresultText(result));
                break;
            }
            DXGI_OUTPUT_DESC description{};
            result = output->GetDesc(&description);
            if (SUCCEEDED(result) && description.Monitor == monitor) {
                DXGI_ADAPTER_DESC1 adapterDescription{};
                if (SUCCEEDED(adapter->GetDesc1(&adapterDescription)))
                    record(state, QStringLiteral("Monitor adapter: %1 (vendor %2, device %3)")
                        .arg(QString::fromWCharArray(adapterDescription.Description))
                        .arg(adapterDescription.VendorId, 4, 16, QLatin1Char('0'))
                        .arg(adapterDescription.DeviceId, 4, 16, QLatin1Char('0')));
                return adapter;
            }
        }
    }
    record(state, QStringLiteral("Monitor adapter unavailable; trying the default hardware adapter."));
    return {};
}

std::unique_ptr<CaptureResources> initializeCapture(HWND window, IDXGIAdapter1 *adapter,
    Capture::IDirect3D11CaptureFramePoolStatics2 *poolFactory, const std::shared_ptr<State> &state,
    const QString &route)
{
    auto resources = std::make_unique<CaptureResources>();
    record(state, route + QStringLiteral("/D3D11CreateDevice (BGRA_SUPPORT)"));
    check(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        resources->device.put(), nullptr, resources->context.put()),
        route + QStringLiteral("/D3D11CreateDevice"));

    record(state, route + QStringLiteral("/CreateForWindow"));
    auto interop = factory<IGraphicsCaptureItemInterop>(L"Windows.Graphics.Capture.GraphicsCaptureItem",
        route + QStringLiteral("/GraphicsCaptureItem activation"));
    HRESULT result = E_INVALIDARG;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (state->cancelled.load(std::memory_order_acquire))
            return {};
        if (!IsWindow(window))
            throw CaptureFailure{route + QStringLiteral("/CreateForWindow: target window was closed.")};
        result = interop->CreateForWindow(window, __uuidof(Capture::IGraphicsCaptureItem), resources->item.putVoid());
        if (result != E_INVALIDARG)
            break;
        record(state, route + QStringLiteral("/CreateForWindow attempt %1: ").arg(attempt + 1) + hresultText(result));
        if (attempt != 4 && waitFor(state, 100))
            return {};
    }
    check(result, route + QStringLiteral("/CreateForWindow"));
    check(resources->item->get_Size(&resources->poolSize), route + QStringLiteral("/GraphicsCaptureItem.Size"));
    if (resources->poolSize.Width <= 0 || resources->poolSize.Height <= 0)
        throw CaptureFailure{route + QStringLiteral("/GraphicsCaptureItem.Size: the capture surface is empty.")};

    ComPtr<IDXGIDevice> dxgiDevice;
    check(resources->device.as(dxgiDevice), route + QStringLiteral("/QueryInterface IDXGIDevice"));
    ComPtr<IInspectable> inspectableDevice;
    check(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectableDevice.put()),
          route + QStringLiteral("/CreateDirect3D11DeviceFromDXGIDevice"));
    check(inspectableDevice.as(resources->captureDevice), route + QStringLiteral("/QueryInterface IDirect3DDevice"));

    record(state, route + QStringLiteral("/CreateFreeThreaded (BGRA8, 2 buffers, %1x%2)")
        .arg(resources->poolSize.Width).arg(resources->poolSize.Height));
    check(poolFactory->CreateFreeThreaded(resources->captureDevice.get(), pixelFormat, bufferCount,
        resources->poolSize, resources->pool.put()), route + QStringLiteral("/CreateFreeThreaded"));
    check(resources->pool->CreateCaptureSession(resources->item.get(), resources->session.put()),
          route + QStringLiteral("/CreateCaptureSession"));

    // Optional properties may fail due to OS version or policy. They must not
    // turn a working core capture session into a terminal initialization error.
    ComPtr<Capture::IGraphicsCaptureSession2> cursor;
    result = resources->session.as(cursor);
    if (SUCCEEDED(result))
        result = cursor->put_IsCursorCaptureEnabled(false);
    record(state, route + QStringLiteral("/Optional cursor suppression: ") + hresultText(result));
    ComPtr<Capture::IGraphicsCaptureSession3> border;
    result = resources->session.as(border);
    if (SUCCEEDED(result))
        result = border->put_IsBorderRequired(false);
    record(state, route + QStringLiteral("/Optional border suppression: ") + hresultText(result));

    if (state->cancelled.load(std::memory_order_acquire))
        return {};
    record(state, route + QStringLiteral("/StartCapture"));
    check(resources->session->StartCapture(), route + QStringLiteral("/StartCapture"));
    record(state, route + QStringLiteral("/Capture session started."));
    return resources;
}

QImage readFrame(CaptureResources &resources, Capture::IDirect3D11CaptureFrame *frame)
{
    ComPtr<Direct3D::IDirect3DSurface> surface;
    check(frame->get_Surface(surface.put()), QStringLiteral("Frame.Surface"));
    ComPtr<DxgiAccess> access;
    check(surface.as(access), QStringLiteral("Frame.QueryInterface IDirect3DDxgiInterfaceAccess"));
    ComPtr<ID3D11Texture2D> texture;
    check(access->GetInterface(__uuidof(ID3D11Texture2D), texture.putVoid()),
          QStringLiteral("Frame.GetInterface ID3D11Texture2D"));
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    const int width = resources.poolSize.Width;
    const int height = resources.poolSize.Height;
    if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        throw CaptureFailure{QStringLiteral("Frame texture validation: expected BGRA8, got DXGI format %1.")
            .arg(int(description.Format))};
    if (width <= 0 || height <= 0 || UINT(width) > description.Width || UINT(height) > description.Height
        || width > std::numeric_limits<int>::max() / 4)
        throw CaptureFailure{QStringLiteral("Frame texture validation: invalid content dimensions.")};
    const QSize size(width, height);
    if (!resources.staging || resources.stagingSize != size) {
        D3D11_TEXTURE2D_DESC stagingDescription{};
        stagingDescription.Width = UINT(width);
        stagingDescription.Height = UINT(height);
        stagingDescription.MipLevels = 1;
        stagingDescription.ArraySize = 1;
        stagingDescription.Format = description.Format;
        stagingDescription.SampleDesc.Count = 1;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(resources.device->CreateTexture2D(&stagingDescription, nullptr, resources.staging.put()),
              QStringLiteral("Frame.CreateTexture2D staging"));
        resources.stagingSize = size;
    }
    const D3D11_BOX source{0, 0, 0, UINT(width), UINT(height), 1};
    resources.context->CopySubresourceRegion(resources.staging.get(), 0, 0, 0, 0, texture.get(), 0, &source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(resources.context->Map(resources.staging.get(), 0, D3D11_MAP_READ, 0, &mapped),
          QStringLiteral("Frame.Map staging"));
    struct Unmap final {
        CaptureResources &resources;
        ~Unmap() { resources.context->Unmap(resources.staging.get(), 0); }
    } unmap{resources};
    if (!mapped.pData || mapped.RowPitch < UINT(width) * 4)
        throw CaptureFailure{QStringLiteral("Frame.Map validation: invalid mapped row pitch.")};
    QImage image(size, QImage::Format_RGB32);
    if (image.isNull())
        throw CaptureFailure{QStringLiteral("Frame image allocation failed at %1x%2.").arg(width).arg(height)};
    for (int row = 0; row < height; ++row) {
        auto *destination = image.scanLine(row);
        const auto *sourceRow = static_cast<const unsigned char *>(mapped.pData) + size_t(row) * mapped.RowPitch;
        std::memcpy(destination, sourceRow, size_t(width) * 4);
        // Constant native-window opacity is only for desktop presentation.
        // The player background consumes opaque RGB, independently of alpha.
        for (int column = 0; column < width; ++column)
            destination[size_t(column) * 4 + 3] = 255;
    }
    return image;
}

void runCapture(const std::shared_ptr<State> &state, quintptr nativeWindow)
{
    const HWND window = reinterpret_cast<HWND>(nativeWindow);
    const HRESULT apartmentResult = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(apartmentResult)) {
        fail(state, QStringLiteral("RoInitialize MTA: ") + hresultText(apartmentResult));
        return;
    }
    struct Apartment final { ~Apartment() { RoUninitialize(); } } apartment;
    try {
        if (state->cancelled.load(std::memory_order_acquire))
            return;
        if (!window || !IsWindow(window))
            throw CaptureFailure{QStringLiteral("Native WGC: invalid or closed target window.")};

        // RtlGetVersion gives the actual build even without a version manifest.
        using VersionFunction = LONG (WINAPI *)(OSVERSIONINFOW *);
        const auto versionFunction = reinterpret_cast<VersionFunction>(GetProcAddress(
            GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (versionFunction && versionFunction(&version) >= 0) {
            record(state, QStringLiteral("Windows %1.%2 build %3")
                .arg(version.dwMajorVersion).arg(version.dwMinorVersion).arg(version.dwBuildNumber));
            if (version.dwMajorVersion < 10 || (version.dwMajorVersion == 10 && version.dwBuildNumber < 18362))
                throw CaptureFailure{QStringLiteral("Native WGC requires Windows 10 version 1903 (build 18362) or later.")};
        }
        record(state, QStringLiteral("Native WGC: BGRA8 / 2 buffers / MTA; HWND 0x%1")
            .arg(nativeWindow, 0, 16));
        record(state, QStringLiteral("GraphicsCaptureSession.IsSupported"));
        auto support = factory<Capture::IGraphicsCaptureSessionStatics>(L"Windows.Graphics.Capture.GraphicsCaptureSession",
            QStringLiteral("GraphicsCaptureSession activation"));
        boolean supported = false;
        check(support->IsSupported(&supported), QStringLiteral("GraphicsCaptureSession.IsSupported"));
        if (!supported)
            throw CaptureFailure{QStringLiteral("Windows Graphics Capture is unsupported on this device.")};
        // Request the free-threaded API explicitly; never use a DispatcherQueue
        // frame pool on this worker, which deliberately has no Qt event loop.
        auto poolFactory = factory<Capture::IDirect3D11CaptureFramePoolStatics2>(
            L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",
            QStringLiteral("Direct3D11CaptureFramePool free-threaded API activation"));
        auto adapter = monitorAdapter(window, state);
        std::unique_ptr<CaptureResources> resources;
        QString lastFailure;
        if (adapter && !state->cancelled.load(std::memory_order_acquire)) {
            try {
                resources = initializeCapture(window, adapter.get(), poolFactory.get(), state,
                    QStringLiteral("Monitor hardware"));
            } catch (const CaptureFailure &error) {
                lastFailure = error.message;
                record(state, lastFailure);
            }
        }
        if (!resources && !state->cancelled.load(std::memory_order_acquire)) {
            try {
                resources = initializeCapture(window, nullptr, poolFactory.get(), state,
                    QStringLiteral("Default hardware"));
            } catch (const CaptureFailure &error) {
                lastFailure = error.message;
                record(state, lastFailure);
            }
        }
        if (state->cancelled.load(std::memory_order_acquire))
            return;
        if (!resources)
            throw CaptureFailure{lastFailure.isEmpty() ? QStringLiteral("Native WGC initialization failed.") : lastFailure};

        bool firstFrame = true;
        while (!state->cancelled.load(std::memory_order_acquire)) {
            if (!IsWindow(window))
                throw CaptureFailure{QStringLiteral("Native WGC: target window was closed.")};
            ComPtr<Capture::IDirect3D11CaptureFrame> frame;
            check(resources->pool->TryGetNextFrame(frame.put()), QStringLiteral("TryGetNextFrame"));
            if (frame) {
                CloseFrame close{frame.get()};
                CaptureSize content{};
                check(frame->get_ContentSize(&content), QStringLiteral("Frame.ContentSize"));
                if (content.Width > 0 && content.Height > 0) {
                    if (content.Width != resources->poolSize.Width || content.Height != resources->poolSize.Height) {
                        // Return the checked-out frame before Recreate invalidates
                        // its surfaces. Clear queued pixels from the previous size.
                        closeObject(frame.get());
                        close.frame = nullptr;
                        frame.reset();
                        {
                            std::lock_guard<std::mutex> lock(state->mutex);
                            state->latest = {};
                        }
                        record(state, QStringLiteral("FramePool.Recreate %1x%2").arg(content.Width).arg(content.Height));
                        check(resources->pool->Recreate(resources->captureDevice.get(), pixelFormat, bufferCount, content),
                              QStringLiteral("FramePool.Recreate"));
                        resources->poolSize = content;
                        resources->staging.reset();
                    } else {
                        QImage image = readFrame(*resources, frame.get());
                        if (firstFrame) {
                            record(state, QStringLiteral("First frame: %1x%2 opaque BGRA").arg(image.width()).arg(image.height()));
                            firstFrame = false;
                        }
                        std::lock_guard<std::mutex> lock(state->mutex);
                        if (!state->cancelled.load(std::memory_order_acquire))
                            state->latest = std::move(image);
                    }
                }
            }
            if (waitFor(state, 8))
                break;
        }
    } catch (const CaptureFailure &error) {
        fail(state, error.message);
    } catch (const std::exception &error) {
        fail(state, QStringLiteral("Native WGC worker: ") + QString::fromLocal8Bit(error.what()));
    } catch (...) {
        fail(state, QStringLiteral("Native WGC worker: unexpected failure."));
    }
    state->active.store(false, std::memory_order_release);
}
#endif
} // namespace
} // namespace NativeWindowCaptureDetail

NativeWindowCapture::NativeWindowCapture() : m_state(std::make_shared<NativeWindowCaptureDetail::State>()) { }

NativeWindowCapture::~NativeWindowCapture() { stop(); }

void NativeWindowCapture::start(quintptr window)
{
    stop();
    auto state = std::make_shared<NativeWindowCaptureDetail::State>();
    m_state = state;
#ifdef Q_OS_WIN
    if (!window || !IsWindow(reinterpret_cast<HWND>(window))) {
        NativeWindowCaptureDetail::fail(state, QStringLiteral("Native WGC: invalid target window handle."));
        return;
    }
    state->active.store(true, std::memory_order_release);
    if (!NativeWindowCaptureDetail::registerWorker(state)) {
        NativeWindowCaptureDetail::fail(state, QStringLiteral("Native WGC workers are shutting down."));
        return;
    }
    // There is no owner pointer or GUI callback in this closure. A cancelled
    // worker owns only its isolated state and may finish after this object dies.
    QThread *worker = QThread::create([state, window] {
        struct Finished final {
            std::shared_ptr<NativeWindowCaptureDetail::State> state;
            ~Finished() { NativeWindowCaptureDetail::finishWorker(state); }
        } finished{state};
        NativeWindowCaptureDetail::runCapture(state, window);
    });
    worker->setObjectName(QStringLiteral("nativeWallpaperCapture"));
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
#else
    Q_UNUSED(window);
    NativeWindowCaptureDetail::fail(state, QStringLiteral("Native window capture is available only on Windows."));
#endif
}

void NativeWindowCapture::stop()
{
    NativeWindowCaptureDetail::cancel(m_state);
}

bool NativeWindowCapture::isActive() const
{
    return m_state->active.load(std::memory_order_acquire)
        && !m_state->cancelled.load(std::memory_order_acquire);
}

QImage NativeWindowCapture::takeLatestFrame()
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->cancelled.load(std::memory_order_acquire))
        return {};
    return std::exchange(m_state->latest, QImage{});
}

QString NativeWindowCapture::errorString() const
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->error;
}

QString NativeWindowCapture::diagnosticReport() const
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->diagnostic.join(QLatin1Char('\n'));
}

bool NativeWindowCapture::shutdownWorkers(int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0);
    std::vector<std::shared_ptr<NativeWindowCaptureDetail::State>> states;
    {
        auto &registry = NativeWindowCaptureDetail::workerRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        registry.shuttingDown = true;
        states.reserve(registry.states.size());
        for (const auto &entry : registry.states) {
            if (auto state = entry.lock())
                states.push_back(std::move(state));
        }
    }
    // Cancel everyone before waiting for anyone; each wait uses the same
    // absolute deadline, even when multiple old generations are still alive.
    for (const auto &state : states)
        NativeWindowCaptureDetail::cancel(state);
    for (const auto &state : states) {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->wake.wait_until(lock, deadline, [&] {
            return state->finished.load(std::memory_order_acquire);
        }))
            return false;
    }
    return true;
}
