#define NOMINMAX
#include <iostream>
#include <thread>
#include <chrono>
#include <windows.h>

#include "../include/ZitroxState.hpp"
#include "../include/ZitroxInternal.hpp"
#include "LuaEngine.hpp"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winmm.lib")

int main(int argc, char* argv[]) {
    SetConsoleTitleA("Zitrox 1.0.1");
    RefreshHitSounds();
    CreateDirectoryA("C:\\ZitFem", NULL);
    LoadGrenadeLineupsFile();
    while (!mem.Attach("cs2.exe")) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    clientBase = mem.GetModuleAddress("client.dll");
    while (!clientBase) {
        clientBase = mem.GetModuleAddress("client.dll");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    engineBase = mem.GetModuleAddress("engine2.dll");
    std::thread cacheThread(EntityCacheThread);
    std::thread logicThread(LogicThread);
    std::thread autoAcceptThread(AutoAcceptThread);
    cacheThread.detach();
    logicThread.detach();
    autoAcceptThread.detach();
    CreateThread(nullptr, 0, MouseHookThread, nullptr, 0, nullptr);
    if (!overlay.Setup("SDL_app", "Counter-Strike 2")) return 1;
    overlay.ToggleTransparency(!settings.menuOpen);
    LuaEngine::Initialize();
    overlay.RenderCallback = RenderESP;
    while (!settings.requestUnload) overlay.Render();

    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    LuaEngine::Shutdown();
    overlay.Cleanup();
    ExitProcess(0);
    return 0;
}
