#include "avatar.hpp"
#include "platform.hpp"
#include <ICubismAllocator.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Id/CubismIdManager.hpp>
#include <Math/CubismTargetPoint.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>

namespace studio {
namespace {
class Allocator : public Csm::ICubismAllocator {
    void* Allocate(Csm::csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* pointer) override { std::free(pointer); }
    void* AllocateAligned(Csm::csmSizeType size, Csm::csmUint32 alignment) override { void* pointer = nullptr; return posix_memalign(&pointer, std::max<size_t>(alignment, sizeof(void*)), size) == 0 ? pointer : nullptr; }
    void DeallocateAligned(void* pointer) override { std::free(pointer); }
};
std::string resourceRoot;
Csm::csmByte* loadResource(const std::string path, Csm::csmSizeInt* size) {
    auto file = fs::path(path).is_absolute() ? fs::path(path) : fs::path(resourceRoot) / path;
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) { *size = 0; return nullptr; }
    auto length = input.tellg();
    if (length <= 0 || length > 256 * 1024 * 1024) { *size = 0; return nullptr; }
    auto* buffer = new Csm::csmByte[static_cast<size_t>(length)]; input.seekg(0); input.read(reinterpret_cast<char*>(buffer), length); *size = static_cast<int>(length); return buffer;
}
std::vector<Csm::csmByte> bytes(const fs::path& file) { std::ifstream input(file, std::ios::binary); return {(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()}; }
}
void initializeCubism(const std::string& resources) {
    resourceRoot = resources; static Allocator allocator; static Csm::CubismFramework::Option options;
    options.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Error;
    options.LoadFileFunction = loadResource; options.ReleaseBytesFunction = [](Csm::csmByte* data) { delete[] data; };
    Csm::CubismFramework::StartUp(&allocator, &options); Csm::CubismFramework::Initialize();
}
void shutdownCubism() { Csm::CubismFramework::Dispose(); Csm::CubismFramework::CleanUp(); }
Avatar::~Avatar() { if (!textures_.empty()) glDeleteTextures(static_cast<int>(textures_.size()), textures_.data()); }
bool Avatar::load(const ModelInfo& info, int maxTexture) {
    try {
        inspectModel(info.path);
        directory_ = fs::path(info.path).parent_path(); auto data = bytes(info.path);
        setting_ = std::make_unique<Csm::CubismModelSettingJson>(data.data(), static_cast<int>(data.size()));
        auto moc = bytes(directory_ / setting_->GetModelFileName());
        LoadModel(moc.data(), static_cast<int>(moc.size()), true);
        if (!_model) { failure = "공식 Core에서 모델을 읽지 못했어요."; return false; }
        const auto physics = std::string(setting_->GetPhysicsFileName());
        if (!physics.empty()) { auto data = bytes(directory_ / physics); LoadPhysics(data.data(), static_cast<int>(data.size())); }
        const auto pose = std::string(setting_->GetPoseFileName());
        if (!pose.empty()) { auto data = bytes(directory_ / pose); LoadPose(data.data(), static_cast<int>(data.size())); }
        _eyeBlink = Csm::CubismEyeBlink::Create(setting_.get());
        _breath = Csm::CubismBreath::Create();
        Csm::csmVector<Csm::CubismBreath::BreathParameterData> breathing;
        auto id = [](const char* name) { return Csm::CubismFramework::GetIdManager()->GetId(name); };
        breathing.PushBack(Csm::CubismBreath::BreathParameterData(id("ParamBreath"), .5f, .5f, 3.2345f, .5f));
        breathing.PushBack(Csm::CubismBreath::BreathParameterData(id("ParamAngleX"), 0, 3, 6.5345f, .5f));
        breathing.PushBack(Csm::CubismBreath::BreathParameterData(id("ParamAngleY"), 0, 2, 3.5345f, .5f));
        _breath->SetParameters(breathing);
        CreateRenderer(1, 1);
        auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
        for (int i = 0; i < setting_->GetTextureCount(); ++i) {
            auto image = platform::loadImage((directory_ / setting_->GetTextureFileName(i)).string(), maxTexture);
            if (image.rgba.empty()) throw std::runtime_error("텍스처를 읽지 못했어요.");
            GLuint texture; glGenTextures(1, &texture); textures_.push_back(texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
            // 공식 셰이더가 mipmap 필터를 선택하므로 전체 레벨을 준비해야 한다.
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            renderer->BindTexture(i, texture);
        }
        glBindTexture(GL_TEXTURE_2D, 0); renderer->IsPremultipliedAlpha(true);
        renderer->SetDrawableClippingMaskBufferSize(maxTexture>=2048?1024:512, maxTexture>=2048?1024:512);
        for (int i=0;i<_model->GetParameterCount();++i) parameters_[_model->GetParameterId(i)->GetString().GetRawString()]=i;
        _model->Update();
        left_ = bottom_ = std::numeric_limits<float>::infinity(); right_ = top_ = -left_;
        for (int i = 0; i < _model->GetDrawableCount(); ++i) {
            if (_model->GetDrawableOpacity(i) <= .01f) continue;
            auto* points = _model->GetDrawableVertexPositions(i);
            for (int j = 0; j < _model->GetDrawableVertexCount(i); ++j) { left_ = std::min(left_, points[j].X); right_ = std::max(right_, points[j].X); bottom_ = std::min(bottom_, points[j].Y); top_ = std::max(top_, points[j].Y); }
        }
        if (!std::isfinite(left_) || right_ <= left_ || top_ <= bottom_) throw std::runtime_error("모델의 그리기 범위를 확인할 수 없어요.");
        _model->SaveParameters(); idle(); return true;
    } catch (const std::exception& error) { failure = error.what(); return false; }
}
void Avatar::motion(const std::string& group) {
    if (!ready() || group.empty() || setting_->GetMotionCount(group.c_str()) == 0) return;
    auto data = bytes(directory_ / setting_->GetMotionFileName(group.c_str(), 0));
    if (data.empty()) return;
    auto* motion = LoadMotion(data.data(), static_cast<int>(data.size()), nullptr, nullptr, nullptr, setting_.get(), group.c_str(), 0);
    if (motion) _motionManager->StartMotionPriority(motion, true, group == "Idle" ? 1 : 3);
}
void Avatar::idle() { motion("Idle"); }
void Avatar::expression(const std::string& name) {
    if (!ready()) return;
    _expressionManager->StopAllMotions();
    if (name == "neutral") return;
    for (int i = 0; i < setting_->GetExpressionCount(); ++i) if (name == setting_->GetExpressionName(i)) {
        auto data = bytes(directory_ / setting_->GetExpressionFileName(i));
        auto* expression = LoadExpression(data.data(), static_cast<int>(data.size()), name.c_str());
        if (expression) _expressionManager->StartMotion(expression, true);
        return;
    }
}
void Avatar::signal(const Signal& value) { manualExpressionUntil_=0; expression(value.emotion); if (!value.motion.empty()) motion(value.motion); }
Json Avatar::diagnostic() {
    if (!ready()) return {{"ready",false}};
    const auto multiply=_model->GetDrawableMultiplyColor(0), screen=_model->GetDrawableScreenColor(0);
    auto color=GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()->GetModelColor();
    Json result={{"drawables",_model->GetDrawableCount()},{"offscreenCount",_model->GetOffscreenCount()},{"multiply",{multiply.X,multiply.Y,multiply.Z,multiply.W}},{"screen",{screen.X,screen.Y,screen.Z,screen.W}},{"modelColor",{color.R,color.G,color.B,color.A}},{"opacity",_model->GetModelOpacity()}};
    result["parameters"]=Json::object();
    for (const auto& [name,index]:parameters_) result["parameters"][name]=_model->GetParameterValue(index);
    if (!textures_.empty()) {
        GLint old,w,h; glGetIntegerv(GL_TEXTURE_BINDING_2D,&old); glBindTexture(GL_TEXTURE_2D,textures_[0]); glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w); glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        std::vector<unsigned char> data(static_cast<size_t>(w)*h*4); glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,data.data());
        int maxRgb=0,colored=0;
        for (size_t i=0;i+3<data.size();i+=4) if (data[i+3]>0) { maxRgb=std::max(maxRgb,static_cast<int>(std::max({data[i],data[i+1],data[i+2]}))); if (data[i]+data[i+1]+data[i+2]>30) ++colored; }
        result["texture"]={{"width",w},{"height",h},{"maxRgb",maxRgb},{"coloredPixels",colored}}; glBindTexture(GL_TEXTURE_2D,old);
    }
    return result;
}
void Avatar::update(double dt, float mouth, float x, float y, const FaceFrame* face, bool expressionLocked) {
    if (!ready()) return;
    float delta = static_cast<float>(std::min(dt, .1));
    _model->LoadParameters();
    if (_motionManager->IsFinished()) idle();
    bool moved = _motionManager->UpdateMotion(_model, delta);
    _model->SaveParameters();
    _expressionManager->UpdateMotion(_model, delta);
    const bool tracked=face&&face->valid;
    if (!tracked && !moved && _eyeBlink) _eyeBlink->UpdateParameters(_model, delta);
    const float follow = 1 - std::exp(-delta * 12);
    lookX_ += (x - lookX_) * follow; lookY_ += (y - lookY_) * follow; mouth_ += (mouth - mouth_) * (1 - std::exp(-delta * 25));
    auto id = [](const char* name) { return Csm::CubismFramework::GetIdManager()->GetId(name); };
    _model->AddParameterValue(id("ParamAngleX"), lookX_ * 16); _model->AddParameterValue(id("ParamAngleY"), lookY_ * 12);
    _model->AddParameterValue(id("ParamEyeBallX"), lookX_ * .7f); _model->AddParameterValue(id("ParamEyeBallY"), lookY_ * .7f);
    _breath->UpdateParameters(_model, delta);
    auto absolute=[&](const char* name,float value) { const auto found=parameters_.find(name); if (found==parameters_.end()) return; const int i=found->second; _model->SetParameterValue(i,std::clamp(value,_model->GetParameterMinimumValue(i),_model->GetParameterMaximumValue(i))); };
    auto normalized=[&](const char* name,float value) { const auto found=parameters_.find(name); if (found==parameters_.end()) return; const int i=found->second; const float def=_model->GetParameterDefaultValue(i); absolute(name,def+value*(value>=0?_model->GetParameterMaximumValue(i)-def:def-_model->GetParameterMinimumValue(i))); };
    if (tracked) {
        absolute("ParamAngleX",face->yaw); absolute("ParamAngleY",face->pitch); absolute("ParamAngleZ",face->roll);
        normalized("ParamEyeBallX",face->gazeX); normalized("ParamEyeBallY",face->gazeY);
        if (!expressionLocked && now()>=manualExpressionUntil_) {
            auto eye=[&](const char* name,float openness) { const auto found=parameters_.find(name); if (found==parameters_.end()) return; const int i=found->second; const float min=_model->GetParameterMinimumValue(i),def=_model->GetParameterDefaultValue(i); absolute(name,min+openness*(def-min)); };
            eye("ParamEyeLOpen",face->eyeLeft); eye("ParamEyeROpen",face->eyeRight);
            normalized("ParamMouthForm",face->smile); normalized("ParamBrowLY",face->browLeft); normalized("ParamBrowRY",face->browRight);
        }
    }
    if (_physics) _physics->Evaluate(_model, delta);
    if (_pose) _pose->UpdateParameters(_model, delta);
    if (setting_->GetLipSyncParameterCount()>0) for (int i=0;i<setting_->GetLipSyncParameterCount();++i) _model->SetParameterValue(setting_->GetLipSyncParameterId(i),mouth_);
    else absolute("ParamMouthOpenY", mouth_);
    _model->Update();
}
void Avatar::draw(int width, int height, const AvatarSettings& settings) {
    if (!ready() || width < 1 || height < 1) return;
    const float scale = std::min(width * .88f / (right_ - left_), height * .94f / (top_ - bottom_)) * settings.scale;
    Csm::CubismMatrix44 matrix;
    matrix.Scale(2 * scale / width, 2 * scale / height);
    matrix.Translate(-(left_ + right_) * scale / width + settings.x * 2, -(top_ + bottom_) * scale / height - settings.y * 2);
    auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    renderer->SetRenderTargetSize(width, height); renderer->SetMvpMatrix(&matrix);
    auto* offscreen = Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    offscreen->BeginFrameProcess(); renderer->DrawModel(); offscreen->EndFrameProcess(); offscreen->ReleaseStaleRenderTextures();
}
Avatar::Target::~Target() { if (texture) glDeleteTextures(1, &texture); if (framebuffer) glDeleteFramebuffersEXT(1, &framebuffer); }
void Avatar::Target::resize(int w, int h) {
    if (width == w && height == h) return;
    width = w; height = h;
    if (!texture) glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!framebuffer) glGenFramebuffersEXT(1, &framebuffer);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer); glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) throw std::runtime_error("아바타 렌더 대상을 만들 수 없어요.");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); glBindTexture(GL_TEXTURE_2D, 0);
}
void Avatar::render(Target& target, int width, int height, const AvatarSettings& settings) {
    target.resize(width, height);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, target.framebuffer); glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    draw(width, height, settings); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); glUseProgram(0);
}
void drawTexture(GLuint texture, int width, int height) {
    glViewport(0, 0, width, height); glUseProgram(0); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1); glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1); glEnd();
    glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
}
}
