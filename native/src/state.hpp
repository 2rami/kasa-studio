#pragma once
#include "json.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace studio {
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct ModelInfo {
    std::string id, name, path;
    std::vector<std::string> expressions, motions;
};
struct AvatarSettings { float scale = 1, x = 0, y = 0; int maxTexture = 2048, fps = 30; bool gaze = true; std::string background = "studio"; };
struct TrackingSettings { std::string source = "webcam", deviceId, lipInput = "auto"; bool mirror = true, phoneAutoConnect = true; float smoothing = .07f, sensitivity = 2; int port = 4782; };
struct Config {
    bool night = false, onboarded = false, speech = false;
    std::string mode = "studio", modelId;
    AvatarSettings avatar;
    TrackingSettings tracking;
    std::string obsUrl = "ws://127.0.0.1:4455", rvcUrl = "http://127.0.0.1:18888";
    std::string agentUrl = "http://127.0.0.1:11434/v1", agentModel, agentName = "방송 파트너", agentPrompt = "방송을 함께하는 차분한 파트너입니다. 한국어로 짧고 자연스럽게 대답하세요.";
    std::vector<ModelInfo> models;
    fs::path directory;
    void load(const fs::path& directory, const fs::path& resourceDirectory);
    void save() const;
    Json json() const;
    const ModelInfo* selected() const;
};
struct Signal {
    std::string text, emotion = "neutral", motion;
    double until = 0;
    int serial = 0;
};
double now();
Json readJson(const fs::path& file);
ModelInfo inspectModel(const fs::path& file, const std::string& id = "", const std::string& name = "");
ModelInfo importModel(const fs::path& file, const fs::path& dataDirectory);
void validateSignal(const Json& signal);
void validateRvcSetting(const std::string& key, const Json& value);
bool safeRelative(const std::string& entry);
bool serviceUrlValid(const std::string& url, bool websocket = false);
}
