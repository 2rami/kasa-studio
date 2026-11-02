#include "ui.hpp"
#include "imgui_stdlib.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace studio {
namespace {
struct Palette { ImVec4 bg, surface, ink, soft, dim, line, accent, lime, stage, stageInk, success, danger; };
Palette colors(bool night) {
    auto c = [](int r, int g, int b) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, 1); };
    if (night) return {c(15,27,38), c(26,42,56), c(244,250,254), c(173,191,203), c(129,151,168), c(43,66,87), c(88,182,248), c(220,242,106), c(26,44,58), c(36,67,83), c(163,198,133), c(246,139,140)};
    return {c(251,250,246), c(244,242,236), c(20,20,20), c(85,87,80), c(124,128,119), c(218,217,208), c(25,183,242), c(220,242,106), c(220,235,240), c(176,212,226), c(79,126,53), c(186,65,66)};
}
ImU32 packed(ImVec4 value) { return ImGui::ColorConvertFloat4ToU32(value); }
void muted(const std::string& text) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", text.c_str()); ImGui::PopTextWrapPos(); }
void gap(float amount = 12) { ImGui::Dummy({0, amount}); }
void section(const char* title) { gap(10); ImGui::Separator(); gap(12); ImGui::TextUnformatted(title); gap(9); }
bool primary(const char* label, ImVec2 size, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x*.9f, color.y*.9f, color.z*.9f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.06f,.12f,.15f,1));
    bool clicked = ImGui::Button(label, size); ImGui::PopStyleColor(3); return clicked;
}
void field(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) { ImGui::TextDisabled("%s", label); ImGui::SetNextItemWidth(-1); ImGui::InputText((std::string("##") + label).c_str(), &value, flags); gap(7); }
bool row(const char* label, bool selected, float height = 39) { return ImGui::Selectable(label, selected, 0, {0, height}); }
void status(const char* text, bool online, const Palette& p) { ImGui::TextColored(online ? p.success : p.dim, "%s  %s", online ? "●" : "○", text); }
bool slider(const char* name, float* value, float min, float max, const char* format = "%.0f") {
    ImGui::TextDisabled("%s", name); ImGui::SetNextItemWidth(-1); ImGui::SliderFloat((std::string("##") + name).c_str(), value, min, max, format); bool commit = ImGui::IsItemDeactivatedAfterEdit(); gap(8); return commit;
}
bool panel(const char* name, ImVec2 position, ImVec2 size, bool scroll = true) {
    ImGui::SetNextWindowPos(position); ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (!scroll) flags |= ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    return ImGui::Begin(name, nullptr, flags);
}
void border(ImVec2 from, ImVec2 to, const Palette& p) { ImGui::GetForegroundDrawList()->AddLine(from, to, packed(p.line)); }
}
Ui::~Ui() { for (auto texture:{obsTexture_,cameraTexture_,logoTexture_,qrTexture_}) if (texture) glDeleteTextures(1,&texture); }
bool Ui::action(const char* id,const char* label,ImVec2 size,bool enabled,const char* reason,bool accent) {
    ImGui::PushID(id); ImGui::BeginDisabled(!enabled);
    bool clicked=accent&&enabled?primary(label,size,colors(lastNight_).accent):ImGui::Button(label,size);
    const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
    ImGui::EndDisabled();
    if (!enabled && reason[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(260); ImGui::TextUnformatted(reason); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    if (headless) audit.push_back({{"id",id},{"label",label},{"enabled",enabled},{"clicked",enabled&&clicked},{"reason",enabled?"":reason},{"rect",{min.x,min.y,max.x,max.y}}});
    ImGui::PopID(); return enabled&&clicked;
}
void Ui::theme(bool night) {
    if (themed_ && lastNight_ == night) return;
    themed_ = true; lastNight_ = night; const auto p = colors(night);
    ImGui::StyleColorsLight(); auto& style = ImGui::GetStyle();
    style.WindowRounding = 0; style.WindowBorderSize = 0; style.FrameRounding = 5; style.FrameBorderSize = 1; style.PopupRounding = 10; style.ChildBorderSize = 0;
    style.WindowPadding = {22,18}; style.FramePadding = {10,8}; style.ItemSpacing = {8,8}; style.ItemInnerSpacing = {7,6}; style.ScrollbarSize = 6; style.GrabRounding = 6; style.GrabMinSize = 10;
    style.Colors[ImGuiCol_Text] = p.ink; style.Colors[ImGuiCol_TextDisabled] = p.dim; style.Colors[ImGuiCol_WindowBg] = p.bg; style.Colors[ImGuiCol_ChildBg] = p.bg; style.Colors[ImGuiCol_PopupBg] = p.bg;
    style.Colors[ImGuiCol_Border] = p.line; style.Colors[ImGuiCol_FrameBg] = p.bg; style.Colors[ImGuiCol_FrameBgHovered] = p.surface; style.Colors[ImGuiCol_FrameBgActive] = p.surface;
    style.Colors[ImGuiCol_Button] = p.surface; style.Colors[ImGuiCol_ButtonHovered] = p.line; style.Colors[ImGuiCol_ButtonActive] = p.line;
    style.Colors[ImGuiCol_Header] = ImVec4(p.accent.x,p.accent.y,p.accent.z,.18f); style.Colors[ImGuiCol_HeaderHovered] = p.surface; style.Colors[ImGuiCol_HeaderActive] = p.line;
    style.Colors[ImGuiCol_Separator] = p.line; style.Colors[ImGuiCol_CheckMark] = p.ink; style.Colors[ImGuiCol_SliderGrab] = p.ink; style.Colors[ImGuiCol_SliderGrabActive] = p.accent;
    style.Colors[ImGuiCol_TitleBg] = p.surface; style.Colors[ImGuiCol_TitleBgActive] = p.surface; style.Colors[ImGuiCol_MenuBarBg] = p.bg; style.Colors[ImGuiCol_ScrollbarBg] = p.bg; style.Colors[ImGuiCol_ScrollbarGrab] = p.line;
}
void Ui::modelPanel(Config& c,Services& s,Avatar* avatar) {
    ImGui::TextUnformatted("화면 배치"); gap(10);
    float scale=c.avatar.scale*100;
    bool changed=slider("크기",&scale,30,200,"%.0f%%"); c.avatar.scale=scale/100; if (changed) s.save();
    if (slider("가로 위치",&c.avatar.x,-.7f,.7f,"%.2f")) s.save();
    if (slider("세로 위치",&c.avatar.y,-.5f,.5f,"%.2f")) s.save();
    if (action("layout.reset","배치 초기화",{-1,32},avatar&&avatar->ready(),"모델을 불러온 뒤 사용할 수 있어요.")) { c.avatar.scale=1; c.avatar.x=c.avatar.y=0; s.save(); }
    section("화질");
    ImGui::SetNextItemWidth(-1);
    const char* quality=c.avatar.maxTexture==1024?"절전 · 1024px / 1배":c.avatar.maxTexture==4096?"고화질 · 4096px / Retina":"화면 맞춤 · 2048px / Retina";
    if (ImGui::BeginCombo("##quality",quality)) {
        for (auto [limit,name]:std::array<std::pair<int,const char*>,3>{{{1024,"절전 · 1024px / 1배"},{2048,"화면 맞춤 · 2048px / Retina"},{4096,"고화질 · 4096px / Retina"}}}) if (ImGui::Selectable(name,c.avatar.maxTexture==limit)) { c.avatar.maxTexture=limit; reload=true; s.save(); }
        ImGui::EndCombo();
    }
    gap(6); muted("화면 맞춤부터 Retina 해상도로 렌더링해요."); gap(8);
    if (ImGui::RadioButton("30fps",c.avatar.fps==30)) { c.avatar.fps=30; s.save(); } ImGui::SameLine(); if (ImGui::RadioButton("60fps",c.avatar.fps==60)) { c.avatar.fps=60; s.save(); }
    section("배경");
    for (auto [id,name]:std::array<std::pair<const char*,const char*>,3>{{{"studio","작업실"},{"transparent","투명"},{"green","크로마키"}}}) { if (ImGui::RadioButton(name,c.avatar.background==id)) { c.avatar.background=id; s.save(); } }
}
void Ui::trackingPanel(Config& c,Services& s,FaceTracker& tracker,platform::Microphone& mic) {
    if (!devicesLoaded_ && !headless) { tracker.refreshDevices(); devicesLoaded_=true; }
    const auto p=colors(c.night); const bool active=tracker.active(),starting=tracker.starting();
    ImGui::TextUnformatted("얼굴 입력"); gap(8);
    ImGui::BeginDisabled(active||starting);
    if (ImGui::RadioButton("웹캠 / 연속성 카메라",c.tracking.source=="webcam")) { c.tracking.source="webcam"; s.save(); }
    ImGui::SameLine(); if (ImGui::RadioButton("아이폰",c.tracking.source=="iphone")) { c.tracking.source="iphone"; s.save(); tracker.start(c.tracking); }
    ImGui::EndDisabled(); gap(10);
    if (c.tracking.source=="webcam") {
        std::string name="사용할 카메라 선택";
        for (const auto& device:tracker.devices) if (device.id==c.tracking.deviceId || (c.tracking.deviceId.empty()&&name=="사용할 카메라 선택")) name=device.name+(device.iphone?" · iPhone":"");
        ImGui::BeginDisabled(active||starting); ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##face-camera",name.c_str())) { for (const auto& device:tracker.devices) if (ImGui::Selectable(device.name.c_str(),c.tracking.deviceId==device.id)) { c.tracking.deviceId=device.id; s.save(); } ImGui::EndCombo(); }
        ImGui::EndDisabled(); gap(5);
        if (action("tracking.devices","카메라 목록 새로고침",{-1,32},!active&&!starting,"추적을 중지한 뒤 장치를 바꿀 수 있어요.")) { tracker.refreshDevices(); devicesLoaded_=true; }
        muted("아이폰 연속성 카메라도 이 목록에서 선택해요.");
    } else {muted("같은 Wi-Fi의 Kasa Face가 이 PC를 자동으로 찾아요. 외부망은 서울 중계소로 연결해요.");if(ImGui::Checkbox("앱을 열면 아이폰 연결 대기",&c.tracking.phoneAutoConnect))s.save();}
    gap(12);
    const bool available=c.tracking.source=="iphone" || !tracker.devices.empty();
    if (action("tracking.toggle",active||starting?"연결 / 추적 중지":c.tracking.source=="iphone"?"아이폰 자동 연결":"웹캠 추적 시작",{-1,38},active||starting||available,"카메라를 연결하고 장치 목록을 새로고침해 주세요.",true)) { if (active||starting) tracker.stop(); else { s.save(); tracker.start(c.tracking); } }
    gap(8); status(starting?"카메라 준비 중":active?(tracker.hasFace()?"얼굴 추적 중":"얼굴 / 아이폰 연결 대기"):"추적 꺼짐",tracker.hasFace(),p);
    if (!tracker.error.empty()) { gap(5); ImGui::TextWrapped("%s",tracker.error.c_str()); }
    gap(8);
    if (action("tracking.calibrate","정면 기준 맞추기",{-1,34},tracker.hasFace(),"얼굴이 감지된 뒤 사용할 수 있어요.")) tracker.calibrate();
    if (c.tracking.source=="iphone" && tracker.phone.listening()) {
        section("아이폰 연결");auto link=tracker.link.snapshot();
        status(link.advertised?"주변 PC 자동 검색 켜짐":"주변 PC 검색 준비 중",link.advertised,p);status(link.online?"서울 중계소 연결됨":"서울 중계소 연결 중",link.online,p);
        if(!link.discoveryError.empty())muted(link.discoveryError);
        muted("같은 Wi-Fi에서는 아이폰의 PC 이름을 눌러요. 외부망은 처음 한 번 QR로 등록하면 다음부터 바로 연결해요.");
        auto value=tracker.connectionUrl();
        if (qrValue_!=value) { qrValue_=value; auto image=platform::qrImage(value); if (!image.rgba.empty()) { if (!qrTexture_) glGenTextures(1,&qrTexture_); glBindTexture(GL_TEXTURE_2D,qrTexture_); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,image.width,image.height,0,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data()); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST); glBindTexture(GL_TEXTURE_2D,0); } }
        if (qrTexture_) ImGui::Image((ImTextureID)qrTexture_,{190,190});
        if (action("tracking.copy","연결 정보 복사",{-1,32})) platform::copyText(value);
        if(ImGui::TreeNode("직접 입력")){for(const auto& address:FaceTracker::addresses())ImGui::TextUnformatted(address.c_str());ImGui::Text("포트 %d / 코드 %s",tracker.phone.port(),tracker.code.c_str());ImGui::TreePop();}
    }
    section("보정");
    muted(tracker.calibrated?"정면 기준을 적용했어요.":"정면을 보고 눈과 입을 편하게 둔 뒤 보정해 주세요.");
    if (ImGui::Checkbox("거울처럼 좌우 반전",&c.tracking.mirror)) s.save();
    if (ImGui::Checkbox("추적이 꺼지면 마우스 시선 사용",&c.avatar.gaze)) s.save();
    if (slider("회전 감도",&c.tracking.sensitivity,.3f,4.f,"%.1f배")) s.save();
    muted("높일수록 고개를 조금 돌려도 크게 움직여요.");
    if (slider("움직임 부드러움",&c.tracking.smoothing,.015f,.3f,"%.2f s")) s.save();
    section("입 모양");
    ImGui::SetNextItemWidth(-1);
    const char* lip=c.tracking.lipInput=="face"?"얼굴 추적":c.tracking.lipInput=="microphone"?"마이크":"자동 · 얼굴 / 마이크";
    if (ImGui::BeginCombo("##lip-input",lip)) { for (auto [id,label]:std::array<std::pair<const char*,const char*>,3>{{{"auto","자동 · 얼굴 / 마이크"},{"face","얼굴 추적"},{"microphone","마이크"}}}) if (ImGui::Selectable(label,c.tracking.lipInput==id)) { c.tracking.lipInput=id; if (c.tracking.lipInput=="face") mic.stop(); s.save(); } ImGui::EndCombo(); }
    if (action("tracking.microphone",mic.active()?"입 모양 마이크 끄기":"입 모양 마이크 켜기",{-1,32},c.tracking.lipInput!="face","마이크를 사용하려면 입 모양 입력을 바꿔 주세요.")) { if (mic.active()) mic.stop(); else mic.start(); }
    ImGui::ProgressBar(mic.level(),{-1,4},""); if (!mic.error().empty()) muted(mic.error());
    muted("입 모양용 마이크예요. RVC 변환 입력은 목소리 화면에서 선택해요.");
}
void Ui::voicePanel(Config& c, Services& s) {
    const auto p = colors(c.night); auto& info = s.rvc.info;
    bool active = s.rvc.connected && info.value("serverAudioStated", 0) == 1;
    ImGui::TextUnformatted("w-okada Voice Changer"); gap(8);
    ImGui::BeginDisabled(active); field("Voice Changer 서버 주소", c.rvcUrl); ImGui::EndDisabled();
    if (action("rvc.connect",s.rvcBusy?"처리 중":s.rvc.connected?"서버 상태 갱신":"Voice Changer 연결",{-1,36},!s.rvcBusy&&serviceUrlValid(c.rvcUrl),"서버 주소를 확인하거나 이전 음성 요청이 끝날 때까지 기다려 주세요.",true)) s.connectRvc();
    gap(8); status(s.rvc.connected ? (active ? "변환 중" : "연결됨") : "서버 연결 전", s.rvc.connected, p);
    if (!s.rvc.connected) { gap(8); muted("MMVCServerSIO 서버를 먼저 실행해 주세요."); if (action("rvc.download","Voice Changer 받기",{-1,32})) platform::openUrl("https://github.com/w-okada/voice-changer/releases"); }
    section("음성 모델");
    ImGui::BeginDisabled(!s.rvc.connected || active || s.rvcBusy);
    const int slot = info.value("modelSlotIndex", -1); std::string name = "RVC 모델 선택";
    for (const auto& model : info.value("modelSlots", Json::array())) if (model.value("slotIndex", -2) == slot && !model.value("modelFile", "").empty()) name = model.value("name", "음성 모델");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##voice-model", name.c_str())) {
        for (const auto& model : info.value("modelSlots", Json::array())) if (model.value("voiceChangerType", "") == "RVC" && !model.value("modelFile", "").empty()) if (ImGui::Selectable(model.value("name", "RVC").c_str(), model.value("slotIndex", -2) == slot)) s.setRvc("modelSlotIndex", model.at("slotIndex"));
        ImGui::EndCombo();
    }
    if (action("rvc.import","음성 모델 가져오기",{-1,34},s.rvc.connected&&!active&&!s.rvcBusy,"서버를 연결하고 변환을 중지한 뒤 가져올 수 있어요.")) {
        auto model = platform::pickFile("RVC 모델 선택", {"pth", "onnx"});
        if (!model.empty()) { auto index = platform::pickFile("RVC 인덱스 선택 · 없으면 취소", {"index"}); std::vector<std::string> files{model}; if (!index.empty()) files.push_back(index); s.uploadRvc(files); }
    }
    ImGui::EndDisabled();
    section("목소리 조절");
    ImGui::BeginDisabled(!s.rvc.connected || s.rvcBusy);
    float tune = info.value("tran", 0.f), ratio = info.value("indexRatio", 0.f), protect = info.value("protect", .5f);
    bool tuneCommit = slider("음높이", &tune, -24, 24, "%.0f st"); info["tran"] = tune; if (tuneCommit) s.setRvc("tran", static_cast<int>(tune));
    bool ratioCommit = slider("음색 혼합", &ratio, 0, 1, "%.2f"); info["indexRatio"] = ratio; if (ratioCommit) s.setRvc("indexRatio", ratio);
    bool protectCommit = slider("자음 보호", &protect, 0, .5f, "%.2f"); info["protect"] = protect; if (protectCommit) s.setRvc("protect", protect);
    std::string detector = info.value("f0Detector", "rmvpe"); ImGui::TextDisabled("음높이 추출"); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##detector", detector.c_str())) { for (auto value : {"rmvpe", "crepe", "harvest", "dio"}) if (ImGui::Selectable(value, detector == value)) s.setRvc("f0Detector", value); ImGui::EndCombo(); }
    ImGui::EndDisabled();
    section("오디오 경로");
    ImGui::BeginDisabled(!s.rvc.connected || active || s.rvcBusy);
    for (auto [key, title, listKey] : std::array<std::array<const char*,3>,3>{{{{"serverInputDeviceId","입력 마이크","serverAudioInputDevices"}},{{"serverOutputDeviceId","변환된 소리 출력","serverAudioOutputDevices"}},{{"serverMonitorDeviceId","모니터링 출력","serverAudioOutputDevices"}}}}) {
        const int selected = info.value(key, -1); std::string name = selected == -1 ? "장치 선택 / 끄기" : std::to_string(selected);
        auto devices = info.value(listKey, Json::array()); if (!devices.is_array()) devices = Json::array();
        for (const auto& device : devices) if (device.value("index", -2) == selected) name = device.value("name", "장치");
        ImGui::TextDisabled("%s", title); ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(key, name.c_str())) { if (std::string(key) == "serverMonitorDeviceId" && ImGui::Selectable("모니터링 끄기", selected == -1)) s.setRvc(key, -1); for (const auto& device : devices) if (ImGui::Selectable(device.value("name", "장치").c_str(), selected == device.value("index", -2))) s.setRvc(key, device.at("index")); ImGui::EndCombo(); }
        gap(7);
    }
    ImGui::TextDisabled("샘플레이트"); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sample-rate", info.value("serverAudioSampleRate", 48000) == 44100 ? "44,100 Hz" : "48,000 Hz")) { for (int value : {44100,48000}) if (ImGui::Selectable((std::to_string(value) + " Hz").c_str())) s.setRvc("serverAudioSampleRate", value); ImGui::EndCombo(); }
    ImGui::TextDisabled("처리 블록"); ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##chunk-size", std::to_string(info.value("serverReadChunkSize", 256)).c_str())) { for (int value : {128,256,512,1024}) if (ImGui::Selectable(std::to_string(value).c_str())) s.setRvc("serverReadChunkSize", value); ImGui::EndCombo(); }
    ImGui::EndDisabled(); gap(12);
    muted("RVC 출력과 OBS 마이크에 같은 가상 오디오 장치를 선택해 주세요.");
    if (action("rvc.driver","Mac 가상 오디오 · BlackHole",{-1,32})) platform::openUrl("https://existential.audio/blackhole/");
}
void Ui::agentPanel(Config& c, Services& s) {
    auto p = colors(c.night);
    ImGui::TextUnformatted("파트너 설정"); gap(10);
    field("파트너 이름", c.agentName); field("호환 API 주소", c.agentUrl); field("API 키 · 이번 실행만 보관", s.agentKey, ImGuiInputTextFlags_Password);
    field("모델 이름", c.agentModel);
    if (action("agent.probe",s.agentBusy?"모델 조회 중":"서버 모델 조회",{-1,34},!s.agentBusy&&serviceUrlValid(c.agentUrl),"서버 주소를 확인하거나 모델 조회가 끝날 때까지 기다려 주세요.")) s.probeAgent();
    if (!s.agentModels.empty() && ImGui::BeginCombo("##agent-models", "조회한 모델 선택")) { for (const auto& item : s.agentModels) if (ImGui::Selectable(item.c_str(), c.agentModel == item)) { c.agentModel = item; s.save(); } ImGui::EndCombo(); }
    gap(8); ImGui::TextDisabled("파트너 지침"); ImGui::SetNextItemWidth(-1); ImGui::InputTextMultiline("##prompt", &c.agentPrompt, {-1,110});
    ImGui::Checkbox("대답을 기기 목소리로 읽기", &c.speech);
    if (action("agent.save","파트너 설정 저장",{-1,35},serviceUrlValid(c.agentUrl)&&!c.agentName.empty(),"서버 주소와 파트너 이름을 입력해 주세요.",true)) { s.save(); s.log("파트너 설정을 저장했어요."); }
    section("외부 에이전트"); muted("에이전트가 로컬 API로 대사·표정·동작을 보낼 수 있어요."); gap(7);
    if (action("agent.copy","연결 명령 복사",{-1,34})) { platform::copyText(s.agentCommand()); s.log("전용 토큰이 담긴 연결 명령을 복사했어요."); }
    muted("수신 주소: 127.0.0.1:" + std::to_string(s.localPort));
}
void Ui::chatPanel(Config& c, Services& s) {
    ImGui::TextUnformatted(c.agentName.c_str()); ImGui::Separator();
    if (ImGui::BeginChild("chat", {0, std::max(120.f, ImGui::GetContentRegionAvail().y - 65)}, ImGuiChildFlags_None)) {
        if (s.messages.empty()) { gap(20); muted(c.agentModel.empty() ? "에이전트 주소와 모델을 먼저 연결해 주세요." : "파트너에게 말을 걸어 보세요."); }
        for (const auto& message : s.messages) { ImGui::TextDisabled("%s", message.value("role", "") == "user" ? "나" : c.agentName.c_str()); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(message.value("content", "").c_str()); ImGui::PopTextWrapPos(); gap(12); }
        if (s.chatBusy) muted("대답을 기다리고 있어요.");
    } ImGui::EndChild();
    ImGui::BeginDisabled(s.chatBusy || c.agentModel.empty()); ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x - 72));
    bool enter = ImGui::InputText("##chat-input", &chatText_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::EndDisabled();
    ImGui::SameLine(); bool send=action("agent.send","보내기",{64,34},!s.chatBusy&&!c.agentModel.empty()&&!chatText_.empty(),"모델을 연결하고 보낼 메시지를 입력해 주세요.");
    if ((enter || send) && !chatText_.empty()) { s.chat(chatText_); chatText_.clear(); }
}
void Ui::connections(Config& c,Services& s) {
    const auto p=colors(c.night);
    ImGui::TextUnformatted("OBS Studio"); gap(14);
    ImGui::BeginDisabled(s.obs.connecting || s.obs.streaming || s.obs.recording);
    field("WebSocket 주소",c.obsUrl); field("비밀번호 · 연결할 때만 사용",obsPassword_,ImGuiInputTextFlags_Password);
    ImGui::EndDisabled();
    if (action("obs.connect",s.obs.connecting?"연결 중":s.obs.connected?"다시 연결":"서버 연결",{-1,38},!s.obs.connecting&&!s.obs.streaming&&!s.obs.recording&&serviceUrlValid(c.obsUrl,true),"OBS 주소를 확인하고 방송·녹화를 종료한 뒤 연결해 주세요.",true)) { s.connectObs(obsPassword_); obsPassword_.clear(); }
    gap(8); status(s.obs.connected?"연결됨":"연결 전",s.obs.connected,p);
    if (s.obs.connected && action("obs.disconnect","연결 해제",{-1,32},!s.obs.streaming&&!s.obs.recording,"방송과 녹화를 먼저 종료해 주세요.")) s.disconnectObs();
    section("OBS에서 준비할 것"); muted("도구 → WebSocket 서버 설정에서 서버를 켜고 같은 주소와 비밀번호를 입력해 주세요."); gap(8);
    if (action("obs.download","OBS 다운로드",{-1,32})) platform::openUrl("https://obsproject.com/download");
    section("아바타 송출"); muted("작업실에서 펫을 띄우고 OBS에 넣기를 누르세요. 직접 설정할 때는 OBS 창 캡처에서 Kasa Studio Pet을 선택해요.");
}
void Ui::onboarding(Config& c, Services& s) {
    if (!c.onboarded && !guideDismissed && !headless) guide = true;
    if (guide && !ImGui::IsPopupOpen("작업실 설정")) ImGui::OpenPopup("작업실 설정");
    ImGui::SetNextWindowSize({590,490}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {.5f,.5f});
    if (ImGui::BeginPopupModal("작업실 설정", &guide, ImGuiWindowFlags_NoResize)) {
        const auto p = colors(c.night);
        ImGui::TextDisabled("%d / 5", guideStep + 1); gap(12);
        if (guideStep == 0) { ImGui::TextUnformatted("작업 모드"); gap(12); for (auto [id,label] : std::array<std::pair<const char*,const char*>,3>{{{"studio","방송 · OBS와 아바타"},{"agent","에이전트 펫 · 대화와 표정"},{"rvc","RVC · 실시간 목소리 변환"}}}) { if (row(label, c.mode == id, 49)) { c.mode = id; s.save(); } } }
        else if (guideStep == 1) { ImGui::TextUnformatted("아바타 선택"); gap(12); for (auto& model : c.models) if (row(model.name.c_str(), c.modelId == model.id, 43)) { c.modelId = model.id; reload = true; s.save(); } gap(10); if (action("guide.model.import",s.importBusy?"가져오는 중":"Live2D 모델 가져오기",{-1,34},!s.importBusy,"이전 모델을 가져오고 있어요.")) { auto file=platform::pickFile("Live2D 모델 가져오기",{"json","zip"}); if(!file.empty()) s.importAvatar(file); } muted("model3.json이나 모델 전체가 든 ZIP을 가져와요."); }
        else if (guideStep == 2) { ImGui::TextUnformatted("OBS 연결"); gap(12); ImGui::BeginDisabled(s.obs.connecting||s.obs.streaming||s.obs.recording); field("OBS WebSocket 주소",c.obsUrl); field("WebSocket 비밀번호",obsPassword_,ImGuiInputTextFlags_Password); ImGui::EndDisabled(); if (action("guide.obs.connect",s.obs.connecting?"연결 중":"OBS 연결하기",{-1,35},!s.obs.connecting&&!s.obs.streaming&&!s.obs.recording&&serviceUrlValid(c.obsUrl,true),"OBS 주소와 연결 상태를 확인해 주세요.")) { s.connectObs(obsPassword_); obsPassword_.clear(); } gap(8); status(s.obs.connected ? "연결됨" : "나중에 연결해도 괜찮아요.",s.obs.connected,p); muted("OBS → 도구 → WebSocket 서버 설정에서 서버를 켜 주세요."); }
        else if (guideStep == 3) { ImGui::TextUnformatted("얼굴 추적"); gap(18); muted("얼굴 추적 화면에서 웹캠이나 아이폰 연속성 카메라를 선택하고 추적 시작을 눌러요."); gap(15); muted("아이폰을 선택하면 Kasa Face가 PC를 자동으로 찾아요. 셀룰러·다른 Wi-Fi는 처음 한 번 QR을 등록하면 서울 중계소로 연결돼요."); gap(15); muted("연결한 뒤 정면 기준 맞추기를 누르면 현재 자세에 맞춰 보정해요. 카메라는 직접 시작해야 켜져요."); }
        else { ImGui::TextUnformatted("목소리와 파트너"); gap(18); muted("목소리 화면에서 w-okada 서버와 음성 모델, 입력·출력 장치를 연결해요."); gap(15); muted("파트너 화면에서 호환 API를 연결하거나, 외부 에이전트 연결 명령을 복사해 사용할 수 있어요."); gap(15); muted("마이크와 방송 송출은 직접 켜야 시작돼요."); }
        gap(15); ImGui::Separator(); gap(10);
        if (ImGui::Button(guideStep == 0 ? "나중에 설정" : "이전", {120,36})) { if (guideStep == 0) { guide = false; guideDismissed = true; ImGui::CloseCurrentPopup(); } else --guideStep; }
        ImGui::SameLine(ImGui::GetWindowWidth() - 145);
        if (primary(guideStep == 4 ? "작업실 열기" : "다음", {120,36}, p.accent)) { if (guideStep < 4) ++guideStep; else { c.onboarded = true; page = c.mode == "rvc" ? Page::Voice : c.mode == "agent" ? Page::Agent : Page::Studio;  s.save(); guide = false; guideDismissed = true; ImGui::CloseCurrentPopup(); } }
        ImGui::EndPopup();
    }
    if (!guide) guideDismissed = true;
}
void Ui::draw(Config& c,Services& s,Avatar* avatar,Avatar::Target& target,platform::Microphone& mic,FaceTracker& tracker) {
    theme(c.night); audit=Json::array();
    const auto p=colors(c.night); const auto size=ImGui::GetIO().DisplaySize;
    const float side=188,header=72,footer=32;
    const float left=side+24,top=header+14,bottom=size.y-footer-14;
    const float workspace=size.x-side;
    const bool ready=avatar&&avatar->ready(),hasModel=c.selected()!=nullptr;
    const bool streamingPending=s.obsPending("StartStream")||s.obsPending("StopStream");
    const bool recordingPending=s.obsPending("StartRecord")||s.obsPending("StopRecord");
    const char* title=page==Page::Studio?"방송 작업실":page==Page::Models?"모델":page==Page::Tracking?"얼굴 추적":page==Page::Voice?"목소리":page==Page::Agent?"파트너":"OBS 연결";
    auto uploadTexture=[](GLuint& texture,const platform::Image& image) {
        if (image.rgba.empty()) return;
        if (!texture) glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,image.width,image.height,0,GL_RGBA,GL_UNSIGNED_BYTE,image.rgba.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE); glBindTexture(GL_TEXTURE_2D,0);
    };
    if (!logoTexture_) uploadTexture(logoTexture_,platform::loadImage((fs::path(platform::resources())/"StudioMark.png").string(),128));
    auto picture=[&](ImVec2 pos,ImVec2 dimensions,bool pointer) {
        auto* draw=ImGui::GetWindowDrawList(); const ImVec2 end{pos.x+dimensions.x,pos.y+dimensions.y};
        ImU32 bg=c.avatar.background=="green"?IM_COL32(0,177,64,255):packed(p.stage);
        draw->AddRectFilled(pos,end,bg,8);
        if (c.avatar.background=="transparent") for (int y=0;y<static_cast<int>(dimensions.y);y+=16) for (int x=0;x<static_cast<int>(dimensions.x);x+=16) if ((x/16+y/16)%2) draw->AddRectFilled({pos.x+x,pos.y+y},{std::min(end.x,pos.x+x+16),std::min(end.y,pos.y+y+16)},packed(p.line));
        if (page==Page::Studio && c.avatar.background=="studio" && displayFont) {
            draw->AddText(displayFont,s.obs.streaming?142:94,{pos.x+30,pos.y+30},packed(p.stageInk),s.obs.streaming?"ON":"STAND");
            draw->AddText(displayFont,142,{pos.x+62,pos.y+145},packed(p.stageInk),s.obs.streaming?"AIR":"BY");
        }
        draw->AddCallback([](const ImDrawList*,const ImDrawCmd*){glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);},nullptr);
        if (target.texture) draw->AddImage((ImTextureID)target.texture,pos,end,{0,1},{1,0});
        draw->AddCallback([](const ImDrawList*,const ImDrawCmd*){glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);},nullptr);
        ImGui::InvisibleButton("##live-avatar",dimensions);
        if (pointer&&ImGui::IsItemHovered()&&c.avatar.gaze&&!tracker.hasFace()) { auto mouse=ImGui::GetIO().MousePos; lookX=(mouse.x-pos.x)/dimensions.x*2-1; lookY=1-(mouse.y-pos.y)/dimensions.y*2; } else lookX=lookY=0;
        if (!ready) draw->AddText({pos.x+24,pos.y+24},packed(p.dim),hasModel?"모델 준비 중":"모델을 선택해 주세요");
        if (!s.signal.text.empty()) { draw->AddRectFilled({end.x-286,pos.y+18},{end.x-18,pos.y+110},packed(p.bg),12); draw->AddText(font,15,{end.x-272,pos.y+32},packed(p.ink),s.signal.text.c_str(),nullptr,238); }
    };
    panel("sidebar",{0,0},{side,size.y},false);
    if (logoTexture_) { ImGui::Image((ImTextureID)logoTexture_,{36,36}); ImGui::SameLine(); }
    if (displayFont) ImGui::PushFont(displayFont,27); ImGui::TextUnformatted("KASA"); if (displayFont) ImGui::PopFont();
    ImGui::TextDisabled("STUDIO"); gap(28);
    const std::array<std::tuple<Page,const char*,const char*>,6> navigation{{{Page::Studio,"nav.studio","작업실"},{Page::Models,"nav.models","모델"},{Page::Tracking,"nav.tracking","얼굴 추적"},{Page::Voice,"nav.voice","목소리"},{Page::Agent,"nav.agent","파트너"},{Page::Connections,"nav.connections","OBS 연결"}}};
    for (auto [id,key,label]:navigation) {
        const bool selected=page==id; ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0); ImGui::PushStyleColor(ImGuiCol_Button,selected?p.lime:p.bg); ImGui::PushStyleColor(ImGuiCol_Text,selected?ImVec4(.08f,.12f,.06f,1):p.soft);
        if (action(key,label,{-1,39})) { page=id; if (id==Page::Studio) c.mode="studio"; if (id==Page::Voice) c.mode="rvc"; if (id==Page::Agent) c.mode="agent"; s.save(); }
        ImGui::PopStyleColor(2); ImGui::PopStyleVar();
    }
    section("OBS 장면");
    const float sceneHeight=std::max(70.f,size.y-ImGui::GetCursorPosY()-84);
    if (ImGui::BeginChild("scenes",{0,sceneHeight},ImGuiChildFlags_None)) {
        if (s.obs.scenes.empty()) muted("연결한 장면이 여기에 보여요.");
        for (const auto& scene:s.obs.scenes) {
            ImGui::PushID(scene.c_str());
            if (ImGui::Selectable(scene.c_str(),s.obs.scene==scene,0,{0,30})) s.obsCall("SetCurrentProgramScene",{{"sceneName",scene}},[&s,scene](Json){s.obs.scene=scene;s.log("장면 전환: "+scene);});
            ImGui::PopID();
        }
    } ImGui::EndChild();
    if (action("guide.open","처음 설정",{-1,32})) { guide=true; guideStep=0; }
    ImGui::End();
    panel("header",{side,0},{workspace,header},false);
    ImGui::PushFont(font,22); ImGui::TextUnformatted(title); ImGui::PopFont();
    ImGui::SetCursorPos({workspace-455,17});
    if (action("theme.toggle",c.night?"밝은 테마":"남색 테마",{94,34})) { c.night=!c.night; s.save(); }
    ImGui::SameLine();
    if (action("pet.toggle",petOpen?"펫 숨기기":"펫 띄우기",{90,34},hasModel,"모델 화면에서 아바타를 먼저 선택해 주세요.")) {petOpen=!petOpen;showPet(petOpen);}
    ImGui::SameLine();
    if (action("record.toggle",recordingPending?"처리 중":s.obs.recording?"녹화 중지":"녹화",{84,34},s.obs.connected&&!recordingPending&&(s.obs.recording||!s.obs.scene.empty()),"OBS를 연결하고 장면과 이전 요청을 확인해 주세요.")) s.obsCall(s.obs.recording?"StopRecord":"StartRecord",{},[&s](Json){s.refreshObs();});
    ImGui::SameLine();
    if (action("stream.open",streamingPending?"처리 중":s.obs.streaming?"방송 종료":"방송 시작",{125,34},s.obs.connected&&!streamingPending&&(s.obs.streaming||!s.obs.scene.empty()),"OBS를 연결하고 방송할 장면을 선택해 주세요.",true)) ImGui::OpenPopup("방송 확인");
    if (ImGui::BeginPopupModal("방송 확인",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(s.obs.streaming?"방송을 종료할까요?":"설정된 채널로 방송을 시작할까요?"); ImGui::Text("현재 장면: %s",s.obs.scene.c_str()); gap(12);
        if (action("stream.cancel","취소",{110,35})) ImGui::CloseCurrentPopup(); ImGui::SameLine();
        if (action("stream.confirm",s.obs.streaming?"방송 종료":"방송 시작",{130,35},s.obs.connected&&!streamingPending,"OBS 연결을 확인해 주세요.",true)) { s.obsCall(s.obs.streaming?"StopStream":"StartStream",{},[&s](Json){s.refreshObs();}); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    } ImGui::End();
    if (page==Page::Studio) {
        constexpr float inspector=292;
        const float width=workspace-inspector-48;
        panel("studio",{side,header},{workspace-inspector,size.y-header-footer},true);
        if (action("studio.model",hasModel?c.selected()->name.c_str():"모델 선택",{160,30})) page=Page::Models;
        ImGui::SameLine();
        if (action("preview.avatar","아바타",{72,30})) viewObs=false; ImGui::SameLine();
        if (action("preview.obs","OBS 장면",{92,30},s.obs.connected,"OBS에 연결한 뒤 장면을 확인할 수 있어요.")) {viewObs=true;s.fetchObsPreview();}
        gap(8); const auto pos=ImGui::GetCursorScreenPos(); previewSize={width,std::clamp(size.y-366.f,280.f,650.f)};
        if (!viewObs) picture(pos,previewSize,true);
        else {
            auto* draw=ImGui::GetWindowDrawList(); draw->AddRectFilled(pos,{pos.x+previewSize.x,pos.y+previewSize.y},packed(p.stage),8);
            if (obsSerial_!=s.obs.previewSerial&&!s.obs.preview.empty()) { obsSerial_=s.obs.previewSerial; auto image=platform::loadImageBase64(s.obs.preview); if (!image.rgba.empty()) {obsSize_={static_cast<float>(image.width),static_cast<float>(image.height)};uploadTexture(obsTexture_,image);} }
            if (obsTexture_) {const float fit=std::min(previewSize.x/obsSize_.x,previewSize.y/obsSize_.y);ImVec2 dimensions{obsSize_.x*fit,obsSize_.y*fit},start{pos.x+(previewSize.x-dimensions.x)*.5f,pos.y+(previewSize.y-dimensions.y)*.5f};draw->AddImage((ImTextureID)obsTexture_,start,{start.x+dimensions.x,start.y+dimensions.y});}
            ImGui::Dummy(previewSize);
        }
        gap(9);
        if (action("obs.overlay","OBS에 넣기",{110,33},s.obs.connected&&ready,"모델을 준비하고 OBS에 연결해 주세요.")) {if(!petOpen){petOpen=true;showPet(true);}s.addOverlay();}
        if (petOpen) {ImGui::SameLine();if (ImGui::Checkbox("클릭 통과",&passthrough)) setPassthrough(passthrough);}
        gap(5); ImGui::Separator(); gap(6);
        ImGui::TextDisabled("표정");
        if (hasModel) {
            std::vector<std::pair<std::string,std::string>> choices{{"neutral","기본"}};
            for (const auto& name:c.selected()->expressions) if (name!="neutral") choices.push_back({name,name=="smile"?"웃음":name=="surprise"?"놀람":name=="shy"?"부끄러움":name=="sad"?"슬픔":name=="angry"?"화남":name=="wink"?"윙크":name=="sweat"?"식은땀":name=="sparkle"?"반짝임":name=="tear"?"눈물":name=="gloom"?"우울":name=="soul"?"혼 빠짐":name=="flag"?"깃발":name});
            float used=0; const float lineWidth=ImGui::GetContentRegionAvail().x;
            for (const auto& [id,label]:choices) { const float buttonWidth=ImGui::CalcTextSize(label.c_str()).x+20; if (used>0&&used+8+buttonWidth<=lineWidth) ImGui::SameLine(); else used=0; if (action(("expression."+id).c_str(),label.c_str(),{buttonWidth,30},ready,"모델을 불러온 뒤 사용할 수 있어요.")) { if (id=="neutral"||id=="smile"||id=="surprise"||id=="shy"||id=="sad"||id=="angry"||id=="wink") s.setSignal({{"emotion",id},{"duration",8}}); else if (avatar) avatar->chooseExpression(id); } used+=buttonWidth+8; }
            gap(7); ImGui::SetNextItemWidth(190);
            if (ImGui::BeginCombo("##motion","동작 선택")) { for(const auto& motion:c.selected()->motions) if(ImGui::Selectable(motion.c_str())&&avatar) avatar->playMotion(motion); ImGui::EndCombo(); }
        }
        section("오디오 믹서");
        if (s.obs.inputs.empty()) muted("OBS를 연결하면 오디오 입력이 보여요.");
        for (auto& input:s.obs.inputs) { ImGui::PushID(input.name.c_str());ImGui::TextUnformatted(input.name.c_str());ImGui::SameLine();if(action(("mute."+input.name).c_str(),input.muted?"음소거 해제":"음소거",{100,28},s.obs.connected)) s.obsCall("SetInputMute",{{"inputName",input.name},{"inputMuted",!input.muted}});ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##volume",&input.db,-60,0,"%.0f dB");if(ImGui::IsItemDeactivatedAfterEdit())s.obsCall("SetInputVolume",{{"inputName",input.name},{"inputVolumeDb",input.db}});ImGui::PopID();}
        ImGui::End();
        panel("layout",{size.x-inspector,header},{inspector,size.y-header-footer},true); modelPanel(c,s,avatar); ImGui::End();
        border({size.x-inspector,header},{size.x-inspector,size.y-footer},p);
    } else if (page==Page::Models) {
        constexpr float library=290;
        panel("model-library",{side,header},{library,size.y-header-footer},true);
        if (action("model.import",s.importBusy?"가져오는 중":"모델 가져오기",{-1,36},!s.importBusy,"이전 모델을 가져오고 있어요.",true)) {auto path=platform::pickFile("Live2D 모델 가져오기",{"json","zip"});if(!path.empty())s.importAvatar(path);}
        gap(17);
        for (auto& model:c.models) if (row((model.name+"##"+model.id).c_str(),c.modelId==model.id,40)) {c.modelId=model.id;reload=true;s.save();}
        section("지원 파일");muted("model3.json 또는 모델 전체가 든 ZIP"); ImGui::End();
        panel("model-preview",{side+library,header},{workspace-library,size.y-header-footer},true);
        if (hasModel) {ImGui::TextUnformatted(c.selected()->name.c_str());gap(12);previewSize={ImGui::GetContentRegionAvail().x,std::max(300.f,size.y-220)};picture(ImGui::GetCursorScreenPos(),previewSize,false);gap(8);muted(fs::path(c.selected()->path).filename().string());} else muted("모델을 가져와 주세요.");ImGui::End();
    } else if (page==Page::Tracking) {
        constexpr float controls=340;
        panel("tracking-controls",{side,header},{controls,size.y-header-footer},true);trackingPanel(c,s,tracker,mic);ImGui::End();
        panel("tracking-preview",{side+controls,header},{workspace-controls,size.y-header-footer},true);
        status(tracker.hasFace()?"얼굴 추적 중":"얼굴 입력 대기",tracker.hasFace(),p);gap(12);
        previewSize={ImGui::GetContentRegionAvail().x,std::max(300.f,size.y-325)};landmarkCount=0;
        if(c.tracking.source=="webcam"){
            ImGui::Checkbox("카메라 영상 표시",&cameraFeed_);gap(8);
            auto camera=cameraFixture.active?cameraFixture:tracker.camera.snapshot();
            if(camera.preview.width>0&&camera.preview.height>0)cameraSize_={static_cast<float>(camera.preview.width),static_cast<float>(camera.preview.height)};
            if(camera.active&&cameraFeed_&&camera.previewSerial!=cameraSerial_){auto capture=cameraFixture.active?cameraFixture:tracker.camera.snapshot(true);cameraSerial_=capture.previewSerial;if(!capture.preview.rgba.empty())uploadTexture(cameraTexture_,capture.preview);}
            const auto position=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();const ImVec2 end{position.x+previewSize.x,position.y+previewSize.y};draw->AddRectFilled(position,end,IM_COL32(15,27,38,255),8);
            const float fit=std::min(previewSize.x/cameraSize_.x,previewSize.y/cameraSize_.y);const ImVec2 dimensions{cameraSize_.x*fit,cameraSize_.y*fit},start{position.x+(previewSize.x-dimensions.x)*.5f,position.y+(previewSize.y-dimensions.y)*.5f};
            if(camera.active&&cameraFeed_&&cameraTexture_)draw->AddImage((ImTextureID)cameraTexture_,start,{start.x+dimensions.x,start.y+dimensions.y},c.tracking.mirror?ImVec2(1,0):ImVec2(0,0),c.tracking.mirror?ImVec2(0,1):ImVec2(1,1),IM_COL32(180,180,180,255));
            if(camera.active&&camera.frame.valid&&(cameraFixture.active||now()-camera.frame.at<.6)){
                float left=1,right=0,top=1,bottom=0;for(const auto& contour:camera.frame.contours)for(const auto& p:contour.points){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}
                const float focus=std::min(previewSize.x*.68f/std::max(1.f,(right-left)*cameraSize_.x),previewSize.y*.78f/std::max(1.f,(bottom-top)*cameraSize_.y));
                auto point=[&](FacePoint p){if(cameraFeed_)return ImVec2(start.x+(c.tracking.mirror?1-p.x:p.x)*dimensions.x,start.y+p.y*dimensions.y);return ImVec2(position.x+previewSize.x*.5f+(p.x-(left+right)*.5f)*cameraSize_.x*focus*(c.tracking.mirror?-1:1),position.y+previewSize.y*.5f+(p.y-(top+bottom)*.5f)*cameraSize_.y*focus);};
                for(const auto& contour:camera.frame.contours){landmarkCount+=static_cast<int>(contour.points.size());for(size_t i=0;i<contour.points.size();++i){auto a=point(contour.points[i]);draw->AddCircleFilled(a,2.4f,IM_COL32(220,242,106,255));if(i==0&&!contour.closed)continue;auto b=point(contour.points[(i+contour.points.size()-1)%contour.points.size()]);const float dx=b.x-a.x,dy=b.y-a.y,length=std::sqrt(dx*dx+dy*dy);for(float distance=3;distance<length;distance+=7){float t=distance/length,u=std::min(distance+3,length)/length;draw->AddLine({a.x+dx*t,a.y+dy*t},{a.x+dx*u,a.y+dy*u},IM_COL32(25,183,242,210),1.2f);}}}
            }else draw->AddText({position.x+24,position.y+24},IM_COL32(173,191,203,255),camera.active?"정면을 바라보면 얼굴 점과 선이 보여요.":"웹캠 추적을 시작해 주세요.");
            ImGui::Dummy(previewSize);
            muted("얼굴 윤곽·눈·눈썹·코·입을 실시간으로 추적해요.");
        }else picture(ImGui::GetCursorScreenPos(),previewSize,false);
        gap(12); const auto& face=cameraFixture.active?cameraFixture.frame:tracker.frame();ImGui::Text("고개  %.1f / %.1f / %.1f",face.yaw,face.pitch,face.roll);ImGui::Text("눈  %.2f / %.2f    입  %.2f",face.eyeLeft,face.eyeRight,face.mouth);ImGui::End();
    } else if (page==Page::Voice) {
        panel("voice",{side,header},{workspace,size.y-header-footer-80},true);ImGui::SetNextItemWidth(-1);ImGui::BeginChild("voice-width",{std::min(760.f,workspace-44),0},ImGuiChildFlags_None);voicePanel(c,s);ImGui::EndChild();ImGui::End();
        panel("voice-transport",{side,size.y-footer-80},{workspace,80},false);
        const bool active=s.rvc.connected&&s.rvc.info.value("serverAudioStated",0)==1;
        if (action("rvc.convert",s.rvcBusy?"처리 중":active?"음성 변환 중지":"음성 변환 시작",{200,38},!s.rvcBusy&&(active||s.rvcReady()),"서버·RVC 모델·입력·출력 장치를 모두 선택해 주세요.",true)) {if(active)s.setRvc("serverAudioStated",0);else s.startRvc();}
        ImGui::SameLine();ImGui::TextDisabled("%s",active?"선택한 오디오 장치로 변환 중":s.rvcReady()?"변환 준비 완료":"서버·음성 모델·오디오 장치를 먼저 선택해 주세요.");ImGui::End();
    } else if (page==Page::Agent) {
        const float settings=330;
        panel("agent-settings",{side,header},{settings,size.y-header-footer},true);agentPanel(c,s);ImGui::End();
        panel("agent-chat",{side+settings,header},{workspace-settings,size.y-header-footer},false);chatPanel(c,s);ImGui::End();border({side+settings,header},{side+settings,size.y-footer},p);
    } else {
        panel("connections",{side,header},{workspace,size.y-header-footer},true);ImGui::BeginChild("connection-width",{std::min(640.f,workspace-44),0},ImGuiChildFlags_None);connections(c,s);ImGui::EndChild();ImGui::End();
    }
    panel("footer",{side,size.y-footer},{workspace,footer},false);ImGui::SetCursorPosY(7);
    status(s.obs.streaming?"방송 중":s.obs.connected?"방송 대기":"OBS 연결 전",s.obs.connected,p);ImGui::SameLine();ImGui::TextDisabled("  |  %s  |  최대 %dfps",tracker.hasFace()?"얼굴 추적 중":tracker.active()?"얼굴 입력 대기":"얼굴 추적 꺼짐",c.avatar.fps);ImGui::End();
    border({side,0},{side,size.y},p);border({side,header},{size.x,header},p);border({side,size.y-footer},{size.x,size.y-footer},p);
    if (!s.notice.empty()&&lastNotice_!=s.notice) {lastNotice_=s.notice;noticeUntil_=now()+4;}
    if (!s.error.empty()||now()<noticeUntil_) {
        ImGui::SetNextWindowPos({size.x*.5f,size.y-52},{},{.5f,1});ImGui::SetNextWindowSize({std::min(610.f,size.x-40),0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,8);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,1);
        ImGui::Begin("notice",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings);ImGui::PushTextWrapPos(ImGui::GetWindowWidth()-70);ImGui::TextColored(s.error.empty()?p.ink:p.danger,"%s",s.error.empty()?s.notice.c_str():s.error.c_str());ImGui::PopTextWrapPos();ImGui::SameLine();if(ImGui::SmallButton("닫기")){s.error.clear();noticeUntil_=0;}ImGui::End();ImGui::PopStyleVar(2);
    }
    onboarding(c,s);
}
}
