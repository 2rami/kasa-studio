#include "tracking.hpp"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace studio {
namespace {
float unit(const Json& shapes, const char* name) {
    if (!shapes.contains(name)) return 0;
    if (!shapes[name].is_number()) throw std::runtime_error("얼굴 추적 값의 형식을 확인해 주세요.");
    float value=shapes[name].get<float>();
    if (!std::isfinite(value)) throw std::runtime_error("유효하지 않은 얼굴 추적 값이에요.");
    return std::clamp(value,0.f,1.f);
}
float angle(const Json& rotation,const char* key) {
    if (!rotation.contains(key)||!rotation[key].is_number()) throw std::runtime_error("고개 회전 값이 필요해요.");
    float value=rotation[key].get<float>();
    if (!std::isfinite(value)||std::abs(value)>180) throw std::runtime_error("고개 회전 범위를 확인해 주세요.");
    return value;
}
}
FaceFrame decodeFacePacket(const Json& packet,const std::string& code,double receivedAt) {
    if (!packet.is_object()||packet.value("version",0)!=1||packet.value("code","")!=code) throw std::runtime_error("아이폰 연결 코드가 일치하지 않아요.");
    FaceFrame frame; frame.at=receivedAt;
    frame.session=packet.value("session","");
    if (frame.session.empty()||frame.session.size()>80||!packet.contains("sequence")||!packet["sequence"].is_number_unsigned()) throw std::runtime_error("아이폰 추적 세션을 확인해 주세요.");
    frame.sequence=packet["sequence"].get<uint64_t>();
    if (packet.value("type","frame")=="stop") return frame;
    frame.valid=packet.value("tracked",false);
    if (!frame.valid) return frame;
    const auto& rotation=packet.at("rotation"); const auto& shapes=packet.at("blendShapes");
    if (!rotation.is_object()||!shapes.is_object()) throw std::runtime_error("얼굴 추적 데이터가 필요해요.");
    frame.yaw=angle(rotation,"yaw"); frame.pitch=angle(rotation,"pitch"); frame.roll=angle(rotation,"roll");
    frame.eyeLeft=1-unit(shapes,"eyeBlinkLeft"); frame.eyeRight=1-unit(shapes,"eyeBlinkRight");
    frame.mouth=unit(shapes,"jawOpen")*(1-unit(shapes,"mouthClose"));
    frame.smile=std::clamp((unit(shapes,"mouthSmileLeft")+unit(shapes,"mouthSmileRight")-unit(shapes,"mouthFrownLeft")-unit(shapes,"mouthFrownRight"))*.5f,-1.f,1.f);
    frame.browLeft=std::clamp(unit(shapes,"browInnerUp")+unit(shapes,"browOuterUpLeft")-unit(shapes,"browDownLeft"),-1.f,1.f);
    frame.browRight=std::clamp(unit(shapes,"browInnerUp")+unit(shapes,"browOuterUpRight")-unit(shapes,"browDownRight"),-1.f,1.f);
    frame.gazeX=(unit(shapes,"eyeLookOutLeft")-unit(shapes,"eyeLookInLeft")+unit(shapes,"eyeLookInRight")-unit(shapes,"eyeLookOutRight"))*.5f;
    frame.gazeY=(unit(shapes,"eyeLookUpLeft")+unit(shapes,"eyeLookUpRight")-unit(shapes,"eyeLookDownLeft")-unit(shapes,"eyeLookDownRight"))*.5f;
    return frame;
}
PhoneTracker::~PhoneTracker() { stop(); }
bool PhoneTracker::start(int port,const std::string& code) {
    stop();
    if ((port!=0&&port<1024)||port>65535||code.size()!=8) { std::lock_guard lock(mutex_); error_="포트와 연결 코드를 확인해 주세요."; return false; }
    socket_=::socket(AF_INET,SOCK_DGRAM,0);
    if (socket_<0) { std::lock_guard lock(mutex_); error_="아이폰 수신 소켓을 만들 수 없어요."; return false; }
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_ANY); address.sin_port=htons(static_cast<uint16_t>(port));
    if (::bind(socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0) { ::close(socket_); socket_=-1; std::lock_guard lock(mutex_); error_="수신 포트가 사용 중이에요. 다른 포트를 선택해 주세요."; return false; }
    socklen_t boundLength=sizeof(address); ::getsockname(socket_,reinterpret_cast<sockaddr*>(&address),&boundLength);
    port_=ntohs(address.sin_port); stopping_=false; listening_=true;
    { std::lock_guard lock(mutex_); error_.clear(); frame_={}; }
    thread_=std::thread([this,code] {
        char buffer[8192]; double rateAt=now(),ackAt=0; int packets=0;
        while (!stopping_) {
            pollfd descriptor{socket_,POLLIN,0};
            if (::poll(&descriptor,1,100)<=0) continue;
            sockaddr_in peer{}; socklen_t length=sizeof(peer);
            const auto count=::recvfrom(socket_,buffer,sizeof(buffer),0,reinterpret_cast<sockaddr*>(&peer),&length);
            if (count<=0||count==sizeof(buffer)) continue;
            const double at=now(); if (at-rateAt>1) { rateAt=at; packets=0; } if (++packets>180) continue;
            try {
                auto packet=Json::parse(buffer,buffer+count);
                if(packet.value("type","")=="pair"&&packet.value("code","")==code){const auto nonce=packet.value("nonce","");std::string url;{std::lock_guard lock(mutex_);url=pairingUrl_;}if(!url.empty()&&nonce.size()==32){const auto reply=Json({{"type","pair"},{"nonce",nonce},{"url",url}}).dump();::sendto(socket_,reply.data(),reply.size(),0,reinterpret_cast<sockaddr*>(&peer),length);}continue;}
                if(packet.value("type","")=="ping"&&packet.value("code","")==code){const auto reply=Json({{"type","ack"},{"session",packet.value("session","")}}).dump();::sendto(socket_,reply.data(),reply.size(),0,reinterpret_cast<sockaddr*>(&peer),length);continue;}
                auto incoming=decodeFacePacket(packet,code,at);
                {
                    std::lock_guard lock(mutex_);
                    if (incoming.session==frame_.session && incoming.sequence<=frame_.sequence) continue;
                    frame_=incoming;
                }
                if (at-ackAt>.5) { ackAt=at; auto ack=Json({{"type","ack"},{"session",incoming.session},{"sequence",incoming.sequence}}).dump(); ::sendto(socket_,ack.data(),ack.size(),0,reinterpret_cast<sockaddr*>(&peer),length); }
            } catch (...) { /* 인증되지 않거나 손상된 패킷은 현재 얼굴 상태를 바꾸지 않는다. */ }
        }
    });
    return true;
}
void PhoneTracker::stop() {
    stopping_=true; listening_=false;
    if (thread_.joinable()) thread_.join();
    if (socket_>=0) ::close(socket_);
    socket_=-1; port_=0; std::lock_guard lock(mutex_); frame_={};pairingUrl_.clear();
}
void PhoneTracker::setPairingUrl(const std::string& url) {std::lock_guard lock(mutex_);pairingUrl_=url;}
FaceFrame PhoneTracker::frame() const { std::lock_guard lock(mutex_); return frame_; }
std::string PhoneTracker::error() const { std::lock_guard lock(mutex_); return error_; }
FaceTracker::FaceTracker(const fs::path& directory) : link(directory) { code=link.code(); }
void FaceTracker::refreshDevices() { devices=camera.devices(); }
bool FaceTracker::start(const TrackingSettings& settings) {
    stop(); error.clear(); calibrated=false; neutral_={};
    if (settings.source=="iphone") { if (!phone.start(settings.port,code)) { error=phone.error(); return false; } link.start(phone.port()); phone.setPairingUrl(link.connectionUrl(phone.port())); runningSource="iphone"; }
    else if (settings.source=="webcam") { camera.start(settings.deviceId); runningSource="webcam"; }
    else { error="얼굴 입력을 선택해 주세요."; return false; }
    return true;
}
void FaceTracker::stop() { camera.stop(); phone.stop(); link.stop(); runningSource.clear(); raw_={}; output_={}; }
bool FaceTracker::active() const { return runningSource=="iphone"?phone.listening():runningSource=="webcam"?camera.snapshot().active:false; }
bool FaceTracker::starting() const { return runningSource=="webcam"&&camera.snapshot().starting; }
void FaceTracker::update(double dt,const TrackingSettings& settings) {
    if (runningSource=="iphone") { link.update(); raw_=phone.frame(); auto remote=link.snapshot().frame; if (remote.at>raw_.at) raw_=remote; }
    else if (runningSource=="webcam") { auto state=camera.snapshot(); raw_=state.frame; if (!state.error.empty()) error=state.error; }
    else raw_={};
    const bool valid=active()&&raw_.valid&&now()-raw_.at<.6;
    FaceFrame target=raw_; target.valid=valid;
    if (!valid) target={};
    else {
        target.yaw=std::clamp(std::remainder(target.yaw-neutral_.yaw,360.f)*settings.sensitivity,-40.f,40.f);
        target.pitch=std::clamp(std::remainder(target.pitch-neutral_.pitch,360.f)*settings.sensitivity,-35.f,35.f);
        target.roll=std::clamp(std::remainder(target.roll-neutral_.roll,360.f)*settings.sensitivity,-40.f,40.f);
        if (calibrated) {
            target.mouth=std::clamp((target.mouth-neutral_.mouth)/std::max(.2f,1-neutral_.mouth),0.f,1.f);
            target.eyeLeft=std::clamp(target.eyeLeft/std::max(.3f,neutral_.eyeLeft),0.f,1.f); target.eyeRight=std::clamp(target.eyeRight/std::max(.3f,neutral_.eyeRight),0.f,1.f);
            target.gazeX=std::clamp(target.gazeX-neutral_.gazeX,-1.f,1.f); target.gazeY=std::clamp(target.gazeY-neutral_.gazeY,-1.f,1.f);
        }
        if (settings.mirror) { target.yaw=-target.yaw; target.roll=-target.roll; target.gazeX=-target.gazeX; std::swap(target.eyeLeft,target.eyeRight); std::swap(target.browLeft,target.browRight); }
    }
    const float weight=1-std::exp(-static_cast<float>(std::clamp(dt,0.0,.2))/std::max(.015f,settings.smoothing));
    auto follow=[&](float& current,float value){current+=(value-current)*weight;};
    follow(output_.yaw,target.yaw); follow(output_.pitch,target.pitch); follow(output_.roll,target.roll);
    follow(output_.eyeLeft,target.eyeLeft); follow(output_.eyeRight,target.eyeRight); follow(output_.mouth,target.mouth); follow(output_.smile,target.smile);
    follow(output_.browLeft,target.browLeft); follow(output_.browRight,target.browRight); follow(output_.gazeX,target.gazeX); follow(output_.gazeY,target.gazeY);
    output_.valid=valid; output_.at=raw_.at; output_.sequence=raw_.sequence;
    output_.contours=raw_.contours;
}
void FaceTracker::calibrate() { if (!raw_.valid||now()-raw_.at>.6) { error="정면의 얼굴이 감지된 뒤 보정해 주세요."; return; } neutral_=raw_; calibrated=true; error.clear(); }
std::vector<std::string> FaceTracker::addresses() {
    std::vector<std::string> result; ifaddrs* interfaces=nullptr;
    if (getifaddrs(&interfaces)!=0) return result;
    for (auto* current=interfaces;current;current=current->ifa_next) {
        if (!current->ifa_addr||current->ifa_addr->sa_family!=AF_INET||!(current->ifa_flags&IFF_UP)||(current->ifa_flags&IFF_LOOPBACK)) continue;
        if (!std::string(current->ifa_name).starts_with("en")) continue;
        char text[INET_ADDRSTRLEN]; auto* address=reinterpret_cast<sockaddr_in*>(current->ifa_addr);
        if (inet_ntop(AF_INET,&address->sin_addr,text,sizeof(text))) result.emplace_back(text);
    }
    freeifaddrs(interfaces); return result;
}
std::string FaceTracker::connectionUrl() const { return link.connectionUrl(phone.port()); }
Json FaceTracker::diagnostic() const { return {{"source",runningSource},{"active",active()},{"starting",starting()},{"face",output_.valid},{"sequence",output_.sequence},{"yaw",output_.yaw},{"pitch",output_.pitch},{"roll",output_.roll},{"eyeLeft",output_.eyeLeft},{"eyeRight",output_.eyeRight},{"mouth",output_.mouth},{"calibrated",calibrated},{"error",error}}; }
}
