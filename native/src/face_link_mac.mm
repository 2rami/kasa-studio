#import <Foundation/Foundation.h>
#include "tracking.hpp"
#include <deque>
#include <fstream>

@interface StudioFacePublisher : NSObject<NSNetServiceDelegate> {
@public std::function<void(bool)> status;
}
@end
@implementation StudioFacePublisher
- (void)netServiceDidPublish:(NSNetService*)sender {if(status)status(true);}
- (void)netService:(NSNetService*)sender didNotPublish:(NSDictionary<NSString*,NSNumber*>*)error {if(status)status(false);}
@end

namespace studio {
namespace {
NSString* ns(const std::string& value) { return [NSString stringWithUTF8String:value.c_str()]; }
bool hex(const std::string& value,size_t length) { return value.size()==length&&value.find_first_not_of("0123456789abcdef")==std::string::npos; }
}
struct PhoneLink::Impl {
    std::string room,key,code,url="wss://face.debimarlene.com/socket";
    NSNetService* service=nil;
    StudioFacePublisher* publisher=nil;
    platform::Socket socket;
    bool requested=false; double reconnectAt=0,ackAt=0;
    mutable std::mutex mutex;
    PhoneLinkState state;
    std::deque<std::pair<double,Json>> inbox;
    void push(Json value) { std::lock_guard lock(mutex); if(inbox.size()<128)inbox.emplace_back(now(),std::move(value)); }
};
PhoneLink::PhoneLink(const fs::path& directory) : impl_(std::make_shared<Impl>()) {
    auto self=impl_;
    if (const char* override=std::getenv("KASA_FACE_RELAY_URL")) self->url=override;
    const auto file=directory/"phone-pairing.json";
    if (!directory.empty() && fs::exists(file)) {
        try { auto value=readJson(file); self->room=value.value("room","");self->key=value.value("key",""); } catch(...) {}
    }
    if (!hex(self->room,32)||!hex(self->key,64)) {
        self->room=platform::randomToken().substr(0,32);self->key=(platform::randomToken()+platform::randomToken()).substr(0,64);
        if (!directory.empty()) { fs::create_directories(directory);const auto temp=directory/"phone-pairing.tmp";std::ofstream out(temp);out<<Json({{"room",self->room},{"key",self->key}}).dump();out.close();fs::permissions(temp,fs::perms::owner_read|fs::perms::owner_write);fs::rename(temp,file); }
    }
    self->code=std::to_string(10000000+std::stoul(self->key.substr(0,7),nullptr,16)%90000000);
}
PhoneLink::~PhoneLink() { stop(); }
std::string PhoneLink::code() const { return impl_->code; }
void PhoneLink::start(int port) {
    stop(); auto self=impl_; self->requested=true;
    if (!std::getenv("KASA_FACE_NO_DISCOVERY")) {
        @autoreleasepool {
            NSString* name=NSHost.currentHost.localizedName ?: NSProcessInfo.processInfo.hostName;
            self->service=[[NSNetService alloc] initWithDomain:@"local." type:@"_kasaface._udp." name:[name substringToIndex:MIN(name.length,50)] port:port];
            self->service.includesPeerToPeer=YES;
            self->publisher=[[StudioFacePublisher alloc] init];auto weak=std::weak_ptr<Impl>(self);
            self->publisher->status=[weak](bool published){if(auto current=weak.lock()){std::lock_guard lock(current->mutex);current->state.advertised=published;current->state.discoveryError=published?"":"시스템 설정에서 로컬 네트워크 접근을 확인해 주세요.";}};
            self->service.delegate=self->publisher;
            [self->service setTXTRecordData:[NSNetService dataFromTXTRecordDictionary:@{@"v":[@"1" dataUsingEncoding:NSUTF8StringEncoding],@"room":[ns(self->room) dataUsingEncoding:NSUTF8StringEncoding],@"code":[ns(self->code) dataUsingEncoding:NSUTF8StringEncoding]}]];
            [self->service publish];
        }
    }
    self->reconnectAt=0;update();
}
void PhoneLink::update() {
    auto self=impl_;if(!self->requested)return;
    std::deque<std::pair<double,Json>> messages;
    {std::lock_guard lock(self->mutex);messages.swap(self->inbox);}
    for (auto& [at,value]:messages) {
        try {
            const auto type=value.value("type","");
            if(type=="failure") {std::lock_guard lock(self->mutex);self->state.online=false;self->state.peer=false;self->state.error=value.value("error","");self->state.frame={};}
            else if(type=="ready") {std::lock_guard lock(self->mutex);self->state.online=true;self->state.error.clear();}
            else if(type=="peer") {std::lock_guard lock(self->mutex);self->state.peer=value.value("connected",false);if(!self->state.peer)self->state.frame={};}
            else {
                auto frame=decodeFacePacket(value,self->code,at);
                {std::lock_guard lock(self->mutex);if(frame.session==self->state.frame.session&&frame.sequence<=self->state.frame.sequence)continue;self->state.frame=frame;self->state.peer=true;}
                if(now()-self->ackAt>.4) {self->ackAt=now();self->socket.send(Json({{"type","ack"},{"session",frame.session},{"sequence",frame.sequence}}).dump());}
            }
        } catch(...) {}
    }
    if(self->url.empty()||self->url=="off"||snapshot().online||now()<self->reconnectAt)return;
    if(!self->url.starts_with("wss://")&&!self->url.starts_with("ws://127.0.0.1:"))return;
    self->reconnectAt=now()+4;
    auto weak=std::weak_ptr<Impl>(self);
    self->socket.connect(self->url,[weak](std::string text){if(auto current=weak.lock()){try{current->push(Json::parse(text));}catch(...){}}},[weak](std::string error){if(auto current=weak.lock())current->push({{"type","failure"},{"error",error}});},"");
    self->socket.send(Json({{"room",self->room},{"key",self->key},{"role","host"}}).dump());
}
void PhoneLink::stop() {
    auto self=impl_;self->requested=false;self->socket.close();
    self->service.delegate=nil;[self->service stop];self->service=nil;self->publisher=nil;
    std::lock_guard lock(self->mutex);self->state={};self->inbox.clear();
}
PhoneLinkState PhoneLink::snapshot() const {std::lock_guard lock(impl_->mutex);return impl_->state;}
std::string PhoneLink::connectionUrl(int port) const {
    @autoreleasepool {
        auto self=impl_;const auto addresses=FaceTracker::addresses();
        NSURLComponents* components=[[NSURLComponents alloc] init];components.scheme=@"kasaface";components.host=@"connect";
        components.queryItems=@[[NSURLQueryItem queryItemWithName:@"host" value:ns(addresses.empty()?"":addresses.front())],[NSURLQueryItem queryItemWithName:@"port" value:ns(std::to_string(port))],[NSURLQueryItem queryItemWithName:@"code" value:ns(self->code)],[NSURLQueryItem queryItemWithName:@"room" value:ns(self->room)],[NSURLQueryItem queryItemWithName:@"key" value:ns(self->key)],[NSURLQueryItem queryItemWithName:@"relay" value:ns(self->url)],[NSURLQueryItem queryItemWithName:@"name" value:NSHost.currentHost.localizedName ?: NSProcessInfo.processInfo.hostName]];
        return components.string.UTF8String;
    }
}
}
