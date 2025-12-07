#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

enum class CompressionFormat {
    MJPEG,
    YUY2,
    RGB32,
    Unknown
};

struct VideoMode {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    CompressionFormat format = CompressionFormat::Unknown;
};

struct CameraInfo {
    std::string id;    // MF symbolic link
    std::string name;  // Friendly name
    std::vector<VideoMode> modes;
};

// Enumerate cameras and (best-effort) native modes.
std::vector<CameraInfo> enumerateCameras();

class Camera {
public:
    struct Config {
        std::string cameraId;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        CompressionFormat format = CompressionFormat::Unknown;
    };

    explicit Camera(const Config& cfg); // Throws std::runtime_error on failure.

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) noexcept;
    Camera& operator=(Camera&&) noexcept;

    ~Camera();

    struct ExposureRange {
        long min_ = 0;
        long max_ = 0;
        long step_ = 0;
        long default_ = 0;
        long caps_ = 0;
    };

    // Media Foundation sample: exposure is not wired up; returns false.
    bool setExposure(long exposureValue);
    bool setAutoExposure();
    bool getAvailableExposureRange(ExposureRange& out);

    size_t frameBufferSize() const;

    // Reads next frame into user buffer. Returns true on success.
    bool grabFrame(uint8_t* buffer, size_t bufferSize);

    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const;
    double fps() const { return fps_; }
    CompressionFormat format() const { return format_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    int width_ = 0;
    int height_ = 0;
    double fps_ = 0.0;
    CompressionFormat format_ = CompressionFormat::Unknown;
};
