#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>

#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;
using namespace std::chrono;

struct KeyConfig {
    int vkCode;
    bool withShift = false;
    bool withCtrl = false;
    bool withAlt = false;
};

struct Config {
	std::string lirc_host = "";
    int lirc_port = 8765;
    int lirc_rc_attempts = 0;
    int lirc_rc_inverval = 1;
	int key_timeout = 150;
    int key_repeat_delay = 3;
};

class RemoteControl {
private:
    Config conf;
    std::unordered_map<std::string, KeyConfig> keyMappings;
    std::atomic<bool> running{ true };
    SOCKET clientSocket = INVALID_SOCKET;

    steady_clock::time_point lastPacketTime;

    std::string currentKey;
    std::atomic<bool> keyIsDown = false;

    std::thread timeoutThread;

private:
    void loadConfig(const std::string& filename) {
        std::ifstream configFile(filename);
        if (!configFile.is_open()) {
            throw std::runtime_error("Unable to open config file");
        }

        json config;
        configFile >> config;

        if (config.contains("key_timeout")) {
            conf.key_timeout = config["key_timeout"].get<int>();
        }
        if (config.contains("key_repeat_delay")) {
            conf.key_repeat_delay = config["key_repeat_delay"].get<int>();
        }
        if (config.contains("lirc_rc_inverval")) {
            conf.lirc_rc_inverval = config["lirc_rc_inverval"].get<int>();
        }
        if (config.contains("lirc_rc_attempts")) {
            conf.lirc_rc_attempts = config["lirc_rc_attempts"].get<int>();
        }
        if (config.contains("lirc_host")) {
            conf.lirc_host = config["lirc_host"].get<std::string>();
        }
        if (config.contains("lirc_port")) {
            conf.lirc_port = config["lirc_port"].get<int>();
        }
    }

    void loadKeymap(const std::string& filename) {
        std::ifstream configFile(filename);
        if (!configFile.is_open()) {
            throw std::runtime_error("Unable to open keymap file");
        }

        json config;
        configFile >> config;

        for (const auto& [key, value] : config.items()) {
            KeyConfig keyConfig;

            if (!value.contains("vkCode"))
                continue;
            if (value["vkCode"].is_number()) {
                keyConfig.vkCode = value["vkCode"].get<WORD>();
            } else {
                std::string vkCodeStr = value["vkCode"].get<std::string>();
                keyConfig.vkCode = static_cast<WORD>(std::stoi(vkCodeStr, nullptr, 16));
            }

            if (value.contains("withShift"))
                keyConfig.withShift = value["withShift"].get<bool>();
            if (value.contains("withCtrl"))
                keyConfig.withCtrl = value["withCtrl"].get<bool>();
            if (value.contains("withAlt"))
                keyConfig.withAlt = value["withAlt"].get<bool>();

            keyMappings[key] = keyConfig;
        }
    }

    void sendInput(int keyCode, bool isKeyDown) {
        INPUT input = {};

        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        input.ki.dwFlags = isKeyDown ? 0 : KEYEVENTF_KEYUP;

        SendInput(1, &input, sizeof(INPUT));
        std::this_thread::sleep_for(milliseconds(1));
    }

    void simulateKeyEvent(const KeyConfig& keyConfig, bool isKeyDown) {
        if (!isKeyDown) {
            //std::cout << "KeyUP event: " << keyConfig.vkCode << std::endl;
            sendInput(keyConfig.vkCode, isKeyDown);
        }

        if (keyConfig.withShift)
            sendInput(VK_SHIFT, isKeyDown);
        if (keyConfig.withCtrl)
            sendInput(VK_CONTROL, isKeyDown);
        if (keyConfig.withAlt)
            sendInput(VK_MENU, isKeyDown);

        if (isKeyDown) {
            //std::cout << "KeyDOWN event: " << keyConfig.vkCode << std::endl;
            sendInput(keyConfig.vkCode, isKeyDown);
        }
    }

