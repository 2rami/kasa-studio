#include "services.hpp"
#include "httplib.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <set>
#include <stdexcept>

namespace studio {
static Json httpJson(const std::string& url, const std::string& method = "GET", const std::string& body = "", const std::map<std::string, std::string>& headers = {}, double timeout = 15) {
    auto response = platform::request(url, method, body, headers, timeout);
    if (!response.error.empty()) throw std::runtime_error(response.error);
    if (response.status < 200 || response.status >= 300) throw std::runtime_error("서버 응답 오류 (" + std::to_string(response.status) + ")");
    auto result = Json::parse(response.body);
    if (result.is_null() || (result.is_object() && (result.value("status", "") == "ERROR" || result.contains("error")))) throw std::runtime_error(result.value("msg", std::string("서버가 요청을 처리하지 못했어요.")));
    return result;
}
static std::string base(std::string url) { while (url.ends_with('/')) url.pop_back(); return url; }
static Json rvcInfo(Json info) {
    if (!info.is_object() || !info.contains("enableServerAudio") || !info.contains("modelSlots") || !info["modelSlots"].is_array()) throw std::runtime_error("w-okada MMVCServerSIO 서버 오디오 API가 필요해요.");
    Json slots=Json::array(); for (const auto& slot:info["modelSlots"]) if (slot.is_object()) slots.push_back(slot);
    info["modelSlots"]=std::move(slots); return info;
}
struct Multipart {
    std::string boundary = "KasaStudio" + platform::randomToken(), body;
    void field(const std::string& key, const std::string& value) { body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + key + "\"\r\n\r\n" + value + "\r\n"; }
    void file(const std::string& filename, const char* data, size_t size) { body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n"; body.append(data, size); body += "\r\n"; }
    Json send(const std::string& url, double timeout = 15) { body += "--" + boundary + "--\r\n"; return httpJson(url, "POST", body, {{"Content-Type", "multipart/form-data; boundary=" + boundary}}, timeout); }
};
static Json updateRvc(const std::string& url, const std::string& key, const Json& value) {
    validateRvcSetting(key, value);
    Multipart form; form.field("key", key); form.field("val", value.is_string() ? value.get<std::string>() : value.dump());
    return rvcInfo(form.send(base(url) + "/update_settings"));
}
void Services::Inbox::push(Json item) { std::lock_guard lock(mutex); if (items.size() < 256) items.push_back(std::move(item)); }
struct Services::Api { httplib::Server server; };
Services::Services(Config& config) : config_(config), inbox_(std::make_shared<Inbox>()), api_(std::make_unique<Api>()) {
    auto tokenFile = config_.directory / "agent-token";
    if (fs::exists(tokenFile)) { std::ifstream stream(tokenFile); stream >> token_; }
    if (token_.size() != 48) { token_ = platform::randomToken(); std::ofstream stream(tokenFile); stream << token_; stream.close(); fs::permissions(tokenFile, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace); }
    if (auto* key = std::getenv("KASA_STUDIO_AGENT_KEY")) agentKey = key;
    auto inbox = inbox_;
    worker_ = std::thread([this, inbox] {
        platform::setRequestCancellation(&requestCancelled_);
        for (;;) {
            std::pair<int, Job> item;
            { std::unique_lock lock(jobsMutex_); jobsCv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); }); if (stopping_) return; item = std::move(jobs_.front()); jobs_.pop_front(); }
            try { inbox->push({{"type", "job"}, {"id", item.first}, {"ok", true}, {"data", item.second.work()}}); }
            catch (const std::exception& failure) { inbox->push({{"type", "job"}, {"id", item.first}, {"ok", false}, {"error", failure.what()}}); }
        }
    });
    api_->server.set_payload_max_length(65536);
    api_->server.new_task_queue = [] { return new httplib::ThreadPool(2); };
    api_->server.Get("/api/health", [](const auto&, auto& response) { response.set_content("{\"ok\":true,\"runtime\":\"C++\",\"renderer\":\"Cubism Native\"}", "application/json"); });
    const auto token = token_;
    api_->server.Post("/api/agent/signal", [inbox, token](const httplib::Request& request, httplib::Response& response) {
        if (request.has_header("Origin") || request.get_header_value("X-Studio-Token") != token) { response.status = 401; response.set_content("{\"error\":\"연결 토큰이 필요해요.\"}", "application/json"); return; }
        try { auto signal = Json::parse(request.body); validateSignal(signal); inbox->push({{"type", "signal"}, {"data", signal}}); response.set_content("{\"accepted\":true}", "application/json"); }
        catch (const std::exception& failure) { response.status = 400; response.set_content(Json({{"error", failure.what()}}).dump(), "application/json"); }
    });
    auto port = std::getenv("KASA_STUDIO_AGENT_PORT");
    const int requested = port ? std::atoi(port) : 4781;
    if (requested > 0 && api_->server.bind_to_port("127.0.0.1", requested)) localPort = requested;
    else localPort = api_->server.bind_to_any_port("127.0.0.1");
    if (localPort > 0) { apiThread_ = std::thread([this] { api_->server.listen_after_bind(); }); api_->server.wait_until_ready(); }
    else error = "에이전트 수신 포트를 열 수 없어요.";
}
Services::~Services() {
    requestCancelled_ = true;
    socket_.close(); api_->server.stop();
    if (apiThread_.joinable()) apiThread_.join();
    { std::lock_guard lock(jobsMutex_); stopping_ = true; jobs_.clear(); }
    jobsCv_.notify_all(); if (worker_.joinable()) worker_.join();
    inbox_.reset();
}
void Services::enqueue(std::function<Json()> work, std::function<void(Json)> done, std::function<void()> failed) {
    ++busy; const int id = ++jobId_; completions_[id] = {std::move(done), std::move(failed)};
    { std::lock_guard lock(jobsMutex_); jobs_.push_back({id, {std::move(work), {}}}); } jobsCv_.notify_one();
}
void Services::update() {
    std::deque<Json> items;
    { std::lock_guard lock(inbox_->mutex); items.swap(inbox_->items); }
    for (auto& item : items) {
        try {
            const auto type = item.value("type", "");
            if (type == "signal") setSignal(item.at("data"));
            else if (type == "obs" && item.value("generation", -1) == generation_) receiveObs(item.at("data"));
            else if (type == "obs_error" && item.value("generation", -1) == generation_) { disconnectObs(); obs.error = item.value("error", "OBS 연결이 끊겼어요."); error = obs.error; }
            else if (type == "job") {
                --busy; auto found = completions_.find(item.at("id").get<int>());
                if (found == completions_.end()) continue;
                auto callback = std::move(found->second); completions_.erase(found);
                if (item.value("ok", false)) callback.done(item.at("data"));
                else { error = item.value("error", "연결 요청을 처리하지 못했어요."); if (callback.failed) callback.failed(); }
            }
        } catch (const std::exception& failure) { error = failure.what(); }
    }
    if (obs.connecting && now() > connectDeadline_) { disconnectObs(); error = obs.error = "OBS 연결 시간이 초과됐어요. 서버 주소와 비밀번호를 확인해 주세요."; }
    for (auto item = requests_.begin(); item != requests_.end();) {
        if (now() > item->second.deadline) { error = "OBS 응답 시간 초과: " + item->second.type; item = requests_.erase(item); } else ++item;
    }
    if (signal.until > 0 && now() > signal.until) { signal.text.clear(); signal.emotion = "neutral"; signal.motion.clear(); signal.until = 0; ++signal.serial; }
}
void Services::log(const std::string& text) { events.push_front({now(), text}); if (events.size() > 30) events.pop_back(); notice = text; }
void Services::save() { try { config_.save(); } catch (const std::exception& failure) { error = failure.what(); } }
void Services::connectObs(const std::string& password) {
    if (!serviceUrlValid(config_.obsUrl, true)) { error = "OBS WebSocket 주소를 확인해 주세요."; return; }
    disconnectObs(); obs.connecting = true; connectDeadline_ = now() + 12; obsPassword_ = password; save();
    const auto generation = generation_; const auto inbox = std::weak_ptr<Inbox>(inbox_);
    socket_.connect(config_.obsUrl, [inbox, generation](std::string data) { if (auto target = inbox.lock()) { try { target->push({{"type", "obs"}, {"generation", generation}, {"data", Json::parse(data)}}); } catch (...) { target->push({{"type", "obs_error"}, {"generation", generation}, {"error", "OBS가 아닌 서버의 응답이에요."}}); } } }, [inbox, generation](std::string error) { if (auto target = inbox.lock()) target->push({{"type", "obs_error"}, {"generation", generation}, {"error", error}}); });
}
void Services::disconnectObs() { ++generation_; socket_.close(); requests_.clear(); obs = {}; obsPassword_.clear(); }
void Services::receiveObs(const Json& packet) {
    const int op = packet.at("op").get<int>(); const auto& data = packet.at("d");
    if (op == 0) {
        Json identify = {{"rpcVersion", 1}, {"eventSubscriptions", 0x7ff}};
        if (data.contains("authentication")) { const auto& auth = data["authentication"]; const auto secret = platform::sha256Base64(obsPassword_ + auth.at("salt").get<std::string>()); identify["authentication"] = platform::sha256Base64(secret + auth.at("challenge").get<std::string>()); }
        socket_.send(Json({{"op", 1}, {"d", identify}}).dump());
    } else if (op == 2) { obs.connected = true; obs.connecting = false; obsPassword_.clear(); log("OBS에 연결했어요."); refreshObs(); }
    else if (op == 7) {
        auto found = requests_.find(data.at("requestId").get<std::string>());
        if (found == requests_.end()) return;
        auto pending = std::move(found->second); requests_.erase(found);
        if (!data.at("requestStatus").value("result", false)) { error = "OBS " + pending.type + ": " + data["requestStatus"].value("comment", "요청 실패"); return; }
        if (pending.result) pending.result(data.value("responseData", Json::object()));
    } else if (op == 5) {
        const auto type = data.value("eventType", ""); const auto event = data.value("eventData", Json::object());
        if (type == "CurrentProgramSceneChanged") obs.scene = event.value("sceneName", "");
        else if (type == "StreamStateChanged") obs.streaming = event.value("outputActive", false);
        else if (type == "RecordStateChanged") obs.recording = event.value("outputActive", false);
        else if (type == "SceneCreated" || type == "SceneRemoved" || type == "InputCreated" || type == "InputRemoved") refreshObs();
        else if (type == "InputMuteStateChanged" || type == "InputVolumeChanged") for (auto& input : obs.inputs) if (input.name == event.value("inputName", "")) { if (event.contains("inputMuted")) input.muted = event["inputMuted"]; if (event.contains("inputVolumeDb")) input.db = event["inputVolumeDb"]; }
    }
}
void Services::obsCall(const std::string& type, const Json& data, std::function<void(Json)> result) {
    if (!obs.connected) { error = "OBS를 먼저 연결해 주세요."; return; }
    const auto id = std::to_string(++sequence_); requests_[id] = {now() + 12, std::move(result), type};
    socket_.send(Json({{"op", 6}, {"d", {{"requestType", type}, {"requestId", id}, {"requestData", data.is_object() ? data : Json::object()}}}}).dump());
}
bool Services::obsPending(const std::string& type) const { for (const auto& [id,pending]:requests_) if (pending.type==type) return true; return false; }
void Services::refreshObs() {
    obsCall("GetSceneList", {}, [this](Json result) { obs.scenes.clear(); obs.scene = result.value("currentProgramSceneName", ""); for (auto& scene : result["scenes"]) obs.scenes.push_back(scene.value("sceneName", "")); });
    obsCall("GetStreamStatus", {}, [this](Json result) { obs.streaming = result.value("outputActive", false); });
    obsCall("GetRecordStatus", {}, [this](Json result) { obs.recording = result.value("outputActive", false); });
    obsCall("GetInputList", {}, [this](Json result) {
        obs.inputs.clear();
        for (auto& item : result["inputs"]) {
            auto name = item.at("inputName").get<std::string>();
            obsCall("GetInputVolume", {{"inputName", name}}, [this, name](Json data) {
                obs.inputs.push_back({name, data.value("inputVolumeDb", 0.f), false});
                obsCall("GetInputMute", {{"inputName", name}}, [this, name](Json data) { for (auto& input : obs.inputs) if (input.name == name) input.muted = data.value("inputMuted", false); });
            });
        }
    });
}
void Services::addOverlay() {
    const auto scene = obs.scene;
    obsCall("GetInputKindList", {{"unversioned", true}}, [this, scene](Json result) {
        const auto kinds = result.value("inputKinds", std::vector<std::string>{});
        if (std::find(kinds.begin(), kinds.end(), "window_capture") == kinds.end()) { error = "OBS에서 창 캡처를 추가하고 'Kasa Studio Pet'을 선택해 주세요. 이 OBS 버전은 창 선택을 직접 해야 해요."; return; }
        obsCall("CreateInput", {{"sceneName", scene}, {"inputName", "Kasa Studio Pet"}, {"inputKind", "window_capture"}, {"inputSettings", Json::object()}, {"sceneItemEnabled", false}}, [this, scene](Json created) {
            obsCall("GetInputPropertiesListPropertyItems", {{"inputName", "Kasa Studio Pet"}, {"propertyName", "window"}}, [this, created, scene](Json result) {
                for (const auto& item : result["propertyItems"]) if (item.value("itemName", "").find("Kasa Studio Pet") != std::string::npos) {
                    obsCall("SetInputSettings", {{"inputName", "Kasa Studio Pet"}, {"inputSettings", {{"window", item.at("itemValue")}}}, {"overlay", true}});
                    obsCall("SetSceneItemEnabled", {{"sceneName", scene}, {"sceneItemId", created.at("sceneItemId")}, {"sceneItemEnabled", true}}, [this](Json) { log("OBS 장면에 네이티브 펫 창을 추가했어요."); }); return;
                }
                obsCall("RemoveInput", {{"inputName", "Kasa Studio Pet"}}); error = "OBS에서 화면 기록 권한을 확인한 뒤 'Kasa Studio Pet' 창을 선택해 주세요.";
            });
        });
    });
}
void Services::fetchObsPreview() { if (!obs.connected || obs.scene.empty()) return; obsCall("GetSourceScreenshot", {{"sourceName", obs.scene}, {"imageFormat", "jpeg"}, {"imageWidth", 960}, {"imageCompressionQuality", 75}}, [this](Json result) { obs.preview = result.value("imageData", ""); ++obs.previewSerial; }); }
void Services::connectRvc() {
    if (rvcBusy) return;
    if (!serviceUrlValid(config_.rvcUrl)) { error = "Voice Changer 주소를 확인해 주세요."; return; }
    save(); const auto url = config_.rvcUrl; rvcBusy=true;
    enqueue([url] { return rvcInfo(httpJson(base(url) + "/info")); }, [this](Json result) { rvcBusy=false; rvc.info = result; rvc.connected = true; rvc.error.clear(); log("Voice Changer에 연결했어요."); }, [this] { rvcBusy=false; rvc.connected=false; rvc.error=error; });
}
void Services::setRvc(const std::string& key, const Json& value) {
    if (rvcBusy) return;
    if (!rvc.connected) { error = "Voice Changer를 먼저 연결해 주세요."; return; }
    try { validateRvcSetting(key, value); } catch (const std::exception& failure) { error = failure.what(); return; }
    const auto url = config_.rvcUrl; rvcBusy=true;
    enqueue([url, key, value] { return updateRvc(url, key, value); }, [this, key, value](Json result) { rvcBusy=false; rvc.info = result; if (key == "serverAudioStated") log(value.get<int>() == 1 ? "음성 변환을 시작했어요." : "음성 변환을 중지했어요."); }, [this] { rvcBusy=false; rvc.connected=false; rvc.error=error; });
}
bool Services::rvcReady() const {
    if (!rvc.connected || rvc.info.value("serverInputDeviceId",-1)<0 || rvc.info.value("serverOutputDeviceId",-1)<0) return false;
    const int selected=rvc.info.value("modelSlotIndex",-1);
    for (const auto& slot:rvc.info.value("modelSlots",Json::array())) if (slot.value("slotIndex",-2)==selected && slot.value("voiceChangerType","")=="RVC" && !slot.value("modelFile","").empty()) return true;
    return false;
}
void Services::startRvc() {
    if (rvcBusy) return;
    if (!rvcReady()) { error = "RVC 음성 모델·입력 마이크·출력 장치를 선택해 주세요."; return; }
    const auto url = config_.rvcUrl; rvcBusy=true;
    enqueue([url] { updateRvc(url, "enableServerAudio", 1); return updateRvc(url, "serverAudioStated", 1); }, [this](Json result) { rvcBusy=false; rvc.info = result; log("음성 변환을 시작했어요."); }, [this] { rvcBusy=false; rvc.connected=false; rvc.error=error; });
}
void Services::uploadRvc(const std::vector<std::string>& files) {
    if (rvcBusy) return;
    if (!rvc.connected || files.empty()) { error = "Voice Changer를 먼저 연결해 주세요."; return; }
    const auto url = config_.rvcUrl; std::set<int> used; rvcBusy=true;
    for (const auto& slot : rvc.info.value("modelSlots", Json::array())) if (!slot.value("modelFile", "").empty()) used.insert(slot.value("slotIndex", -1));
    int slot = 0; while (used.contains(slot)) ++slot;
    enqueue([url, files, slot] {
        Json refs = Json::array(); int modelCount = 0;
        for (const auto& file : files) {
            const auto extension = fs::path(file).extension().string();
            if (extension != ".pth" && extension != ".onnx" && extension != ".index") throw std::runtime_error("RVC 모델 .pth/.onnx와 선택한 .index 파일만 가져올 수 있어요.");
            if (extension != ".index") ++modelCount;
            if (fs::file_size(file) == 0 || fs::file_size(file) > 160ULL * 1024 * 1024) throw std::runtime_error("음성 모델 파일은 1바이트~160MB여야 해요.");
        }
        if (modelCount != 1 || files.size() > 2) throw std::runtime_error("음성 모델 하나와 선택한 인덱스 파일을 넣어 주세요.");
        for (const auto& file : files) {
            std::string name = fs::path(file).filename().string(); for (auto& c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) c = '_';
            std::ifstream input(file, std::ios::binary); std::vector<char> buffer(1024 * 1024); int chunks = 0;
            while (input) { input.read(buffer.data(), buffer.size()); const auto size = input.gcount(); if (size == 0) break; Multipart form; auto chunk = name + "_" + std::to_string(chunks++); form.field("filename", chunk); form.file(chunk, buffer.data(), size); form.send(base(url) + "/upload_file"); }
            Multipart concat; concat.field("filename", name); concat.field("filenameChunkNum", std::to_string(chunks)); concat.send(base(url) + "/concat_uploaded_file");
            refs.push_back({{"name", name}, {"dir", ""}, {"kind", fs::path(file).extension() == ".index" ? "rvcIndex" : "rvcModel"}});
        }
        Json params = {{"voiceChangerType", "RVC"}, {"slot", slot}, {"isSampleMode", false}, {"sampleId", ""}, {"files", refs}, {"params", Json::object()}};
        Multipart load; load.field("slot", std::to_string(slot)); load.field("isHalf", "false"); load.field("params", params.dump()); load.send(base(url) + "/load_model", 120);
        return rvcInfo(httpJson(base(url) + "/info"));
    }, [this](Json result) { rvcBusy=false; rvc.info = result; log("RVC 모델을 빈 슬롯에 가져왔어요."); }, [this] { rvcBusy=false; rvc.connected=false; rvc.error=error; });
}
void Services::probeAgent() {
    if (agentBusy) return;
    if (!serviceUrlValid(config_.agentUrl)) { error = "에이전트 API 주소를 확인해 주세요."; return; }
    const auto url = config_.agentUrl, key = agentKey; save(); agentBusy=true;
    enqueue([url, key] { return httpJson(base(url) + "/models", "GET", "", key.empty() ? std::map<std::string, std::string>{} : std::map<std::string, std::string>{{"Authorization", "Bearer " + key}}); }, [this](Json result) { agentBusy=false; agentModels.clear(); for (const auto& model : result.value("data", Json::array())) agentModels.push_back(model.at("id").get<std::string>()); if (config_.agentModel.empty() && !agentModels.empty()) config_.agentModel = agentModels.front(); save(); log("에이전트 모델 목록을 가져왔어요."); }, [this] { agentBusy=false; });
}
void Services::chat(const std::string& text) {
    if (chatBusy || text.empty()) return;
    if (config_.agentModel.empty() || !serviceUrlValid(config_.agentUrl)) { error = "에이전트 주소와 모델을 먼저 설정해 주세요."; return; }
    chatBusy = true;
    auto history = messages; history.push_back({{"role", "user"}, {"content", text}});
    Json prompt = Json::array({{{"role", "system"}, {"content", config_.agentPrompt + "\nJSON 객체로 답하세요: {\"text\":\"할 말\",\"emotion\":\"neutral|smile|surprise|sad|angry|shy|wink\",\"motion\":\"|Nod|Shake|Blink\"}."}}});
    for (auto& message : history) prompt.push_back(message);
    const auto url = config_.agentUrl, key = agentKey;
    Json payload = {{"model", config_.agentModel}, {"stream", false}, {"messages", prompt}};
    enqueue([url, key, payload] {
        auto headers = std::map<std::string, std::string>{{"Content-Type", "application/json"}}; if (!key.empty()) headers["Authorization"] = "Bearer " + key;
        auto result = httpJson(base(url) + "/chat/completions", "POST", payload.dump(), headers, 60);
        auto text = result.at("choices").at(0).at("message").at("content").get<std::string>();
        Json reply = {{"text", text}, {"emotion", "neutral"}, {"duration", 10}};
        try { auto parsed = Json::parse(text); validateSignal(parsed); reply = parsed; } catch (...) { if (text.size() > 6000) reply["text"] = text.substr(0, 6000); }
        return reply;
    }, [this, history](Json reply) mutable { chatBusy = false; history.push_back({{"role", "assistant"}, {"content", reply.at("text")}}); while (history.size() > 20) history.erase(history.begin()); messages = history; setSignal(reply); }, [this] { chatBusy = false; });
}
void Services::setSignal(const Json& value) { validateSignal(value); signal.text = value.value("text", ""); signal.emotion = value.value("emotion", "neutral"); signal.motion = value.value("motion", ""); signal.until = now() + value.value("duration", 8.0); ++signal.serial; }
void Services::importAvatar(const std::string& path) {
    if (importBusy) return; importBusy=true;
    const auto directory = config_.directory;
    enqueue([path, directory] { auto model = importModel(path, directory); return Json({{"path", model.path}, {"id", model.id}, {"name", model.name}}); }, [this](Json result) { importBusy=false; auto model = inspectModel(result["path"].get<std::string>(), result["id"].get<std::string>(), result["name"].get<std::string>()); config_.models.push_back(model); config_.modelId = model.id; save(); log(model.name + " 모델을 가져왔어요."); }, [this] { importBusy=false; });
}
std::string Services::agentCommand() const { return "curl -X POST 'http://127.0.0.1:" + std::to_string(localPort) + "/api/agent/signal' -H 'Content-Type: application/json' -H 'X-Studio-Token: " + token_ + "' -d '{\"text\":\"준비됐어요.\",\"emotion\":\"smile\",\"motion\":\"Nod\"}'"; }
Json Services::diagnostic() const { return {{"runtime", "C++"}, {"obs", {{"connected", obs.connected}, {"scene", obs.scene}, {"streaming", obs.streaming}, {"recording", obs.recording}}}, {"rvcConnected", rvc.connected}, {"localPort", localPort}, {"signal", {{"emotion", signal.emotion}, {"text", signal.text}}}, {"busy", busy}, {"error", error}}; }
}
