#include "avatar.hpp"
#include "platform.hpp"
#include "services.hpp"
#include "ui.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
struct Options {
    bool headless = false; int frames = 120;
    std::string snapshot, petShot, report, page, uiScript, trackingFixture, cameraFixture;
};
Options options(int argc, char** argv) {
    Options result;
    for (int i=1;i<argc;++i) {
        std::string argument = argv[i];
        auto next = [&]() { if (i+1>=argc) throw std::runtime_error("옵션 값이 필요해요: "+argument); return std::string(argv[++i]); };
        if (argument=="--headless") result.headless=true;
        else if (argument=="--frames") result.frames=std::clamp(std::stoi(next()),10,1800);
        else if (argument=="--snapshot") result.snapshot=next();
        else if (argument=="--pet-shot") result.petShot=next();
        else if (argument=="--report") result.report=next();
        else if (argument=="--page") result.page=next();
        else if (argument=="--ui-script") result.uiScript=next();
        else if (argument=="--tracking-fixture") result.trackingFixture=next();
        else if (argument=="--camera-fixture") result.cameraFixture=next();
    } return result;
}
std::vector<unsigned char> readPixels(int width,int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4), top(pixels.size());
    glPixelStorei(GL_PACK_ALIGNMENT,1); glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    for (int y=0;y<height;++y) std::copy_n(pixels.data()+static_cast<size_t>(height-y-1)*width*4,static_cast<size_t>(width)*4,top.data()+static_cast<size_t>(y)*width*4);
    return top;
}
int run(const Options& options) {
    using namespace studio;
    glfwSetErrorCallback([](int code,const char* message) { std::cerr<<"GLFW "<<code<<": "<<message<<'\n'; });
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES,GLFW_FALSE);
    if (options.headless) glfwInitHint(GLFW_COCOA_MENUBAR,GLFW_FALSE);
    if (!glfwInit()) throw std::runtime_error("네이티브 창을 초기화할 수 없어요.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,2); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_FOCUS_ON_SHOW,options.headless?GLFW_FALSE:GLFW_TRUE);
    GLFWwindow* window=glfwCreateWindow(1440,900,"Kasa Studio Native",nullptr,nullptr);
    if (!window) { glfwTerminate(); throw std::runtime_error("OpenGL 창을 만들 수 없어요."); }
    glfwMakeContextCurrent(window); glfwSwapInterval(0); glewInit(); while (glGetError()!=GL_NO_ERROR) {}
    GLFWwindow* pet=nullptr;
    auto ensurePet=[&]() {
        if (pet) return;
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,GLFW_TRUE); glfwWindowHint(GLFW_DECORATED,GLFW_FALSE); glfwWindowHint(GLFW_FLOATING,GLFW_TRUE); glfwWindowHint(GLFW_FOCUS_ON_SHOW,GLFW_FALSE);
        pet=glfwCreateWindow(320,540,"Kasa Studio Pet",nullptr,window);
        if (!pet) throw std::runtime_error("펫 창을 만들 수 없어요.");
        platform::configurePet(pet);
        auto* monitor=glfwGetPrimaryMonitor(); if (monitor) { int x,y,w,h; glfwGetMonitorWorkarea(monitor,&x,&y,&w,&h); glfwSetWindowPos(pet,x+w-350,y+h-560); }
    };
    const bool testPet=options.headless&&!options.petShot.empty();
    if (testPet) ensurePet();
    glfwSetWindowSizeLimits(window,1000,680,GLFW_DONT_CARE,GLFW_DONT_CARE);
    auto resourceDirectory=platform::resources();
    initializeCubism(resourceDirectory);
    {
        Config config; config.load(platform::dataDirectory(),resourceDirectory);
        Services services(config); platform::Microphone microphone; FaceTracker tracker(config.directory);
        if(!options.headless&&config.tracking.source=="iphone"&&config.tracking.phoneAutoConnect)tracker.start(config.tracking);
        FaceFrame fixture;
        if (!options.trackingFixture.empty()) { if (!options.headless) throw std::runtime_error("추적 시험은 숨김 모드에서만 사용할 수 있어요."); auto value=readJson(options.trackingFixture); fixture=decodeFacePacket(value,value.value("code",""),now()); }
        Json uiScript=options.uiScript.empty()?Json::array():readJson(options.uiScript);
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); auto& io=ImGui::GetIO();
        io.IniFilename=nullptr; io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        Ui ui; ui.headless=options.headless;
        if(!options.cameraFixture.empty()){if(!options.headless)throw std::runtime_error("카메라 시험은 숨김 모드에서만 사용할 수 있어요.");ui.cameraFixture.frame=FaceCamera::analyzeImage(options.cameraFixture);ui.cameraFixture.preview=platform::loadImage(options.cameraFixture,640);ui.cameraFixture.previewSerial=1;ui.cameraFixture.active=true;}
        if (config.mode=="rvc") ui.page=Ui::Page::Voice;
        if (config.mode=="agent") ui.page=Ui::Page::Agent;
        const auto fontPath=(fs::path(resourceDirectory)/"fonts/Paperlogy-4Regular.ttf").string();
        ui.font=io.Fonts->AddFontFromFileTTF(fontPath.c_str(),16);
        ui.displayFont=io.Fonts->AddFontFromFileTTF((fs::path(resourceDirectory)/"fonts/Anton-Regular.ttf").c_str(),30);
        ImGui_ImplGlfw_InitForOpenGL(window,true); ImGui_ImplOpenGL2_Init();
        if (options.page=="voice") ui.page=Ui::Page::Voice;
        if (options.page=="agent") ui.page=Ui::Page::Agent;
        if (options.page=="models") ui.page=Ui::Page::Models;
        if (options.page=="tracking") ui.page=Ui::Page::Tracking;
        if (options.page=="connections") ui.page=Ui::Page::Connections;
        ui.showPet=[&](bool visible) { if (visible && !config.selected()) { services.error="아바타 모델을 먼저 가져와 주세요."; ui.petOpen=false; return; } if (visible) ensurePet(); if (pet) { if (visible && !options.headless) glfwShowWindow(pet); else glfwHideWindow(pet); } };
        ui.setPassthrough=[&](bool enabled) { if (pet) platform::clickThrough(pet,enabled); };
        bool quit = false;
        if (!options.headless) platform::installMenu([&](int action) {
            if (action == 0) { glfwShowWindow(window); glfwFocusWindow(window); }
            if (action == 1 && pet) { ui.passthrough = false; platform::clickThrough(pet,false); }
            if (action == 2 && pet) { ui.petOpen = false; glfwHideWindow(pet); }
            if (action == 3) quit = true;
        });
        std::unique_ptr<Avatar> avatar;
        Avatar::Target target,petTarget,uiTarget;
        std::string loadedId; int textureLimit=0,lastSignal=-1;
        double previous=now(),previewAt=0; int frame=0; bool dragging=false;
        double dragX=0,dragY=0; int windowX=0,windowY=0;
        Json diagnostics; Json uiClicks=Json::array();
        if (!options.headless) { glfwShowWindow(window); glfwFocusWindow(window); }
        while (!quit) {
            if (glfwWindowShouldClose(window)) {
                if (ui.petOpen && pet && !glfwWindowShouldClose(pet)) { glfwSetWindowShouldClose(window,GLFW_FALSE); glfwHideWindow(window); }
                else break;
            }
            const double begin=now(),dt=std::clamp(begin-previous,.001,.1); previous=begin;
            glfwPollEvents(); platform::drainEvents(); services.update(); tracker.update(dt,config.tracking);
            const bool drawMain=options.headless || (glfwGetWindowAttrib(window,GLFW_VISIBLE) && !glfwGetWindowAttrib(window,GLFW_ICONIFIED));
            if (!drawMain && !ui.petOpen) { glfwWaitEventsTimeout(.2); continue; }
            const bool drawAvatar=(drawMain && (ui.page==Ui::Page::Studio || ui.page==Ui::Page::Models || (ui.page==Ui::Page::Tracking&&config.tracking.source=="iphone"))) || ui.petOpen || testPet;
            glfwMakeContextCurrent(window);
            if (drawAvatar && (config.modelId!=loadedId || textureLimit!=config.avatar.maxTexture || ui.reload)) {
                avatar.reset(); loadedId=config.modelId; textureLimit=config.avatar.maxTexture; ui.reload=false;
                if (auto* model=config.selected()) { avatar=std::make_unique<Avatar>(); avatar->load(*model,textureLimit); if (!avatar->failure.empty()) services.error=avatar->failure; }
                lastSignal=-1;
            }
            if (services.signal.serial!=lastSignal) { lastSignal=services.signal.serial; if (avatar) avatar->signal(services.signal); if (config.speech && !services.signal.text.empty() && !options.headless) platform::speak(services.signal.text); }
            if (ui.viewObs && services.obs.connected && begin-previewAt>2) { previewAt=begin; services.fetchObsPreview(); }
            if (drawAvatar && avatar && avatar->ready()) {
                const FaceFrame* face=fixture.valid?&fixture:tracker.hasFace()?&tracker.frame():nullptr;
                float mouth=config.tracking.lipInput=="face"?0:microphone.level();
                if (face && config.tracking.lipInput!="microphone") mouth=std::max(mouth,face->mouth);
                if (platform::speaking()) mouth=static_cast<float>(.35+.25*std::sin(begin*20));
                avatar->update(dt,mouth,face?0:config.avatar.gaze?ui.lookX:0,face?0:config.avatar.gaze?ui.lookY:0,face,services.signal.emotion!="neutral" && services.signal.until>now());
                auto preview=ui.previewSize;
                int fw,fh,ww,wh;glfwGetFramebufferSize(window,&fw,&fh);glfwGetWindowSize(window,&ww,&wh);
                const float density=config.avatar.maxTexture==1024?1.f:std::clamp(std::max(fw/static_cast<float>(std::max(1,ww)),fh/static_cast<float>(std::max(1,wh))),1.f,3.f);
                if (drawMain) avatar->render(target,std::clamp(static_cast<int>(std::round(preview.x*density)),64,4096),std::clamp(static_cast<int>(std::round(preview.y*density)),64,4096),config.avatar);
                if (ui.petOpen || testPet) avatar->render(petTarget,config.avatar.maxTexture==1024?320:640,config.avatar.maxTexture==1024?540:1080,config.avatar);
            }
            if (options.headless && frame==5) diagnostics["avatarGlError"]=glGetError();
            int width,height; glfwGetFramebufferSize(window,&width,&height);
            if (drawMain) {
            if (options.headless) uiTarget.resize(width,height);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,options.headless?uiTarget.framebuffer:0); glViewport(0,0,width,height); glClearColor(.98f,.98f,.96f,1); glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(0); glBindBuffer(GL_ARRAY_BUFFER,0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
            ImGui_ImplOpenGL2_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
            for (const auto& event:uiScript) {
                const int at=event.value("frame",-1); if (frame!=at && frame!=at+1) continue;
                for (const auto& button:ui.audit) if (button.value("id","")==event.value("id","")) { auto rect=button["rect"];io.AddMousePosEvent((rect[0].get<float>()+rect[2].get<float>())*.5f,(rect[1].get<float>()+rect[3].get<float>())*.5f);io.AddMouseButtonEvent(0,frame==at);break; }
            }
            ui.draw(config,services,avatar.get(),target,microphone,tracker);
            if (options.headless) for (const auto& item:ui.audit) if (item.value("clicked",false)) uiClicks.push_back({{"frame",frame},{"id",item.at("id")}});
            ImGui::Render(); ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
            }
            if (options.headless && frame==5) diagnostics["uiGlError"]=glGetError();
            ++frame;
            const bool finish=options.headless && frame>=options.frames;
            if (finish && !options.snapshot.empty()) { auto pixels=readPixels(width,height); fs::create_directories(fs::path(options.snapshot).parent_path()); if (!platform::writePng(options.snapshot,width,height,pixels.data())) throw std::runtime_error("화면 저장에 실패했어요."); }
            if (drawMain && !options.headless) glfwSwapBuffers(window);
            if (pet && (ui.petOpen || (finish&&testPet))) {
                if (!options.headless) platform::petCaption(pet,services.signal.text);
                glfwMakeContextCurrent(pet); int pw,ph; glfwGetFramebufferSize(pet,&pw,&ph);
                glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0); glViewport(0,0,pw,ph); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
                if (petTarget.texture) drawTexture(petTarget.texture,pw,ph);
                if (finish) {
                    auto pixels=readPixels(pw,ph); int transparent=0,visible=0,colored=0;
                    for (size_t i=3;i<pixels.size();i+=4) { if (pixels[i]==0) ++transparent; else { ++visible; if (pixels[i-3]+pixels[i-2]+pixels[i-1]>30) ++colored; } }
                    diagnostics["petAlpha"]={{"transparentPixels",transparent},{"visiblePixels",visible},{"coloredPixels",colored}};
                    if (!options.petShot.empty()) { fs::create_directories(fs::path(options.petShot).parent_path()); platform::writePng(options.petShot,pw,ph,pixels.data()); }
                }
                if (!options.headless) glfwSwapBuffers(pet);
                if (!options.headless && !ui.passthrough) {
                    double x,y; platform::globalMouse(x,y);
                    if (glfwGetMouseButton(pet,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS) {
                        if (!dragging) { dragging=true; dragX=x; dragY=y; glfwGetWindowPos(pet,&windowX,&windowY); }
                        glfwSetWindowPos(pet,windowX+static_cast<int>(x-dragX),windowY+static_cast<int>(y-dragY));
                    } else dragging=false;
                }
                if (glfwWindowShouldClose(pet)) { glfwSetWindowShouldClose(pet,GLFW_FALSE); glfwHideWindow(pet); ui.petOpen=false; }
            }
            glfwMakeContextCurrent(window);
            if (finish) {
                diagnostics["application"]="Kasa Studio Native"; diagnostics["runtime"]="C++20"; diagnostics["renderer"]="Official Cubism Native SDK / OpenGL";
                diagnostics["modelReady"]=avatar&&avatar->ready(); diagnostics["rssMB"]=platform::residentMegabytes(); diagnostics["footprintMB"]=platform::footprintMegabytes(); diagnostics["frames"]=frame;
                diagnostics["mainVisible"]=glfwGetWindowAttrib(window,GLFW_VISIBLE)!=0; diagnostics["petCreated"]=pet!=nullptr; diagnostics["petVisible"]=pet&&glfwGetWindowAttrib(pet,GLFW_VISIBLE)!=0; diagnostics["petFloating"]=pet&&glfwGetWindowAttrib(pet,GLFW_FLOATING)!=0; diagnostics["petTransparent"]=pet&&glfwGetWindowAttrib(pet,GLFW_TRANSPARENT_FRAMEBUFFER)!=0;
                if (pet) { platform::clickThrough(pet,true); platform::clickThrough(pet,false); diagnostics["clickThroughChecked"]=true; }
                diagnostics["gpu"]=reinterpret_cast<const char*>(glGetString(GL_RENDERER)); diagnostics["opengl"]=reinterpret_cast<const char*>(glGetString(GL_VERSION));
                diagnostics["services"]=services.diagnostic(); diagnostics["glError"]=glGetError();
                diagnostics["tracking"]=tracker.diagnostic(); diagnostics["trackingFixture"]=fixture.valid; diagnostics["ui"]=ui.audit;
                diagnostics["uiClicks"]=uiClicks; diagnostics["page"]=static_cast<int>(ui.page);
                diagnostics["trackingLandmarks"]=ui.landmarkCount;
                diagnostics["renderTarget"]={{"width",target.width},{"height",target.height},{"logicalWidth",ui.previewSize.x},{"logicalHeight",ui.previewSize.y},{"textureLimit",config.avatar.maxTexture}};
                if (avatar) diagnostics["avatar"]=avatar->diagnostic();
                if (!options.report.empty()) { fs::create_directories(fs::path(options.report).parent_path()); std::ofstream file(options.report); file<<diagnostics.dump(2); }
                std::cout<<diagnostics.dump(2)<<'\n'; break;
            }
            const double interval=1.0/config.avatar.fps;
            const double wait=interval-(now()-begin); if (wait>0) glfwWaitEventsTimeout(wait);
        }
        tracker.stop(); microphone.stop(); platform::stopSpeech(); if (!options.headless) config.save();
        if (!options.headless) platform::removeMenu();
        glfwMakeContextCurrent(window); avatar.reset();
        ImGui_ImplOpenGL2_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    }
    shutdownCubism(); if (pet) glfwDestroyWindow(pet); glfwDestroyWindow(window); glfwTerminate(); return 0;
}
}
int main(int argc,char** argv) { try { return run(options(argc,argv)); } catch (const std::exception& error) { std::cerr<<"Kasa Studio Native: "<<error.what()<<'\n'; return 1; } }
