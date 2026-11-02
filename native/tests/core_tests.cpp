#include "state.hpp"
#include "platform.hpp"
#include "services.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>

static void zip(const studio::fs::path& file,const std::vector<std::pair<std::string,std::string>>& entries) {
    auto* archive=archive_write_new(); archive_write_set_format_zip(archive); assert(archive_write_open_filename(archive,file.c_str())==ARCHIVE_OK);
    for (const auto& [name,content]:entries) {
        auto* entry=archive_entry_new(); archive_entry_set_pathname(entry,name.c_str()); archive_entry_set_filetype(entry,AE_IFREG); archive_entry_set_perm(entry,0644); archive_entry_set_size(entry,content.size());
        assert(archive_write_header(archive,entry)==ARCHIVE_OK); assert(archive_write_data(archive,content.data(),content.size())==static_cast<la_ssize_t>(content.size())); archive_entry_free(entry);
    }
    archive_write_close(archive); archive_write_free(archive);
}

int main() {
    using namespace studio;
    const auto directory=fs::temp_directory_path()/("kasa-native-test-"+platform::randomToken().substr(0,12));
    fs::create_directories(directory);
    try {
        assert(!safeRelative("../private")); assert(!safeRelative("/etc/private")); assert(!safeRelative("x/../../private")); assert(!safeRelative("x\\private")); assert(safeRelative("textures/tex.png"));
        assert(serviceUrlValid("ws://127.0.0.1:4455",true)); assert(!serviceUrlValid("file:///tmp/test")); assert(!serviceUrlValid("http://user:pass@localhost"));
        validateSignal({{"text","준비됐어요."},{"emotion","smile"},{"motion","Nod"}});
        bool invalid=false; try { validateSignal({{"emotion","invalid"}}); } catch (...) { invalid=true; } assert(invalid);
        invalid=false; try { validateRvcSetting("tran",200); } catch (...) { invalid=true; } assert(invalid);
        validateRvcSetting("tran",7); validateRvcSetting("f0Detector","rmvpe");
        assert(platform::sha256Base64("abc")=="ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
        Config config; config.load(directory,directory); config.agentName="테스트 파트너"; config.avatar.scale=1.2; config.save();
        Config restored; restored.load(directory,directory); assert(restored.agentName=="테스트 파트너"); assert(restored.avatar.scale>1.19f);
        assert(restored.tracking.sensitivity==2.f);
        auto legacy=config.json();legacy["version"]=3;legacy["tracking"]["sensitivity"]=1.f;
        std::ofstream(directory/"settings.json")<<legacy.dump();restored.load(directory,directory);assert(restored.tracking.sensitivity==2.f);
        legacy["tracking"]["sensitivity"]=1.5f;std::ofstream(directory/"settings.json")<<legacy.dump();restored.load(directory,directory);assert(restored.tracking.sensitivity==1.5f);
        legacy["version"]=4;legacy["tracking"]["sensitivity"]=1.f;std::ofstream(directory/"settings.json")<<legacy.dump();restored.load(directory,directory);assert(restored.tracking.sensitivity==1.f);
        restored.tracking.sensitivity=3.5f;restored.save();Config highGain;highGain.load(directory,directory);assert(highGain.tracking.sensitivity==3.5f);
        fs::create_directories(directory/"model/textures"); std::ofstream(directory/"model/avatar.moc3")<<"MOC3"; std::ofstream(directory/"model/textures/tex.png")<<"test";
        std::ofstream(directory/"model/avatar.model3.json")<<Json({{"Version",3},{"FileReferences",{{"Moc","avatar.moc3"},{"Textures",Json::array({"textures/tex.png"})}}}}).dump();
        auto model=inspectModel(directory/"model/avatar.model3.json"); assert(model.name=="avatar");
        const auto manifest=readJson(directory/"model/avatar.model3.json").dump();
        zip(directory/"model.zip",{{"avatar/avatar.model3.json",manifest},{"avatar/avatar.moc3","MOC3"},{"avatar/textures/tex.png","test"}});
        auto imported=importModel(directory/"model.zip",directory); assert(imported.name=="avatar"); assert(fs::exists(imported.path));
        zip(directory/"escape.zip",{{"../escape.model3.json",manifest}});
        invalid=false; try { importModel(directory/"escape.zip",directory); } catch (...) { invalid=true; } assert(invalid);
        assert(!fs::exists(directory/"models/escape.model3.json"));
        {
            Services services(config); assert(services.localPort>0);
            const auto origin="http://127.0.0.1:"+std::to_string(services.localPort);
            auto health=platform::request(origin+"/api/health"); assert(health.status==200);
            auto denied=platform::request(origin+"/api/agent/signal","POST","{}",{{"Content-Type","application/json"}}); assert(denied.status==401);
            std::ifstream input(directory/"agent-token"); std::string token; input>>token;
            auto accepted=platform::request(origin+"/api/agent/signal","POST",Json({{"text","반가워요."},{"emotion","smile"}}).dump(),{{"Content-Type","application/json"},{"X-Studio-Token",token}}); assert(accepted.status==200);
            services.update(); assert(services.signal.emotion=="smile");
            auto rejected=platform::request(origin+"/api/agent/signal","POST","{}",{{"Origin","https://example.com"},{"X-Studio-Token",token}}); assert(rejected.status==401);
            assert(services.diagnostic().dump().find(token)==std::string::npos);
        }
        fs::remove_all(directory); std::cout<<"설정·모델 경로·OBS 인증 해시·RVC 검증·에이전트 수신 확인 완료\n"; return 0;
    } catch (const std::exception& error) { fs::remove_all(directory); std::cerr<<error.what()<<'\n'; return 1; }
}
