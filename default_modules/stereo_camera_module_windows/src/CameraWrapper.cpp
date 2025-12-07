#include "CameraWrapper.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <dshow.h>     // for IAMCameraControl
#pragma comment(lib, "strmiids.lib")

using Microsoft::WRL::ComPtr;

#include <comdef.h>

static std::string hrStr(HRESULT hr)
{
    _com_error err(hr);
    auto* tmsg = err.ErrorMessage();   // TCHAR*

#ifdef _UNICODE
    const wchar_t* msgW = tmsg;

    int len = WideCharToMultiByte(CP_UTF8, 0, msgW, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "Unknown COM error";

    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, msgW, -1, out.data(), len, nullptr, nullptr);
    return out;

#else
    const char* msgA = tmsg;
    return std::string(msgA ? msgA : "Unknown COM error");
#endif
}



static void HR(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + " failed: hr=0x" +
                                 std::to_string((unsigned)hr) + " (" + hrStr(hr) + ")");
    }
}


namespace {

class ComInitializer {
public:
    ComInitializer() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
            throw std::runtime_error("CoInitializeEx failed");
        hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) {
            CoUninitialize();
            throw std::runtime_error("MFStartup failed");
        }
    }
    ~ComInitializer() {
        MFShutdown();
        CoUninitialize();
    }
};

CompressionFormat subtypeToCompression(const GUID& g) {
    if (g == MFVideoFormat_MJPG) return CompressionFormat::MJPEG;
    if (g == MFVideoFormat_YUY2) return CompressionFormat::YUY2;
    if (g == MFVideoFormat_RGB32) return CompressionFormat::RGB32;
    return CompressionFormat::Unknown;
}

GUID compressionToSubtype(CompressionFormat fmt) {
    switch (fmt) {
    case CompressionFormat::MJPEG: return MFVideoFormat_MJPG;
    case CompressionFormat::YUY2:  return MFVideoFormat_YUY2;
    case CompressionFormat::RGB32: return MFVideoFormat_RGB32;
    default: return MFVideoFormat_RGB32; // decoder-converted output
    }
}

std::wstring toW(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}

std::string toU8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

struct MediaTypeInfo {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    CompressionFormat fmt = CompressionFormat::Unknown;
};

MediaTypeInfo parseMediaType(IMFMediaType* type) {
    MediaTypeInfo out;
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);

    UINT32 num = 0, den = 0;
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) && den != 0) {
        out.fps = static_cast<double>(num) / static_cast<double>(den);
    }

    GUID sub = {};
    type->GetGUID(MF_MT_SUBTYPE, &sub);
    out.fmt = subtypeToCompression(sub);
    return out;
}

} // namespace

// --------- Camera enumeration ----------
std::vector<CameraInfo> enumerateCameras() {
    ComInitializer init;

    std::vector<CameraInfo> out;
    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(&attrs, 1))) return out;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFEnumDeviceSources(attrs.Get(), &devices, &count);
    if (FAILED(hr)) return out;

    const DWORD streamIndex = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IMFActivate> dev = devices[i];
        WCHAR* name = nullptr;
        UINT32 cch = 0;
        dev->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &cch);
        WCHAR* link = nullptr;
        UINT32 lch = 0;
        dev->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &lch);

        CameraInfo info;
        info.name = name ? toU8(name) : "Unknown";
        info.id = link ? toU8(link) : std::to_string(i);

        CoTaskMemFree(name);
        CoTaskMemFree(link);

        // Activate and probe native media types to fill modes.
        ComPtr<IMFMediaSource> source;
        if (SUCCEEDED(dev->ActivateObject(IID_PPV_ARGS(&source)))) {
            ComPtr<IMFSourceReader> reader;
            if (SUCCEEDED(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader))) {
                for (DWORD mtIndex = 0;; ++mtIndex) {
                    ComPtr<IMFMediaType> mt;
                    if (reader->GetNativeMediaType(streamIndex, mtIndex, &mt) != S_OK)
                        break;
                    auto infoMode = parseMediaType(mt.Get());
                    if (infoMode.width > 0 && infoMode.height > 0)
                        info.modes.push_back({infoMode.width, infoMode.height, infoMode.fps, infoMode.fmt});
                }
            }
            source->Shutdown();
        }
        out.push_back(std::move(info));
        dev->ShutdownObject();
    }

    CoTaskMemFree(devices);
    return out;
}

// --------- Camera impl ----------
struct Camera::Impl {
    ComInitializer com_;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    LONG stride = 0;
    const DWORD streamIndex = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

    Impl(const Config& cfg, int& outW, int& outH, double& outFps, CompressionFormat& outFmt) {
        // Create source by symbolic link
        ComPtr<IMFAttributes> attr;
        if (FAILED(MFCreateAttributes(&attr, 2)))
            throw std::runtime_error("MFCreateAttributes failed");

        attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        attr->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, toW(cfg.cameraId).c_str());

        if (FAILED(MFCreateDeviceSource(attr.Get(), &source)))
            throw std::runtime_error("MFCreateDeviceSource failed");

        // Build reader
        ComPtr<IMFAttributes> readerAttr;
        if (FAILED(MFCreateAttributes(&readerAttr, 1)))
            throw std::runtime_error("MFCreateAttributes failed");
        readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

