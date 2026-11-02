#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
namespace studio::platform {
struct Response { int status = 0; std::string body, error; };
Response request(const std::string& url, const std::string& method = "GET", const std::string& body = "", const std::map<std::string, std::string>& headers = {}, double timeout = 15);
void setRequestCancellation(const std::atomic<bool>* cancelled);
class Socket {
public:
    Socket(); ~Socket();
    void connect(const std::string& url, std::function<void(std::string)> message, std::function<void(std::string)> error, const std::string& protocol = "obswebsocket.json");
    void send(const std::string& text);
    void close();
private:
    struct Impl; std::shared_ptr<Impl> impl_;
};
struct Image { int width = 0, height = 0; std::vector<unsigned char> rgba; };
Image loadImage(const std::string& path, int limit);
Image loadImageBase64(const std::string& data);
Image qrImage(const std::string& text);
bool writePng(const std::string& path, int width, int height, const unsigned char* rgba);
std::string resources();
std::string dataDirectory();
std::string pickFile(const std::string& title, const std::vector<std::string>& extensions);
void copyText(const std::string& text);
void openUrl(const std::string& url);
std::string sha256Base64(const std::string& text);
std::string randomToken();
void configurePet(GLFWwindow* window);
void petCaption(GLFWwindow* window, const std::string& text);
void installMenu(std::function<void(int)> action);
void removeMenu();
void clickThrough(GLFWwindow* window, bool enabled);
void globalMouse(double& x, double& y);
double residentMegabytes();
double footprintMegabytes();
void drainEvents();
class Microphone {
public:
    Microphone(); ~Microphone();
    void start(); void stop(); bool active() const; float level() const; std::string error() const;
private:
    struct Impl; std::shared_ptr<Impl> impl_;
};
void speak(const std::string& text);
void stopSpeech();
bool speaking();
}
