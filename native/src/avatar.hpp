#pragma once
#include <GL/glew.h>
#include <CubismFramework.hpp>
#include <Model/CubismUserModel.hpp>
#include <CubismModelSettingJson.hpp>
#include "state.hpp"
#include "tracking.hpp"
#include <memory>

namespace studio {
class Avatar : public Csm::CubismUserModel {
public:
    ~Avatar() override;
    bool load(const ModelInfo& model, int maxTexture);
    void update(double dt, float mouth, float lookX, float lookY, const FaceFrame* face = nullptr, bool expressionLocked = false);
    void signal(const Signal& signal);
    void chooseExpression(const std::string& name) { expression(name); manualExpressionUntil_ = name == "neutral" ? 0 : now() + 8; }
    void playMotion(const std::string& name) { motion(name); }
    Json diagnostic();
    void draw(int width, int height, const AvatarSettings& settings);
    bool ready() const { return _model != nullptr; }
    std::string failure;
    struct Target {
        GLuint texture = 0, framebuffer = 0; int width = 0, height = 0;
        ~Target(); void resize(int w, int h);
    };
    void render(Target& target, int width, int height, const AvatarSettings& settings);
private:
    void idle();
    void motion(const std::string& group);
    void expression(const std::string& name);
    std::unique_ptr<Csm::CubismModelSettingJson> setting_;
    fs::path directory_;
    std::vector<GLuint> textures_;
    std::map<std::string,int> parameters_;
    float left_ = -1, right_ = 1, bottom_ = -1, top_ = 1;
    float lookX_ = 0, lookY_ = 0, mouth_ = 0;
    double manualExpressionUntil_ = 0;
};
void initializeCubism(const std::string& resources);
void shutdownCubism();
void drawTexture(GLuint texture, int width, int height);
}
