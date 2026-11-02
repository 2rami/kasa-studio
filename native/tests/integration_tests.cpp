#include "services.hpp"
#include "httplib.h"
#include <atomic>
#include <cassert>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

using namespace studio;
struct Mock {
    httplib::Server server;
    std::thread thread;
    int port = 0;
    void start() { port = server.bind_to_any_port("127.0.0.1"); thread = std::thread([this] { server.listen_after_bind(); }); server.wait_until_ready(); }
    ~Mock() { server.stop(); if (thread.joinable()) thread.join(); }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};
static void waitFor(Services& services, const std::function<bool()>& predicate) {
    const double deadline = now()+10;
    while (now()<deadline) { services.update(); if (predicate()) return; if (!services.error.empty()) throw std::runtime_error(services.error); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    throw std::runtime_error("시험 서버 응답 시간 초과");
}
int main() {
    const auto directory = fs::temp_directory_path() / ("kasa-native-integration-"+platform::randomToken().substr(0,12)); fs::create_directories(directory);
    try {
        Mock peer;
        Json info = {{"status","OK"},{"enableServerAudio",0},{"serverAudioStated",0},{"modelSlotIndex",0},{"serverInputDeviceId",3},{"serverOutputDeviceId",7},{"tran",0},{"modelSlots",Json::array({{{"slotIndex",0},{"name","기존 음성"},{"modelFile","existing.pth"},{"voiceChangerType","RVC"}}})}};
        info["modelSlots"].push_back(nullptr);
        std::mutex mutex; std::vector<Json> calls; std::vector<std::string> chunks;
        std::atomic<bool> authenticated{false},recorded{false},loaded{false};
        std::string currentScene="대화";
        peer.server.WebSocket("/obs", [&](const httplib::Request&, httplib::ws::WebSocket& socket) {
            socket.set_read_timeout(1);
            socket.send(Json({{"op",0},{"d",{{"obsWebSocketVersion","5.0.0"},{"rpcVersion",1},{"authentication",{{"salt","test-salt"},{"challenge","test-challenge"}}}}}}).dump());
            std::string raw;
            for (;;) {
                auto result = socket.read(raw); if (result==httplib::ws::Timeout) continue; if (result==httplib::ws::Fail) break;
                auto packet=Json::parse(raw); int op=packet.at("op"); auto data=packet.at("d");
                if (op==1) { assert(data.at("authentication")==platform::sha256Base64(platform::sha256Base64("passwordtest-salt")+"test-challenge")); authenticated=true; socket.send("{\"op\":2,\"d\":{\"negotiatedRpcVersion\":1}}"); continue; }
                assert(op==6); assert(data.at("requestData").is_object());
                { std::lock_guard lock(mutex); calls.push_back(data); }
                std::string type=data.at("requestType"); Json response=Json::object();
                if (type=="GetSceneList") response={{"currentProgramSceneName",currentScene},{"scenes",Json::array({{{"sceneName","대화"}},{{"sceneName","게임"}}})}};
                if (type=="GetInputList") response={{"inputs",Json::array({{{"inputName","Mic"}}})}};
                if (type=="GetInputVolume") response={{"inputVolumeDb",-12}};
                if (type=="GetInputMute") response={{"inputMuted",false}};
                if (type=="GetStreamStatus") response={{"outputActive",false}};
                if (type=="GetRecordStatus") response={{"outputActive",recorded.load()}};
                if (type=="SetCurrentProgramScene") currentScene=data["requestData"]["sceneName"];
                if (type=="StartRecord") recorded=true;
                if (type=="StopRecord") recorded=false;
                socket.send(Json({{"op",7},{"d",{{"requestType",type},{"requestId",data.at("requestId")},{"requestStatus",{{"result",true},{"code",100}}},{"responseData",response}}}}).dump());
            }
        }, [](const std::vector<std::string>&) { return std::string("obswebsocket.json"); });
        peer.server.Get("/info", [&](const auto&,auto& response) { response.set_content(info.dump(),"application/json"); });
        peer.server.Post("/update_settings", [&](const auto& request,auto& response) { auto key=request.form.get_field("key"),value=request.form.get_field("val"); assert(!key.empty()); info[key]=key=="f0Detector"?Json(value):Json::parse(value); response.set_content(info.dump(),"application/json"); });
        peer.server.Post("/upload_file", [&](const auto& request,auto& response) { const auto file=request.form.get_file("file"); assert(!file.content.empty()); chunks.push_back(request.form.get_field("filename")); response.set_content("{\"status\":\"OK\"}","application/json"); });
        peer.server.Post("/concat_uploaded_file", [](const auto& request,auto& response) { assert(!request.form.get_field("filename").empty()); response.set_content("{\"status\":\"OK\"}","application/json"); });
        peer.server.Post("/load_model", [&](const auto& request,auto& response) { const auto params=Json::parse(request.form.get_field("params")); assert(params.at("slot")==1); assert(params.at("voiceChangerType")=="RVC"); assert(params["files"].size()==2); info["modelSlots"].push_back({{"slotIndex",1},{"name","새 음성"},{"modelFile","voice.pth"},{"voiceChangerType","RVC"}}); loaded=true; response.set_content("{\"status\":\"OK\"}","application/json"); });
        peer.server.Get("/v1/models", [](const auto&,auto& response) { response.set_content("{\"data\":[{\"id\":\"test-partner\"}]}","application/json"); });
        peer.server.Post("/v1/chat/completions", [](const auto& request,auto& response) { assert(request.get_header_value("Authorization")=="Bearer test-secret-key"); auto data=Json::parse(request.body); assert(data.at("model")=="test-partner"); assert(data["messages"].back()["content"]=="안녕하세요"); Json reply={{"text","반가워요."},{"emotion","smile"},{"motion","Nod"}}; response.set_content(Json({{"choices",Json::array({{{"message",{{"content",reply.dump()}}}}})}}).dump(),"application/json"); });
        peer.start();
        Config config; config.load(directory,directory); config.obsUrl="ws://127.0.0.1:"+std::to_string(peer.port)+"/obs"; config.rvcUrl=peer.url(); config.agentUrl=peer.url()+"/v1";
        {
            Services services(config); services.agentKey="test-secret-key";
            services.connectObs("password"); waitFor(services,[&] { return services.obs.connected&&!services.obs.inputs.empty(); });
            assert(authenticated); assert(services.obs.scene=="대화"); assert(services.obs.inputs[0].db==-12);
            bool changed=false; services.obsCall("SetCurrentProgramScene",{{"sceneName","게임"}},[&](Json){changed=true;services.refreshObs();}); waitFor(services,[&]{return changed&&services.obs.scene=="게임";});
            bool recording=false; services.obsCall("StartRecord",{},[&](Json){recording=true;}); waitFor(services,[&]{return recording;}); assert(recorded);
            bool stopped=false; services.obsCall("StopRecord",{},[&](Json){stopped=true;}); waitFor(services,[&]{return stopped;}); assert(!recorded);
            services.connectRvc(); waitFor(services,[&]{return services.rvc.connected&&services.busy==0;});
            assert(services.rvc.info["modelSlots"].size()==1);
            assert(services.rvcReady());
            services.rvc.info["modelSlotIndex"]=99; assert(!services.rvcReady());
            services.startRvc(); assert(!services.rvcBusy && !services.error.empty()); services.error.clear();
            services.rvc.info["modelSlotIndex"]=0;
            services.rvc.info["modelSlots"][0]["voiceChangerType"]="Other"; assert(!services.rvcReady());
            services.rvc.info["modelSlots"][0]["voiceChangerType"]="RVC";
            services.setRvc("tran",7); waitFor(services,[&]{return services.busy==0;}); assert(services.rvc.info["tran"]==7);
            services.startRvc(); waitFor(services,[&]{return services.busy==0;}); assert(services.rvc.info["serverAudioStated"]==1);
            services.setRvc("serverAudioStated",0); waitFor(services,[&]{return services.busy==0;});
            config.rvcUrl=peer.url()+"/missing"; services.setRvc("tran",1); waitFor(services,[&]{return services.busy==0;}); assert(!services.rvc.connected); assert(!services.error.empty());
            services.error.clear(); config.rvcUrl=peer.url(); services.connectRvc(); waitFor(services,[&]{return services.busy==0&&services.rvc.connected;});
            std::vector<char> bytes(1024*1024+20,'x'); std::ofstream model(directory/"voice.pth",std::ios::binary); model.write(bytes.data(),bytes.size()); model.close(); std::ofstream(directory/"voice.index")<<"index";
            services.uploadRvc({(directory/"voice.pth").string(),(directory/"voice.index").string()}); waitFor(services,[&]{return services.busy==0;});
            assert(loaded); assert((chunks==std::vector<std::string>{"voice.pth_0","voice.pth_1","voice.index_0"}));
            services.probeAgent(); waitFor(services,[&]{return services.busy==0;}); assert(config.agentModel=="test-partner");
            services.chat("안녕하세요"); waitFor(services,[&]{return services.busy==0&&!services.chatBusy;}); assert(services.messages.size()==2); assert(services.signal.emotion=="smile"); assert(services.signal.motion=="Nod");
            std::ifstream settings(directory/"settings.json"); std::string persisted{std::istreambuf_iterator<char>(settings),std::istreambuf_iterator<char>()}; assert(persisted.find("test-secret-key")==std::string::npos); assert(persisted.find("password")==std::string::npos);
            { std::lock_guard lock(mutex); for (const auto& call:calls) assert(call.at("requestType")!="StartStream"); }
        }
        std::atomic<bool> slowEntered {false};
        peer.server.Get("/slow/info",[&](const auto&,auto& response){slowEntered=true;std::this_thread::sleep_for(std::chrono::milliseconds(600));response.set_content(info.dump(),"application/json");});
        config.rvcUrl=peer.url()+"/slow";
        double closing=0;
        {
            Services pending(config); pending.connectRvc();
            const double deadline=now()+3; while (!slowEntered && now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(slowEntered); closing=now();
        }
        assert(now()-closing<.5);
        peer.server.stop(); if (peer.thread.joinable()) peer.thread.join();
        fs::remove_all(directory); std::cout<<"OBS 인증·장면·녹화, RVC 설정·분할 업로드, 에이전트 대화·취소 통과\n"; return 0;
    } catch (const std::exception& error) { fs::remove_all(directory); std::cerr<<error.what()<<'\n'; return 1; }
}
