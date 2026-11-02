#include "tracking.hpp"
#include <cassert>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

using namespace studio;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    setenv("KASA_FACE_RELAY_URL",argv[1],1);setenv("KASA_FACE_NO_DISCOVERY","1",1);
    PhoneLink link;link.start(4782);
    auto wait=[&](const char* phase,auto condition){const auto deadline=now()+15;while(!condition()&&now()<deadline){link.update();platform::drainEvents();std::this_thread::sleep_for(std::chrono::milliseconds(10));}if(!condition()){std::cerr<<phase<<": "<<link.snapshot().error<<'\n';std::exit(1);}};
    wait("host ready",[&]{return link.snapshot().online;});
    const auto url=link.connectionUrl(4782);
    auto value=[&](const std::string& key){const auto from=url.find(key+"=");assert(from!=std::string::npos);const auto start=from+key.size()+1;return url.substr(start,url.find('&',start)-start);};
    std::mutex mutex;std::deque<Json> inbox;bool failed=false;
    platform::Socket phone;
    phone.connect(argv[1],[&](std::string text){std::lock_guard lock(mutex);inbox.push_back(Json::parse(text));},[&](std::string){std::lock_guard lock(mutex);failed=true;},"");
    phone.send(Json({{"role","phone"},{"room",value("room")},{"key",value("key")}}).dump());
    wait("phone joined",[&]{return link.snapshot().peer;});
    Json frame={{"type","frame"},{"version",1},{"code",link.code()},{"session","native-relay-probe"},{"sequence",uint64_t(1)},{"tracked",true},{"rotation",{{"yaw",12},{"pitch",-4},{"roll",2}}},{"blendShapes",{{"jawOpen",.5}}}};
    phone.send(frame.dump());wait("face received",[&]{return link.snapshot().frame.valid;});
    assert(link.snapshot().frame.yaw==12&&link.snapshot().frame.mouth==.5f);
    wait("ack received",[&]{std::lock_guard lock(mutex);for(const auto& item:inbox)if(item.value("type","")=="ack")return true;return false;});
    frame["type"]="stop";frame["sequence"]=uint64_t(2);phone.send(frame.dump());wait("stopped",[&]{return link.snapshot().frame.sequence==2;});assert(!link.snapshot().frame.valid);
    phone.close();link.stop();
    {std::lock_guard lock(mutex);assert(!failed);}
    std::cout<<"네이티브 WebSocket → 서울 중계 → 얼굴 수신·ACK·중지 검증 통과\n";
}
