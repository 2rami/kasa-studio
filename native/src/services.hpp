#pragma once
#include "platform.hpp"
#include "state.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace studio {
struct Input { std::string name; float db = 0; bool muted = false; };
struct ObsState {
    bool connected = false, connecting = false, streaming = false, recording = false;
    std::string scene, error, preview;
    int previewSerial = 0;
    std::vector<std::string> scenes;
    std::vector<Input> inputs;
};
struct RvcState { bool connected = false; Json info = Json::object(); std::string error; };
class Services {
public:
    explicit Services(Config& config);
    ~Services();
    ObsState obs; RvcState rvc; Signal signal;
    std::vector<std::string> agentModels;
    Json messages = Json::array();
    std::deque<std::pair<double, std::string>> events;
    std::string error, notice, agentKey;
    int busy = 0, localPort = 0;
    bool chatBusy = false;
    bool rvcBusy = false, agentBusy = false, importBusy = false;
    bool obsPending(const std::string& request) const;
    void update();
    void connectObs(const std::string& password);
    void disconnectObs();
    void obsCall(const std::string& type, const Json& data = Json::object(), std::function<void(Json)> result = {});
    void refreshObs();
    void addOverlay();
    void fetchObsPreview();
    void connectRvc();
    bool rvcReady() const;
    void setRvc(const std::string& key, const Json& value);
    void startRvc();
    void uploadRvc(const std::vector<std::string>& files);
    void probeAgent();
    void chat(const std::string& text);
    void setSignal(const Json& value);
    void importAvatar(const std::string& path);
    void log(const std::string& text);
    void save();
    std::string agentCommand() const;
    Json diagnostic() const;
private:
    Config& config_;
    platform::Socket socket_;
    struct Pending { double deadline; std::function<void(Json)> result; std::string type; };
    std::map<std::string, Pending> requests_;
    int sequence_ = 0, generation_ = 0;
    double connectDeadline_ = 0;
    std::string obsPassword_, token_;
    struct Inbox { std::mutex mutex; std::deque<Json> items; void push(Json item); };
    std::shared_ptr<Inbox> inbox_;
    struct Job { std::function<Json()> work; std::function<void(Json)> done; };
    std::mutex jobsMutex_; std::condition_variable jobsCv_;
    std::deque<std::pair<int, Job>> jobs_;
    struct Completion { std::function<void(Json)> done; std::function<void()> failed; };
    std::map<int, Completion> completions_;
    std::thread worker_, apiThread_;
    bool stopping_ = false;
    std::atomic<bool> requestCancelled_ {false};
    int jobId_ = 0;
    struct Api; std::unique_ptr<Api> api_;
    void enqueue(std::function<Json()> work, std::function<void(Json)> done, std::function<void()> failed = {});
    void receiveObs(const Json& packet);
};
}
