#include "state.hpp"
#include "platform.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>

namespace studio {
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
Json readJson(const fs::path& file) {
    std::ifstream stream(file);
    if (!stream) throw std::runtime_error("파일을 읽을 수 없어요: " + file.filename().string());
    return Json::parse(stream);
}
bool safeRelative(const std::string& name) {
    if (name.empty() || name.size() > 500 || name.find('\\') != name.npos || name.find(':') != name.npos || name.find('\0') != name.npos) return false;
    fs::path path(name);
    if (path.is_absolute()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}
bool serviceUrlValid(const std::string& url, bool websocket) {
    const bool prefix = websocket ? (url.starts_with("ws://") || url.starts_with("wss://")) : (url.starts_with("http://") || url.starts_with("https://"));
    return prefix && url.size() < 2000 && url.find('@') == url.npos && url.find_first_of("\r\n\t ") == url.npos && url.find("://") + 3 < url.size();
}
static std::vector<std::string> references(const Json& model) {
    const auto& refs = model.at("FileReferences");
    if (!refs.is_object() || !refs.at("Moc").is_string() || !refs.at("Textures").is_array() || refs.at("Textures").empty()) throw std::runtime_error("모델 본체와 텍스처 참조가 필요해요.");
    std::vector<std::string> files;
    files.push_back(refs.at("Moc").get<std::string>());
    for (const auto& texture : refs.at("Textures")) files.push_back(texture.get<std::string>());
    for (auto key : {"Physics", "Pose"}) if (refs.contains(key)) files.push_back(refs.at(key).get<std::string>());
    for (const auto& item : refs.value("Expressions", Json::array())) files.push_back(item.at("File").get<std::string>());
    if (refs.contains("Motions")) for (const auto& [group, motions] : refs.at("Motions").items()) for (const auto& motion : motions) files.push_back(motion.at("File").get<std::string>());
    return files;
}
ModelInfo inspectModel(const fs::path& file, const std::string& id, const std::string& name) {
    if (!file.filename().string().ends_with(".model3.json")) throw std::runtime_error("model3.json 파일을 선택해 주세요.");
    const auto data = readJson(file);
    const auto root = fs::weakly_canonical(file.parent_path());
    for (const auto& ref : references(data)) {
        if (!safeRelative(ref)) throw std::runtime_error("모델에 허용되지 않는 참조 경로가 있어요.");
        auto target = fs::weakly_canonical(root / ref);
        if (!target.string().starts_with(root.string() + "/") || !fs::is_regular_file(target)) throw std::runtime_error("참조 파일이 없어요: " + ref);
    }
    ModelInfo result { id.empty() ? platform::randomToken().substr(0, 16) : id, name.empty() ? file.filename().string().substr(0, file.filename().string().size() - 12) : name, fs::absolute(file).string(), {}, {} };
    for (const auto& item : data["FileReferences"].value("Expressions", Json::array())) result.expressions.push_back(item.at("Name").get<std::string>());
    if (data["FileReferences"].contains("Motions")) for (const auto& [group, list] : data["FileReferences"]["Motions"].items()) result.motions.push_back(group);
    return result;
}
ModelInfo importModel(const fs::path& source, const fs::path& dataDirectory) {
    if (source.extension() != ".zip") return inspectModel(source);
    const auto id = platform::randomToken().substr(0, 16);
    const auto target = dataDirectory / "models" / id;
    fs::create_directories(target);
    auto* reader = archive_read_new();
    archive_read_support_format_zip(reader); archive_read_support_filter_all(reader);
    if (archive_read_open_filename(reader, source.c_str(), 65536) != ARCHIVE_OK) { archive_read_free(reader); fs::remove_all(target); throw std::runtime_error("ZIP을 열 수 없어요."); }
    try {
        struct archive_entry* entry;
        uint64_t total = 0; int count = 0, status;
        std::vector<fs::path> manifests;
        while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
            const std::string name = archive_entry_pathname(entry);
            if (name.starts_with("__MACOSX/") || name.find("/.DS_Store") != name.npos) { archive_read_data_skip(reader); continue; }
            if (!safeRelative(name) || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) throw std::runtime_error("ZIP에 허용되지 않는 경로가 있어요.");
            if (++count > 4096) throw std::runtime_error("ZIP 파일이 너무 많아요.");
            const auto kind = archive_entry_filetype(entry);
            if (kind == AE_IFDIR) { fs::create_directories(target / name); continue; }
            if (kind != AE_IFREG) throw std::runtime_error("일반 파일만 가져올 수 있어요.");
            auto destination = target / name;
            fs::create_directories(destination.parent_path());
            std::ofstream output(destination, std::ios::binary);
            if (!output) throw std::runtime_error("모델 파일을 저장할 수 없어요.");
            char buffer[65536]; la_ssize_t size;
            while ((size = archive_read_data(reader, buffer, sizeof(buffer))) > 0) {
                total += static_cast<uint64_t>(size);
                if (total > 256ULL * 1024 * 1024) throw std::runtime_error("압축 해제 용량은 256MB 이하여야 해요.");
                output.write(buffer, size);
                if (!output) throw std::runtime_error("모델 파일을 저장할 수 없어요.");
            }
            if (size < 0) throw std::runtime_error("손상된 ZIP 파일이에요.");
            if (name.ends_with(".model3.json")) manifests.push_back(destination);
        }
        if (status != ARCHIVE_EOF || manifests.size() != 1) throw std::runtime_error("ZIP에 model3.json이 하나 있어야 해요.");
        auto model = inspectModel(manifests[0], id);
        archive_read_free(reader); return model;
    } catch (...) { archive_read_free(reader); fs::remove_all(target); throw; }
}

Json Config::json() const {
    Json library = Json::array();
    for (const auto& model : models) library.push_back({{"id", model.id}, {"name", model.name}, {"path", model.path}});
    return {{"version", 4}, {"theme", night ? "night" : "personal"}, {"onboarded", onboarded}, {"mode", mode}, {"modelId", modelId},
        {"avatar", {{"scale", avatar.scale}, {"x", avatar.x}, {"y", avatar.y}, {"maxTexture", avatar.maxTexture}, {"fps", avatar.fps}, {"gaze", avatar.gaze}, {"background", avatar.background}}},
        {"tracking", {{"source",tracking.source},{"deviceId",tracking.deviceId},{"lipInput",tracking.lipInput},{"mirror",tracking.mirror},{"smoothing",tracking.smoothing},{"sensitivity",tracking.sensitivity},{"port",tracking.port},{"phoneAutoConnect",tracking.phoneAutoConnect}}},
        {"obsUrl", obsUrl}, {"rvcUrl", rvcUrl}, {"agent", {{"endpoint", agentUrl}, {"model", agentModel}, {"name", agentName}, {"prompt", agentPrompt}, {"speak", speech}}}, {"models", library}};
}
void Config::load(const fs::path& folder, const fs::path& resources) {
    directory = folder; fs::create_directories(directory);
    fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace);
    const auto file = directory / "settings.json";
    if (fs::exists(file)) {
        const auto data = readJson(file);
        night = data.value("theme", "personal") == "night"; onboarded = data.value("onboarded", false); mode = data.value("mode", "studio"); modelId = data.value("modelId", "");
        const auto a = data.value("avatar", Json::object());
        avatar.scale = std::clamp(a.value("scale", 1.f), 0.3f, 2.f); avatar.x = std::clamp(a.value("x", 0.f), -0.7f, 0.7f); avatar.y = std::clamp(a.value("y", 0.f), -0.5f, 0.5f);
        avatar.maxTexture = a.value("maxTexture", 2048); if (avatar.maxTexture!=1024 && avatar.maxTexture!=4096) avatar.maxTexture=2048;
        if (data.value("version",2)<3 && avatar.maxTexture==1024) avatar.maxTexture=2048;
        avatar.fps = a.value("fps", 30) == 60 ? 60 : 30;
        avatar.gaze = a.value("gaze", true); avatar.background = a.value("background", "studio");
        const auto track=data.value("tracking",Json::object());
        tracking.source=track.value("source","webcam"); if (tracking.source!="iphone") tracking.source="webcam";
        tracking.deviceId=track.value("deviceId",""); tracking.lipInput=track.value("lipInput","auto"); if (tracking.lipInput!="face" && tracking.lipInput!="microphone") tracking.lipInput="auto";
        tracking.mirror=track.value("mirror",true); tracking.smoothing=std::clamp(track.value("smoothing",.07f),.015f,.3f); tracking.sensitivity=std::clamp(track.value("sensitivity",2.f),.3f,4.f);
        if(data.value("version",3)<4&&tracking.sensitivity==1.f)tracking.sensitivity=2.f;
        tracking.port=std::clamp(track.value("port",4782),1024,65535);
        tracking.phoneAutoConnect=track.value("phoneAutoConnect",true);
        obsUrl = data.value("obsUrl", obsUrl); rvcUrl = data.value("rvcUrl", rvcUrl);
        const auto agent = data.value("agent", Json::object());
        agentUrl = agent.value("endpoint", agentUrl); agentModel = agent.value("model", ""); agentName = agent.value("name", agentName); agentPrompt = agent.value("prompt", agentPrompt); speech = agent.value("speak", false);
        for (const auto& item : data.value("models", Json::array())) {
            try { models.push_back(inspectModel(item.at("path").get<std::string>(), item.at("id").get<std::string>(), item.at("name").get<std::string>())); } catch (...) { /* 옮겨진 모델 하나 때문에 작업실 전체를 열지 못하는 일을 막는다. */ }
        }
    }
    const auto sample = resources / "models/hoodiecat/hoodiecat.model3.json";
    if (models.empty() && fs::exists(sample)) models.push_back(inspectModel(sample, "hoodiecat", "후드캣"));
    if (!selected()) modelId = models.empty() ? "" : models.front().id;
}
void Config::save() const {
    const auto tmp = directory / "settings.json.tmp";
    std::ofstream stream(tmp); stream << json().dump(2); stream.close();
    if (!stream) throw std::runtime_error("설정을 저장할 수 없어요.");
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    fs::rename(tmp, directory / "settings.json");
}
const ModelInfo* Config::selected() const {
    for (const auto& model : models) if (model.id == modelId) return &model;
    return nullptr;
}
void validateSignal(const Json& signal) {
    const std::set<std::string> emotions {"neutral", "smile", "surprise", "sad", "angry", "shy", "wink"};
    if (!signal.is_object() || signal.value("text", "").size() > 6000 || !emotions.contains(signal.value("emotion", "neutral"))) throw std::runtime_error("대사와 표정의 형식을 확인해 주세요.");
    const auto motion = signal.value("motion", "");
    const double duration = signal.value("duration", 8.0);
    if ((!motion.empty() && motion != "Nod" && motion != "Shake" && motion != "Blink") || duration < 1 || duration > 60 || !std::isfinite(duration)) throw std::runtime_error("동작과 지속 시간을 확인해 주세요.");
}
void validateRvcSetting(const std::string& key, const Json& value) {
    if (key == "f0Detector" && value.is_string() && std::set<std::string>{"rmvpe", "crepe", "harvest", "dio"}.contains(value.get<std::string>())) return;
    const std::map<std::string, std::pair<double, double>> bounds {
        {"modelSlotIndex", {0, 999}}, {"tran", {-24, 24}}, {"indexRatio", {0, 1}}, {"protect", {0, .5}}, {"serverInputDeviceId", {-1, 10000}}, {"serverOutputDeviceId", {-1, 10000}}, {"serverMonitorDeviceId", {-1, 10000}}, {"serverReadChunkSize", {64, 4096}}, {"serverAudioSampleRate", {16000, 96000}}, {"serverAudioStated", {0, 1}}, {"enableServerAudio", {0, 1}}
    };
    auto found = bounds.find(key);
    if (found == bounds.end() || !value.is_number()) throw std::runtime_error("허용되지 않는 음성 설정이에요.");
    double number = value.get<double>();
    if (!std::isfinite(number) || number < found->second.first || number > found->second.second) throw std::runtime_error("음성 설정 범위를 확인해 주세요.");
}
}
