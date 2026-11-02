#include "tracking.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>

using namespace studio;
static Json packet(uint64_t sequence,const std::string& code="12345678") {
    return {{"version",1},{"code",code},{"session","tracking-test"},{"sequence",sequence},{"tracked",true},{"rotation",{{"yaw",15},{"pitch",-8},{"roll",4}}},{"blendShapes",{{"eyeBlinkLeft",1},{"eyeBlinkRight",0},{"jawOpen",.6},{"mouthSmileLeft",.5},{"mouthSmileRight",.5}}}};
}
static void send(int port,const Json& data) {
    const int socket=::socket(AF_INET,SOCK_DGRAM,0); assert(socket>=0);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);inet_pton(AF_INET,"127.0.0.1",&address.sin_addr);
    const auto bytes=data.dump();assert(::sendto(socket,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&address),sizeof(address))==static_cast<ssize_t>(bytes.size()));::close(socket);
}
int main(int argc,char** argv) {
    setenv("KASA_FACE_RELAY_URL","off",1);setenv("KASA_FACE_NO_DISCOVERY","1",1);
    try {
        const auto identityDirectory=fs::temp_directory_path()/("kasa-face-identity-"+platform::randomToken());
        {PhoneLink first(identityDirectory),second(identityDirectory);assert(first.connectionUrl(4782)==second.connectionUrl(4782));assert(readJson(identityDirectory/"phone-pairing.json").at("key").get<std::string>().size()==64);}
        fs::remove_all(identityDirectory);
        auto frame=decodeFacePacket(packet(1),"12345678",now());
        assert(frame.valid&&frame.yaw==15&&frame.pitch==-8&&frame.eyeLeft==0&&frame.eyeRight==1);assert(std::abs(frame.mouth-.6f)<.01);
        bool rejected=false;try{decodeFacePacket(packet(2,"wrong"),"12345678",now());}catch(...){rejected=true;}assert(rejected);
        auto invalid=packet(3);invalid["rotation"]["yaw"]=200;rejected=false;try{decodeFacePacket(invalid,"12345678",now());}catch(...){rejected=true;}assert(rejected);
        PhoneTracker receiver;assert(receiver.start(0,"12345678"));
        send(receiver.port(),packet(3));
        for(int i=0;i<100&&receiver.frame().sequence!=3;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(receiver.frame().valid&&receiver.frame().sequence==3);
        send(receiver.port(),packet(2));send(receiver.port(),packet(4,"wrong"));std::this_thread::sleep_for(std::chrono::milliseconds(30));assert(receiver.frame().sequence==3);
        auto stop=packet(4);stop["type"]="stop";send(receiver.port(),stop);
        for(int i=0;i<100&&receiver.frame().sequence!=4;++i)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(!receiver.frame().valid);receiver.stop();assert(!receiver.listening());
        FaceTracker controller;TrackingSettings settings;assert(settings.sensitivity==2.f);settings.sensitivity=1;settings.source="iphone";settings.port=47820;settings.mirror=false;
        for(int port=47820;port<47840;++port){settings.port=port;if(controller.start(settings))break;}
        assert(controller.active());send(controller.phone.port(),packet(1,controller.code));
        for(int i=0;i<100&&!controller.hasFace();++i){controller.update(.033,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        assert(controller.hasFace());controller.calibrate();assert(controller.calibrated);
        auto moved=packet(2,controller.code);moved["rotation"]["yaw"]=25;send(controller.phone.port(),moved);
        for(int i=0;i<15;++i){controller.update(.033,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        assert(controller.frame().yaw>8&&controller.frame().yaw<11);
        settings.sensitivity=2;
        auto small=packet(3,controller.code);small["rotation"]["yaw"]=18;send(controller.phone.port(),small);
        for(int i=0;i<20;++i){controller.update(.033,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        assert(controller.frame().yaw>5.8f&&controller.frame().yaw<6.2f);
        settings.sensitivity=1;moved["sequence"]=uint64_t(4);send(controller.phone.port(),moved);
        for(int i=0;i<15;++i){controller.update(.033,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        settings.mirror=true;
        for(int i=0;i<15;++i)controller.update(.033,settings);
        assert(controller.frame().yaw < -8 && controller.frame().eyeLeft > .98f && controller.frame().eyeRight < .02f);
        settings.mirror=false;
        auto edge=packet(5,controller.code);edge["rotation"]["yaw"]=179;send(controller.phone.port(),edge);
        for(int i=0;i<100&&controller.frame().sequence!=5;++i){controller.update(.033,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        controller.calibrate();
        edge["sequence"]=uint64_t(6);edge["rotation"]["yaw"]=-179;send(controller.phone.port(),edge);
        for(int i=0;i<20;++i){controller.update(.033,settings);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        assert(controller.frame().yaw>1.5f&&controller.frame().yaw<2.5f);
        std::this_thread::sleep_for(std::chrono::milliseconds(620));controller.update(.033,settings);assert(!controller.hasFace());controller.stop();
        if(argc>1){auto detected=FaceCamera::analyzeImage(argv[1]);assert(detected.valid);assert(std::isfinite(detected.yaw)&&std::isfinite(detected.pitch));std::cout<<"Vision 정지 영상 얼굴 검출 통과\n";}
        std::cout<<"아이폰 인증·순서·UDP·보정·입력 끊김 복구 통과\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
