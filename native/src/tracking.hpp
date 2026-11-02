#pragma once
#include "state.hpp"
#include "platform.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace studio {
struct FacePoint { float x=0,y=0; };
struct FaceContour { std::vector<FacePoint> points; bool closed=false; };
struct FaceFrame {
    bool valid = false;
    double at = 0;
    float yaw = 0, pitch = 0, roll = 0;
    float eyeLeft = 1, eyeRight = 1, gazeX = 0, gazeY = 0;
    float mouth = 0, smile = 0, browLeft = 0, browRight = 0;
    uint64_t sequence = 0;
    std::string session;
    std::vector<FaceContour> contours;
};
struct CameraDevice { std::string id, name; bool iphone = false; };
struct FaceCameraState {
    bool active = false, starting = false;
    std::string error;
    FaceFrame frame;
    platform::Image preview;
    uint64_t previewSerial = 0;
};
class FaceCamera {
public:
    FaceCamera(); ~FaceCamera();
    std::vector<CameraDevice> devices() const;
    void start(const std::string& deviceId);
    void stop();
    FaceCameraState snapshot(bool withPreview = false) const;
    static FaceFrame analyzeImage(const std::string& path);
private:
    struct Impl; std::shared_ptr<Impl> impl_;
};
struct PhoneLinkState { bool online=false,peer=false,advertised=false; std::string error,discoveryError; FaceFrame frame; };
class PhoneLink {
public:
    explicit PhoneLink(const fs::path& directory = {}); ~PhoneLink();
    void start(int port);
    void update();
    void stop();
    PhoneLinkState snapshot() const;
    std::string connectionUrl(int port) const;
    std::string code() const;
private:
    struct Impl; std::shared_ptr<Impl> impl_;
};
FaceFrame decodeFacePacket(const Json& packet, const std::string& code, double receivedAt);
class PhoneTracker {
public:
    ~PhoneTracker();
    bool start(int port, const std::string& code);
    void setPairingUrl(const std::string& url);
    void stop();
    FaceFrame frame() const;
    bool listening() const { return listening_; }
    int port() const { return port_; }
    std::string error() const;
private:
    std::atomic<bool> listening_ {false}, stopping_ {false};
    int socket_ = -1, port_ = 0;
    std::thread thread_;
    mutable std::mutex mutex_;
    FaceFrame frame_; std::string error_;
    std::string pairingUrl_;
};
class FaceTracker {
public:
    FaceCamera camera;
    PhoneTracker phone;
    PhoneLink link;
    std::vector<CameraDevice> devices;
    std::string code, error, runningSource;
    bool calibrated = false;
    explicit FaceTracker(const fs::path& directory = {});
    void refreshDevices();
    bool start(const TrackingSettings& settings);
    void stop();
    void update(double dt, const TrackingSettings& settings);
    bool active() const;
    bool starting() const;
    bool hasFace() const { return output_.valid; }
    const FaceFrame& frame() const { return output_; }
    void calibrate();
    std::string connectionUrl() const;
    Json diagnostic() const;
    static std::vector<std::string> addresses();
private:
    FaceFrame raw_, output_, neutral_;
};
}
