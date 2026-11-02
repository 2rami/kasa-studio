#pragma once
#include "avatar.hpp"
#include "services.hpp"
#include "imgui.h"
#include <functional>

namespace studio {
class Ui {
public:
    ~Ui();
    enum class Page { Studio, Models, Tracking, Voice, Agent, Connections };
    Page page = Page::Studio;
    bool guide = false, guideDismissed = false, viewObs = false;
    bool petOpen = false, passthrough = false, reload = false;
    bool headless = false;
    int guideStep = 0;
    ImVec2 previewSize {800, 500};
    float lookX = 0, lookY = 0;
    ImFont* font = nullptr; ImFont* displayFont = nullptr;
    std::function<void(bool)> showPet;
    std::function<void(bool)> setPassthrough;
    Json audit=Json::array();
    FaceCameraState cameraFixture;
    int landmarkCount=0;
    void draw(Config& config, Services& services, Avatar* avatar, Avatar::Target& target, platform::Microphone& mic, FaceTracker& tracker);
private:
    bool themed_ = false, lastNight_ = false;
    std::string obsPassword_, chatText_;
    double noticeUntil_ = 0; std::string lastNotice_;
    GLuint obsTexture_ = 0; int obsSerial_ = -1;
    GLuint cameraTexture_ = 0, logoTexture_ = 0; uint64_t cameraSerial_ = 0;
    GLuint qrTexture_ = 0; std::string qrValue_;
    ImVec2 cameraSize_{640,480}; bool devicesLoaded_=false;
    bool cameraFeed_=false;
    ImVec2 obsSize_ {960,540};
    void theme(bool night);
    bool action(const char* id,const char* label,ImVec2 size,bool enabled=true,const char* reason="",bool accent=false);
    void modelPanel(Config&, Services&, Avatar*);
    void trackingPanel(Config&, Services&, FaceTracker&, platform::Microphone&);
    void voicePanel(Config&, Services&);
    void agentPanel(Config&, Services&);
    void connections(Config&, Services&);
    void chatPanel(Config&, Services&);
    void onboarding(Config&, Services&);
};
}