        if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), readerAttr.Get(), &reader)))
            throw std::runtime_error("MFCreateSourceReaderFromMediaSource failed");

        // Choose media type: try to find native matching Config, else first native
        ComPtr<IMFMediaType> chosen;
        DWORD chosenIndex = 0;
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> mt;
            if (reader->GetNativeMediaType(streamIndex, i, &mt) != S_OK)
                break;
            auto info = parseMediaType(mt.Get());
            if (chosen == nullptr)
                chosen = mt, chosenIndex = i;
            if (cfg.width == info.width && cfg.height == info.height &&
                (cfg.format == CompressionFormat::Unknown || cfg.format == info.fmt)) {
                chosen = mt;
                chosenIndex = i;
                if (cfg.fps <= 0.0 || std::abs(cfg.fps - info.fps) < 0.1)
                    break;
            }
        }
        if (!chosen)
            throw std::runtime_error("No media type found");

        // Apply native type
        if (FAILED(reader->SetCurrentMediaType(streamIndex, nullptr, chosen.Get())))
            throw std::runtime_error("SetCurrentMediaType failed");

        auto chosenInfo = parseMediaType(chosen.Get());
        outW = chosenInfo.width;
        outH = chosenInfo.height;
        outFps = chosenInfo.fps;
        outFmt = chosenInfo.fmt;

        // Request RGB32 for easy display; the underlying device still runs chosen native type.
        ComPtr<IMFMediaType> rgb;
        MFCreateMediaType(&rgb);
        rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        MFSetAttributeSize(rgb.Get(), MF_MT_FRAME_SIZE, outW, outH);
        reader->SetCurrentMediaType(streamIndex, nullptr, rgb.Get());

        // Compute stride
        ComPtr<IMFMediaType> current;
        reader->GetCurrentMediaType(streamIndex, &current);
        LONG lStride = 0;
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, (UINT32*)&outW, (UINT32*)&outH);
        MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, outW, &lStride);
        stride = lStride;
        outFmt = CompressionFormat::RGB32;
    }

    ~Impl() {
        if (source) source->Shutdown();
    }
};

Camera::Camera(const Config& cfg)
{
    impl_ = std::move(std::make_unique<Impl>(cfg, width_, height_, fps_, format_));
    if (!setAutoExposure()) {
        std::cerr << "Warning: setAutoExposure not supported on this device.\n";
    }
}

Camera::Camera(Camera&& other) noexcept = default;
Camera& Camera::operator=(Camera&& other) noexcept = default;
Camera::~Camera() = default;

bool Camera::setExposure(long exposureValue) {
    if (!impl_ || !impl_->source) return false;

    ComPtr<IAMCameraControl> camCtrl;
    if (FAILED(impl_->source.As(&camCtrl))) {
        return false; // device doesn’t expose IAMCameraControl
    }

    long min = 0, max = 0, step = 0, defVal = 0, caps = 0;
    if (FAILED(camCtrl->GetRange(CameraControl_Exposure, &min, &max, &step, &defVal, &caps))) {
        return false;
    }

    long desired = std::clamp(exposureValue, min, max);
    return SUCCEEDED(camCtrl->Set(CameraControl_Exposure, desired, CameraControl_Flags_Manual));
}

bool Camera::setAutoExposure() {
    if (!impl_ || !impl_->source) return false;

    ComPtr<IAMCameraControl> camCtrl;
    if (FAILED(impl_->source.As(&camCtrl))) {
        return false; // device doesn’t expose IAMCameraControl
    }

    return SUCCEEDED(camCtrl->Set(CameraControl_Exposure, 0, CameraControl_Flags_Auto));
}

bool Camera::getAvailableExposureRange(ExposureRange& out) {
    if (!impl_ || !impl_->source) return false;

    ComPtr<IAMCameraControl> camCtrl;
    if (FAILED(impl_->source.As(&camCtrl))) {
        return false; // not supported
    }

    return SUCCEEDED(camCtrl->GetRange(CameraControl_Exposure, &out.min_, &out.max_, &out.step_, &out.default_, &out.caps_));
}


size_t Camera::frameBufferSize() const {
    // RGB32 after conversion in reader
    return static_cast<size_t>(std::abs(impl_->stride)) * height_;
}


int Camera::stride() const {
    return impl_ ? std::abs(impl_->stride) : 0;
}


bool Camera::grabFrame(uint8_t* buffer, size_t bufferSize) {
    if (!impl_ || !buffer || bufferSize < frameBufferSize()) return false;

    for (;;) {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;

        HRESULT hr = impl_->reader->ReadSample(impl_->streamIndex, 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr)) {
            std::cerr << "ReadSample failed hr=0x" << std::hex << hr << std::dec << "\n";
            return false;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) return false;
        if (flags & MF_SOURCE_READERF_STREAMTICK) continue;          // not an error
        if (flags & MF_SOURCE_READERF_NEWSTREAM) continue;           // wait for samples
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) continue;

        if (!sample) continue; // no sample yet, keep reading

        ComPtr<IMFMediaBuffer> mb;
        HR(sample->ConvertToContiguousBuffer(&mb), "ConvertToContiguousBuffer");

        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        HR(mb->Lock(&data, &maxLen, &curLen), "Buffer Lock");

        const size_t expected = frameBufferSize();
        if (curLen < expected) {
            mb->Unlock();
            return false; // partial frame, drop it
        }

        memcpy(buffer, data, expected);

        mb->Unlock();
        return true;
    }
}