    void processWinLIRCMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string code, repeat_str, button, remote;
        iss >> code >> repeat_str >> button >> remote;

        // Convert repeat from hex string to integer
        int repeat = std::stoi(repeat_str, nullptr, 16);

        auto it = keyMappings.find(button);
        if (it != keyMappings.end()) {
            lastPacketTime = steady_clock::now();
            if (repeat == 0) {
                // New button press or same button pressed again
                if (keyIsDown) {
                    // Release the previous key if it's still down
                    simulateKeyEvent(keyMappings[currentKey], false);
                    keyIsDown = false;
                }
                // Press the new key
                simulateKeyEvent(it->second, true);
                currentKey = button;
                keyIsDown = true;
            }
            else if (repeat >= conf.key_repeat_delay) {
                simulateKeyEvent(keyMappings[currentKey], true);
            }
        } else if (repeat == 0) {
            std::cout << "Unmapped button is pressed: " << message;
        }
    }

    bool connectToWinLIRC() {
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
        }

        clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == INVALID_SOCKET) {
            throw std::runtime_error("Socket creation failed!");
            return false;
        }

        sockaddr_in serverAddr{0};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(conf.lirc_port);
        inet_pton(AF_INET, conf.lirc_host.c_str(), &serverAddr.sin_addr);

        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            //std::cerr << "Connection to WinLIRC failed: " << WSAGetLastError() << std::endl;
            closesocket(clientSocket);
            clientSocket = INVALID_SOCKET;
            return false;
        }

        std::cout << "Connected to WinLIRC server" << std::endl;
        return true;
    }

    void timeoutHandler() {
        while (running) {
            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(steady_clock::now());
            auto elapsed = duration_cast<milliseconds>(now - lastPacketTime);
            if (keyIsDown && elapsed.count() > conf.key_timeout) {
                simulateKeyEvent(keyMappings[currentKey], false);
                keyIsDown = false;
            }
            std::this_thread::sleep_for(milliseconds(1));
        }
    }

public:
    RemoteControl(const std::string& configFile, const std::string& keymapFile) {
        loadConfig(configFile);
        loadKeymap(keymapFile);

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }

        if (!connectToWinLIRC()) {
            throw std::runtime_error("Initial connection to WinLIRC failed");
        }

        timeoutThread = std::thread(&RemoteControl::timeoutHandler, this);
    }

    ~RemoteControl() {
        stop();
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
        }
        WSACleanup();
    }

    void run() {
        char buffer[512];
        int bytesReceived;
        int reconnectAttempts = 0;

        while (running) {
            if (clientSocket == INVALID_SOCKET) {
                //std::cout << "Attempting to reconnect..." << std::endl;
                if (connectToWinLIRC()) {
                    reconnectAttempts = 0;
                }
                else {
                    reconnectAttempts++;
                    if (conf.lirc_rc_attempts > 0 && reconnectAttempts >= conf.lirc_rc_attempts) {
                        throw std::runtime_error("Max reconnection attempts reached. Exiting.");
                        stop();
                        break;
                    }
                    std::this_thread::sleep_for(milliseconds(conf.lirc_rc_inverval));
                    continue;
                }
            }

            bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                processWinLIRCMessage(buffer);
            }
            else if (bytesReceived == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                //std::cout << "Connection closed or error occurred. Reconnecting..." << std::endl;
                closesocket(clientSocket);
                clientSocket = INVALID_SOCKET;
                continue;
            } else {
                // No data available, small delay to prevent busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    void stop() {
        running = false;
    }
};

//int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR cmdLine, int nCmdShow)
int main(int argc, char* argv[])
{
    try {
        RemoteControl control("config.json", "keymap.json");
        std::thread controlThread(&RemoteControl::run, &control);

        std::cout << "WinLIRC Client is running. Press Enter to exit." << std::endl;
        std::cin.get();

        control.stop();
        controlThread.join();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}