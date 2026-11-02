#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <CoreVideo/CoreVideo.h>
#include "tracking.hpp"
#include <algorithm>
#include <cmath>

namespace {
struct Bounds { float left=1,right=0,top=0,bottom=1; };
Bounds bounds(VNFaceLandmarkRegion2D* region) {
    Bounds result;
    if (!region) return result;
    for (NSUInteger i=0;i<region.pointCount;++i) { const auto point=region.normalizedPoints[i]; const float x=static_cast<float>(point.x),y=static_cast<float>(point.y); result.left=std::min(result.left,x); result.right=std::max(result.right,x); result.top=std::max(result.top,y); result.bottom=std::min(result.bottom,y); }
    return result;
}
float openness(VNFaceLandmarkRegion2D* region,float closed,float open) {
    auto b=bounds(region); const float ratio=(b.top-b.bottom)/std::max(.001f,b.right-b.left);
    return region?std::clamp((ratio-closed)/(open-closed),0.f,1.f):1.f;
}
studio::FaceFrame observation(VNFaceObservation* face) {
    studio::FaceFrame frame; frame.at=studio::now();
    if (!face||!face.landmarks||face.confidence<.4) return frame;
    frame.valid=true;
    constexpr float degrees=180.f/3.14159265358979323846f;
    frame.yaw=face.yaw.floatValue*degrees; frame.pitch=face.pitch.floatValue*degrees; frame.roll=face.roll.floatValue*degrees;
    auto landmarks=face.landmarks;
    auto contour=[&](VNFaceLandmarkRegion2D* region,bool closed) {
        if(!region||!region.pointCount)return;
        studio::FaceContour line;line.closed=closed;
        for(NSUInteger i=0;i<region.pointCount;++i){const auto p=region.normalizedPoints[i];line.points.push_back({static_cast<float>(face.boundingBox.origin.x+p.x*face.boundingBox.size.width),1-static_cast<float>(face.boundingBox.origin.y+p.y*face.boundingBox.size.height)});}
        frame.contours.push_back(std::move(line));
    };
    contour(landmarks.faceContour,false);contour(landmarks.leftEye,true);contour(landmarks.rightEye,true);
    contour(landmarks.leftEyebrow,false);contour(landmarks.rightEyebrow,false);contour(landmarks.nose,false);
    contour(landmarks.noseCrest,false);contour(landmarks.outerLips,true);contour(landmarks.innerLips,true);
    contour(landmarks.leftPupil,false);contour(landmarks.rightPupil,false);
    const auto left=bounds(landmarks.leftEye),right=bounds(landmarks.rightEye);
    if (landmarks.leftEye && landmarks.rightEye) {
        const float lx=(left.left+left.right)*.5f,ly=(left.top+left.bottom)*.5f,rx=(right.left+right.right)*.5f,ry=(right.top+right.bottom)*.5f;
        if (std::abs(rx-lx)>.01) { const float direction=rx>lx?1.f:-1.f; frame.roll=std::atan2((ry-ly)*direction,(rx-lx)*direction)*degrees; }
    }
    frame.eyeLeft=openness(landmarks.leftEye,.045f,.23f); frame.eyeRight=openness(landmarks.rightEye,.045f,.23f);
    frame.mouth=openness(landmarks.innerLips,.06f,.50f);
    if (!landmarks.innerLips) frame.mouth=0;
    auto pupil=[&](VNFaceLandmarkRegion2D* eye,VNFaceLandmarkRegion2D* center) {
        if (!eye||!center||!center.pointCount) return std::pair<float,float>{0,0};
        auto b=bounds(eye); auto point=center.normalizedPoints[0]; const float x=static_cast<float>(point.x),y=static_cast<float>(point.y);
        return std::pair<float,float>{std::clamp((x-(b.left+b.right)*.5f)/std::max(.01f,(b.right-b.left)*.5f),-1.f,1.f),std::clamp((y-(b.top+b.bottom)*.5f)/std::max(.01f,(b.top-b.bottom)*.8f),-1.f,1.f)};
    };
    auto l=pupil(landmarks.leftEye,landmarks.leftPupil),r=pupil(landmarks.rightEye,landmarks.rightPupil); frame.gazeX=(l.first+r.first)*.5f; frame.gazeY=(l.second+r.second)*.5f;
    return frame;
}
VNFaceObservation* largest(NSArray<VNFaceObservation*>* faces) {
    VNFaceObservation* result=nil; CGFloat area=0;
    for (VNFaceObservation* face in faces) { const CGFloat next=face.boundingBox.size.width*face.boundingBox.size.height; if (next>area) { result=face; area=next; } }
    return result;
}
NSArray<AVCaptureDevice*>* videoDevices() {
    NSMutableArray<AVCaptureDeviceType>* types=[NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
    if (@available(macOS 14.0,*)) { [types addObject:AVCaptureDeviceTypeExternal]; [types addObject:AVCaptureDeviceTypeContinuityCamera]; }
    else [types addObject:AVCaptureDeviceTypeExternalUnknown];
    return [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified].devices;
}
}
@interface StudioFaceSink : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate> {
@public std::function<void(CMSampleBufferRef)> consume;
}
@end
@implementation StudioFaceSink
- (void)captureOutput:(AVCaptureOutput*)output didOutputSampleBuffer:(CMSampleBufferRef)buffer fromConnection:(AVCaptureConnection*)connection { if (consume) consume(buffer); }
@end

namespace studio {
struct FaceCamera::Impl : std::enable_shared_from_this<FaceCamera::Impl> {
    std::atomic<bool> requested{false};
    std::atomic<uint64_t> generation{0};
    mutable std::mutex mutex;
    FaceCameraState state;
    AVCaptureSession* session=nil;
    StudioFaceSink* sink=nil;
    dispatch_queue_t queue=dispatch_queue_create("com.kasa.studio.face",DISPATCH_QUEUE_SERIAL);
    double analyzedAt=0; uint64_t sequence=0;
    void capture(CMSampleBufferRef sample) {
        if (!requested || now()-analyzedAt<1.0/30) return;
        analyzedAt=now();
        @autoreleasepool {
            CVPixelBufferRef buffer=CMSampleBufferGetImageBuffer(sample); if (!buffer) return;
            VNDetectFaceLandmarksRequest* request=[[VNDetectFaceLandmarksRequest alloc] init]; request.revision=VNDetectFaceLandmarksRequestRevision3;
            VNImageRequestHandler* handler=[[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer orientation:kCGImagePropertyOrientationUp options:@{}];
            NSError* error=nil; bool success=[handler performRequests:@[request] error:&error];
            FaceFrame frame=success?observation(largest(request.results)):FaceFrame{}; frame.at=now(); frame.sequence=++sequence;
            platform::Image image;
            CVPixelBufferLockBaseAddress(buffer,kCVPixelBufferLock_ReadOnly);
            const int width=static_cast<int>(CVPixelBufferGetWidth(buffer)),height=static_cast<int>(CVPixelBufferGetHeight(buffer));
            const size_t stride=CVPixelBufferGetBytesPerRow(buffer); auto* source=static_cast<const unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
            if (source && width>0 && width<=1920 && height<=1080) {
                image.width=width; image.height=height; image.rgba.resize(static_cast<size_t>(width)*height*4);
                for (int y=0;y<height;++y) for (int x=0;x<width;++x) { const auto* p=source+y*stride+x*4; auto* q=image.rgba.data()+(static_cast<size_t>(y)*width+x)*4; q[0]=p[2]; q[1]=p[1]; q[2]=p[0]; q[3]=255; }
            }
            CVPixelBufferUnlockBaseAddress(buffer,kCVPixelBufferLock_ReadOnly);
            if (!requested) return;
            std::lock_guard lock(mutex); state.frame=frame; state.preview=std::move(image); ++state.previewSerial;
            state.error=success?"":std::string("얼굴 분석을 다시 시도하고 있어요.");
        }
    }
    void begin(const std::string& selected) {
        if (!requested) return;
        @autoreleasepool {
            AVCaptureDevice* device=nil;
            for (AVCaptureDevice* item in videoDevices()) if (selected.empty()||[item.uniqueID isEqualToString:[NSString stringWithUTF8String:selected.c_str()]]) { device=item; break; }
            if (!device) { std::lock_guard lock(mutex); state.starting=false; state.error="선택한 카메라를 찾을 수 없어요. 장치 목록을 새로고침해 주세요."; return; }
            NSError* error=nil; AVCaptureDeviceInput* input=[AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
            if (!input) { std::lock_guard lock(mutex); state.starting=false; state.error=error.localizedDescription.UTF8String ?: "카메라를 열 수 없어요."; return; }
            session=[[AVCaptureSession alloc] init]; [session beginConfiguration];
            if ([session canSetSessionPreset:AVCaptureSessionPreset640x480]) session.sessionPreset=AVCaptureSessionPreset640x480;
            AVCaptureVideoDataOutput* output=[[AVCaptureVideoDataOutput alloc] init]; output.alwaysDiscardsLateVideoFrames=YES;
            output.videoSettings=@{(NSString*)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA)};
            if (![session canAddInput:input]||![session canAddOutput:output]) { [session commitConfiguration]; session=nil; std::lock_guard lock(mutex); state.starting=false; state.error="이 카메라의 영상 형식을 사용할 수 없어요."; return; }
            [session addInput:input]; [session addOutput:output];
            sink=[[StudioFaceSink alloc] init]; auto weak=std::weak_ptr<Impl>(shared_from_this()); sink->consume=[weak](CMSampleBufferRef buffer){if (auto self=weak.lock()) self->capture(buffer);};
            [output setSampleBufferDelegate:sink queue:queue]; [session commitConfiguration];
            if (!requested) { session=nil; sink=nil; return; }
            [session startRunning];
            std::lock_guard lock(mutex); state.active=session.isRunning; state.starting=false; if (!state.active) state.error="카메라를 시작할 수 없어요.";
        }
    }
};
FaceCamera::FaceCamera() : impl_(std::make_shared<Impl>()) {}
FaceCamera::~FaceCamera() { stop(); }
std::vector<CameraDevice> FaceCamera::devices() const {
    std::vector<CameraDevice> result;
    @autoreleasepool { for (AVCaptureDevice* device in videoDevices()) { bool iphone=false; if (@available(macOS 14.0,*)) iphone=[device.deviceType isEqualToString:AVCaptureDeviceTypeContinuityCamera]; result.push_back({device.uniqueID.UTF8String,device.localizedName.UTF8String,iphone}); } }
    return result;
}
void FaceCamera::start(const std::string& id) {
    stop(); auto self=impl_; self->requested=true;
    { std::lock_guard lock(self->mutex); self->state={}; self->state.starting=true; }
    const auto permission=[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    const auto generation=self->generation.load();
    auto open=[self,id,generation]{dispatch_async(self->queue,^{if (self->generation==generation && self->requested) self->begin(id);});};
    if (permission==AVAuthorizationStatusAuthorized) open();
    else if (permission==AVAuthorizationStatusNotDetermined) [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted){if (!self->requested || self->generation!=generation) return; if (granted) open(); else { std::lock_guard lock(self->mutex); self->state.starting=false; self->state.error="시스템 설정에서 카메라 접근을 허용해 주세요."; }}];
    else { std::lock_guard lock(self->mutex); self->state.starting=false; self->state.error="시스템 설정에서 카메라 접근을 허용해 주세요."; }
}
void FaceCamera::stop() {
    auto self=impl_; self->requested=false; ++self->generation;
    dispatch_sync(self->queue,^{ @autoreleasepool { if (self->session) [self->session stopRunning]; self->session=nil; self->sink=nil; std::lock_guard lock(self->mutex); self->state={}; } });
}
FaceCameraState FaceCamera::snapshot(bool withPreview) const {
    std::lock_guard lock(impl_->mutex); FaceCameraState result;
    result.active=impl_->state.active; result.starting=impl_->state.starting; result.error=impl_->state.error; result.frame=impl_->state.frame; result.previewSerial=impl_->state.previewSerial;
    result.preview.width=impl_->state.preview.width;result.preview.height=impl_->state.preview.height;
    if (withPreview) result.preview=impl_->state.preview;
    return result;
}
FaceFrame FaceCamera::analyzeImage(const std::string& path) {
    @autoreleasepool {
        VNDetectFaceLandmarksRequest* request=[[VNDetectFaceLandmarksRequest alloc] init]; request.revision=VNDetectFaceLandmarksRequestRevision3;
        VNImageRequestHandler* handler=[[VNImageRequestHandler alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]] options:@{}];
        NSError* error=nil; if (![handler performRequests:@[request] error:&error]) throw std::runtime_error(error.localizedDescription.UTF8String ?: "얼굴 분석에 실패했어요.");
        return observation(largest(request.results));
    }
}
}
