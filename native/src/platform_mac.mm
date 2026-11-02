#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <ImageIO/ImageIO.h>
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>
#import <objc/runtime.h>
#import <CoreImage/CoreImage.h>
#include <mach/mach.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <mutex>

@interface StudioMenuTarget : NSObject {
@public std::function<void(int)> action;
}
- (void)performAction:(id)sender;
@end
@implementation StudioMenuTarget
- (void)performAction:(id)sender { if (action) action(static_cast<int>([sender tag])); }
@end

namespace studio::platform {
static thread_local const std::atomic<bool>* requestCancelled = nullptr;
void setRequestCancellation(const std::atomic<bool>* cancelled) { requestCancelled = cancelled; }
static NSString* ns(const std::string& value) { return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding]; }
Response request(const std::string& url, const std::string& method, const std::string& body, const std::map<std::string, std::string>& headers, double timeout) {
    @autoreleasepool {
        NSURL* address = [NSURL URLWithString:ns(url)];
        if (!address) return {0, "", "서버 주소를 확인해 주세요."};
        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:address];
        req.HTTPMethod = ns(method); req.timeoutInterval = timeout;
        if (!body.empty()) req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
        for (const auto& [key, value] : headers) [req setValue:ns(value) forHTTPHeaderField:ns(key)];
        auto promise = std::make_shared<std::promise<Response>>();
        auto future = promise->get_future();
        NSURLSessionDataTask* task = [NSURLSession.sharedSession dataTaskWithRequest:req completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
            Response result;
            if ([response isKindOfClass:NSHTTPURLResponse.class]) result.status = static_cast<int>(((NSHTTPURLResponse*)response).statusCode);
            if (data) result.body.assign(static_cast<const char*>(data.bytes), data.length);
            if (error) result.error = error.localizedDescription.UTF8String;
            promise->set_value(std::move(result));
        }];
        if (requestCancelled && *requestCancelled) return {0, "", "요청을 취소했어요."};
        [task resume];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>((timeout + 1) * 1000));
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            if (requestCancelled && *requestCancelled) { [task cancel]; return {0, "", "요청을 취소했어요."}; }
            if (std::chrono::steady_clock::now() > deadline) { [task cancel]; return {0, "", "서버 응답 시간이 초과됐어요."}; }
        }
        return future.get();
    }
}
struct Socket::Impl : std::enable_shared_from_this<Socket::Impl> {
    NSURLSessionWebSocketTask* task = nil;
    std::atomic<bool> alive {true};
    std::function<void(std::string)> message, error;
    void receive() {
        auto self = shared_from_this();
        [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* value, NSError* failure) {
            if (!self->alive) return;
            if (failure) { if (self->alive.exchange(false) && self->error) self->error(failure.localizedDescription.UTF8String); return; }
            if (value.type == NSURLSessionWebSocketMessageTypeString && self->message) self->message(value.string.UTF8String);
            else if (value.data && self->message) self->message(std::string(static_cast<const char*>(value.data.bytes), value.data.length));
            if (self->alive) self->receive();
        }];
    }
};
Socket::Socket() = default;
Socket::~Socket() { close(); }
void Socket::connect(const std::string& url, std::function<void(std::string)> message, std::function<void(std::string)> error, const std::string& protocol) {
    close(); impl_ = std::make_shared<Impl>(); impl_->message = std::move(message); impl_->error = std::move(error);
    @autoreleasepool {
        if (protocol.empty()) impl_->task = [NSURLSession.sharedSession webSocketTaskWithURL:[NSURL URLWithString:ns(url)]];
        else impl_->task = [NSURLSession.sharedSession webSocketTaskWithURL:[NSURL URLWithString:ns(url)] protocols:@[ns(protocol)]];
        [impl_->task resume]; impl_->receive();
    }
}
void Socket::send(const std::string& text) {
    if (!impl_ || !impl_->alive) return;
    @autoreleasepool {
        NSURLSessionWebSocketMessage* message = [[NSURLSessionWebSocketMessage alloc] initWithString:ns(text)];
        auto self = impl_;
        [self->task sendMessage:message completionHandler:^(NSError* error) { if (error && self->alive.exchange(false) && self->error) self->error(error.localizedDescription.UTF8String); }];
    }
}
void Socket::close() {
    if (impl_) { impl_->alive = false; [impl_->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil]; impl_.reset(); }
}
Image loadImage(const std::string& path, int limit) {
    Image out;
    @autoreleasepool {
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:ns(path)], nullptr);
        if (!source) return out;
        NSDictionary* options = @{ (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways:@YES, (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize:@(limit), (__bridge NSString*)kCGImageSourceShouldCache:@NO };
        CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
        if (image) {
            out.width = static_cast<int>(CGImageGetWidth(image)); out.height = static_cast<int>(CGImageGetHeight(image));
            out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
            CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            CGContextRef context = CGBitmapContextCreate(out.rgba.data(), out.width, out.height, 8, out.width * 4, colors, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            CGContextSetBlendMode(context, kCGBlendModeCopy); CGContextDrawImage(context, CGRectMake(0, 0, out.width, out.height), image);
            CGContextRelease(context); CGColorSpaceRelease(colors); CGImageRelease(image);
        }
        CFRelease(source);
    }
    return out;
}
Image loadImageBase64(const std::string& source) {
    Image out;
    @autoreleasepool {
        const auto comma = source.find(',');
        NSData* data = [[NSData alloc] initWithBase64EncodedString:ns(comma == std::string::npos ? source : source.substr(comma + 1)) options:0];
        CGImageSourceRef reader = data ? CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr) : nullptr;
        CGImageRef image = reader ? CGImageSourceCreateImageAtIndex(reader, 0, nullptr) : nullptr;
        if (image) {
            out.width = static_cast<int>(CGImageGetWidth(image)); out.height = static_cast<int>(CGImageGetHeight(image));
            if (out.width <= 2048 && out.height <= 2048) {
                out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
                CGColorSpaceRef color = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                CGContextRef context = CGBitmapContextCreate(out.rgba.data(), out.width, out.height, 8, out.width * 4, color, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
                CGContextDrawImage(context, CGRectMake(0, 0, out.width, out.height), image); CGContextRelease(context); CGColorSpaceRelease(color);
            }
            CGImageRelease(image);
        }
        if (reader) CFRelease(reader);
    }
    return out;
}
bool writePng(const std::string& path, int width, int height, const unsigned char* rgba) {
    @autoreleasepool {
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(const_cast<unsigned char*>(rgba), width, height, 8, width * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGImageRef image = CGBitmapContextCreateImage(context);
        CGImageDestinationRef destination = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:ns(path)], CFSTR("public.png"), 1, nullptr);
        bool success = false;
        if (destination) { CGImageDestinationAddImage(destination, image, nullptr); success = CGImageDestinationFinalize(destination); CFRelease(destination); }
        CGImageRelease(image); CGContextRelease(context); CGColorSpaceRelease(space); return success;
    }
}
Image qrImage(const std::string& text) {
    Image result;
    @autoreleasepool {
        CIFilter* filter=[CIFilter filterWithName:@"CIQRCodeGenerator"];
        [filter setValue:[NSData dataWithBytes:text.data() length:text.size()] forKey:@"inputMessage"]; [filter setValue:@"M" forKey:@"inputCorrectionLevel"];
        CIImage* image=filter.outputImage; if (!image) return result;
        const CGFloat scale=std::max(1.0,std::floor(240.0/image.extent.size.width)); image=[image imageByApplyingTransform:CGAffineTransformMakeScale(scale,scale)];
        CIContext* renderer=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES}];
        CGImageRef cg=[renderer createCGImage:image fromRect:image.extent]; if (!cg) return result;
        result.width=static_cast<int>(CGImageGetWidth(cg)); result.height=static_cast<int>(CGImageGetHeight(cg)); result.rgba.resize(result.width*result.height*4);
        CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context=CGBitmapContextCreate(result.rgba.data(),result.width,result.height,8,result.width*4,color,static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast)|kCGBitmapByteOrder32Big);
        CGContextDrawImage(context,CGRectMake(0,0,result.width,result.height),cg); CGContextRelease(context); CGColorSpaceRelease(color); CGImageRelease(cg);
    }
    return result;
}
std::string resources() { @autoreleasepool { return NSBundle.mainBundle.resourcePath.UTF8String; } }
std::string dataDirectory() {
    if (auto* value = getenv("KASA_STUDIO_NATIVE_DATA")) return value;
    @autoreleasepool { return [[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Kasa Studio Native"] UTF8String]; }
}
std::string pickFile(const std::string& title, const std::vector<std::string>& extensions) {
    @autoreleasepool {
        NSOpenPanel* panel = NSOpenPanel.openPanel; panel.title = ns(title); panel.canChooseDirectories = NO; panel.allowsMultipleSelection = NO;
        NSMutableArray* types = NSMutableArray.array; for (const auto& extension : extensions) [types addObject:ns(extension)]; panel.allowedFileTypes = types;
        return [panel runModal] == NSModalResponseOK ? std::string(panel.URL.path.UTF8String) : "";
    }
}
void copyText(const std::string& text) { @autoreleasepool { [NSPasteboard.generalPasteboard clearContents]; [NSPasteboard.generalPasteboard setString:ns(text) forType:NSPasteboardTypeString]; } }
void openUrl(const std::string& url) { @autoreleasepool { if (url.starts_with("https://")) [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:ns(url)]]; } }
std::string sha256Base64(const std::string& text) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(text.data(), static_cast<CC_LONG>(text.size()), digest);
    @autoreleasepool { return [[NSData dataWithBytes:digest length:sizeof(digest)] base64EncodedStringWithOptions:0].UTF8String; }
}
std::string randomToken() {
    unsigned char bytes[24];
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(bytes), bytes) != errSecSuccess) throw std::runtime_error("연결 토큰을 만들 수 없어요.");
    static const char* hex = "0123456789abcdef"; std::string result; result.reserve(48);
    for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; } return result;
}
void configurePet(GLFWwindow* window) {
    @autoreleasepool { NSWindow* native = glfwGetCocoaWindow(window); native.opaque = NO; native.backgroundColor = NSColor.clearColor; native.hasShadow = NO; native.level = NSFloatingWindowLevel; native.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary; }
}
void petCaption(GLFWwindow* window, const std::string& text) {
    @autoreleasepool {
        static char captionKey;
        NSWindow* native = glfwGetCocoaWindow(window);
        NSTextField* field = objc_getAssociatedObject(native, &captionKey);
        if (!field) {
            field = [[NSTextField alloc] initWithFrame:NSZeroRect]; field.editable = NO; field.selectable = NO; field.bordered = NO; field.drawsBackground = YES; field.backgroundColor = [NSColor colorWithWhite:.98 alpha:1]; field.textColor = NSColor.labelColor; field.font = [NSFont systemFontOfSize:14]; field.alignment = NSTextAlignmentLeft;
            field.wantsLayer = YES; field.layer.cornerRadius = 10; field.layer.masksToBounds = YES;
            [native.contentView addSubview:field]; objc_setAssociatedObject(native, &captionKey, field, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        if (![field.stringValue isEqualToString:ns(text)]) field.stringValue = ns(text);
        auto size = native.contentView.bounds.size; field.frame = NSMakeRect(18, size.height - 110, size.width - 36, 86); field.hidden = text.empty();
    }
}
static NSStatusItem* statusItem;
static StudioMenuTarget* menuTarget;
void installMenu(std::function<void(int)> action) {
    @autoreleasepool {
        menuTarget = [[StudioMenuTarget alloc] init]; menuTarget->action = std::move(action);
        statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength]; statusItem.button.title = @"K";
        NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Kasa Studio"];
        NSArray* labels = @[@"작업실 열기", @"펫 클릭 통과 해제", @"펫 숨기기", @"종료"];
        for (NSInteger i = 0; i < labels.count; ++i) { NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:labels[i] action:@selector(performAction:) keyEquivalent:@""]; item.target = menuTarget; item.tag = i; [menu addItem:item]; }
        statusItem.menu = menu;
    }
}
void removeMenu() { @autoreleasepool { if (statusItem) [NSStatusBar.systemStatusBar removeStatusItem:statusItem]; statusItem = nil; menuTarget = nil; } }
void clickThrough(GLFWwindow* window, bool enabled) { @autoreleasepool { NSWindow* native = glfwGetCocoaWindow(window); native.ignoresMouseEvents = enabled; } }
void globalMouse(double& x, double& y) { @autoreleasepool { NSPoint point = NSEvent.mouseLocation; x = point.x; y = NSScreen.screens.firstObject.frame.size.height - point.y; } }
double residentMegabytes() {
    mach_task_basic_info_data_t info; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS ? info.resident_size / 1048576.0 : 0;
}
double footprintMegabytes() {
    task_vm_info_data_t info; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS ? info.phys_footprint / 1048576.0 : 0;
}
void drainEvents() { @autoreleasepool { CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.0,false); } }

struct Microphone::Impl {
    AVAudioEngine* engine = nil;
    std::atomic<bool> requested {false}, running {false}; std::atomic<float> rms {0};
    mutable std::mutex mutex; std::string failure;
    void begin() {
        if (!requested || running) return;
        @autoreleasepool {
            engine = [[AVAudioEngine alloc] init];
            AVAudioInputNode* input = engine.inputNode;
            AVAudioFormat* format = [input outputFormatForBus:0];
            if (!format || format.sampleRate <= 0 || format.channelCount == 0) { std::lock_guard lock(mutex); failure = "사용 가능한 마이크가 없어요."; return; }
            [input installTapOnBus:0 bufferSize:512 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
                const float* values = buffer.floatChannelData ? buffer.floatChannelData[0] : nullptr;
                if (!values || buffer.frameLength == 0) return;
                double sum = 0; for (AVAudioFrameCount i = 0; i < buffer.frameLength; ++i) sum += values[i] * values[i];
                rms = std::clamp(static_cast<float>((std::sqrt(sum / buffer.frameLength) - .008) * 12), 0.f, 1.f);
            }];
            NSError* error = nil;
            if (![engine startAndReturnError:&error]) { [input removeTapOnBus:0]; std::lock_guard lock(mutex); failure = error.localizedDescription.UTF8String; engine = nil; return; }
            running = true;
        }
    }
};
Microphone::Microphone() : impl_(std::make_shared<Impl>()) {}
Microphone::~Microphone() { stop(); }
void Microphone::start() {
    impl_->requested = true; { std::lock_guard lock(impl_->mutex); impl_->failure.clear(); }
    auto state = impl_;
    auto permission = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (permission == AVAuthorizationStatusAuthorized) state->begin();
    else if (permission == AVAuthorizationStatusNotDetermined) [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) { dispatch_async(dispatch_get_main_queue(), ^{ if (granted) state->begin(); else { std::lock_guard lock(state->mutex); state->failure = "시스템 설정에서 마이크 접근을 허용해 주세요."; } }); }];
    else { std::lock_guard lock(state->mutex); state->failure = "시스템 설정에서 마이크 접근을 허용해 주세요."; }
}
void Microphone::stop() { impl_->requested = false; if (impl_->engine) { [impl_->engine stop]; [impl_->engine.inputNode removeTapOnBus:0]; impl_->engine = nil; } impl_->running = false; impl_->rms = 0; }
bool Microphone::active() const { return impl_->running; }
float Microphone::level() const { return impl_->rms; }
std::string Microphone::error() const { std::lock_guard lock(impl_->mutex); return impl_->failure; }
static NSSpeechSynthesizer* speech;
void speak(const std::string& text) { @autoreleasepool { if (!speech) { speech = [[NSSpeechSynthesizer alloc] init]; [speech setVoice:@"com.apple.speech.synthesis.voice.yuna"]; } [speech stopSpeaking]; [speech startSpeakingString:ns(text)]; } }
void stopSpeech() { [speech stopSpeaking]; }
bool speaking() { return speech.isSpeaking; }
}
