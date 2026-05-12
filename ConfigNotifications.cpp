#define NOMINMAX
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <mutex>
#include <utility>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <winsock2.h>
#include <iphlpapi.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "../include/ZitroxState.hpp"
#include "../include/ZitroxInternal.hpp"
#include "gui.hpp"
#include "LuaEngine.hpp"

#include "../offsets/client_dll.hpp"
#include "../offsets/offsets.hpp"

#pragma comment(lib, "iphlpapi.lib")

using namespace cs2_dumper::offsets::client_dll;
using namespace cs2_dumper::offsets::engine2_dll;
using namespace cs2_dumper::schemas::client_dll;

namespace fs = std::filesystem;

void PushNotification(const std::string& text, float r, float g, float b, uint32_t durationMs) {
    std::lock_guard<std::mutex> lk(g_notifMutex);
    Notification n;
    n.text = text;
    n.startTime = GetTickCount64();
    n.duration = durationMs;
    n.color[0] = r; n.color[1] = g; n.color[2] = b; n.color[3] = 1.0f;
    g_notifications.push_back(n);
    if (g_notifications.size() > 6) g_notifications.erase(g_notifications.begin());
}

void RenderNotifications() {
    std::lock_guard<std::mutex> lk(g_notifMutex);
    uint64_t now = GetTickCount64();
    g_notifications.erase(std::remove_if(g_notifications.begin(), g_notifications.end(), [now](const Notification& n) {
        return now - n.startTime > n.duration;
    }), g_notifications.end());
    
    if (g_notifications.empty()) return;
    
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float baseX = overlay.Width / 2.0f;
    float startY = 60.0f;
    
    for (size_t i = 0; i < g_notifications.size(); i++) {
        const auto& n = g_notifications[i];
        uint64_t age = now - n.startTime;
        float lifeFrac = (float)age / n.duration;
        float alpha = 1.0f;
        if (lifeFrac > 0.85f) alpha = 1.0f - (lifeFrac - 0.85f) / 0.15f;
        if (lifeFrac < 0.1f) alpha = lifeFrac / 0.1f;
        if (alpha < 0) alpha = 0;
        
        ImVec2 size = ImGui::CalcTextSize(n.text.c_str());
        float w = size.x + 22.0f;
        float h = size.y + 12.0f;
        float x = baseX - w / 2.0f;
        float y = startY + i * (h + 6.0f);
        
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(0.0f, 0.0f, 0.0f, 0.78f * alpha), 6.0f);
        draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(n.color[0], n.color[1], n.color[2], alpha), 6.0f, 0, 1.5f);
        // акцент-полоса слева
        draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 4, y + h), ImColor(n.color[0], n.color[1], n.color[2], alpha), 6.0f, ImDrawFlags_RoundCornersLeft);
        draw->AddText(ImVec2(x + 11, y + 6), ImColor(1.0f, 1.0f, 1.0f, alpha), n.text.c_str());
    }
}

void RefreshHitSounds() {
    g_hitSounds.clear();
    CreateDirectoryA("C:\\ZitFem", NULL);
    CreateDirectoryA("C:\\ZitFem\\sounds", NULL);
    try {
        for (const auto& entry : std::filesystem::directory_iterator(g_hitSoundsDir)) {
            if (entry.path().extension() == ".wav") {
                g_hitSounds.push_back(entry.path().filename().string());
            }
        }
    } catch (...) {}
}

void SaveConfig(const std::string& name) {
    CreateDirectoryA("C:\\ZitFem", NULL);
    std::string path = "C:\\ZitFem\\" + name + ".fem";
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "wb");
    if (!f) return;
    fwrite(&settings.theme, sizeof(settings.theme), 1, f);
    fwrite(&settings.aim, sizeof(settings.aim), 1, f);
    fwrite(&settings.trigger, sizeof(settings.trigger), 1, f);
    fwrite(&settings.visuals, sizeof(settings.visuals), 1, f);
    fwrite(&settings.misc, sizeof(settings.misc), 1, f);
    fwrite(&settings.aimExtra, sizeof(settings.aimExtra), 1, f);
    size_t hmCount = g_hmShapes.size();
    fwrite(&hmCount, sizeof(hmCount), 1, f);
    if (hmCount) fwrite(g_hmShapes.data(), sizeof(HitMarkerShape), hmCount, f);
    
    // Section sizes
    size_t sectCount = gui::g_sectionHeights.size();
    fwrite(&sectCount, sizeof(sectCount), 1, f);
    for (const auto& kv : gui::g_sectionHeights) {
        size_t keyLen = kv.first.size();
        fwrite(&keyLen, sizeof(keyLen), 1, f);
        fwrite(kv.first.data(), 1, keyLen, f);
        fwrite(&kv.second, sizeof(float), 1, f);
    }
    
    fwrite(&settings.grenadeTypeColors, sizeof(settings.grenadeTypeColors), 1, f);
    fwrite(&settings.grenadeHelper, sizeof(settings.grenadeHelper), 1, f);
    fclose(f);
    PushNotification("Saved config: " + name, 0.5f, 1.0f, 0.5f);
}

void LoadConfig(const std::string& name) {
    std::string path = "C:\\ZitFem\\" + name + ".fem";
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "rb");
    if (!f) return;
    fread(&settings.theme, sizeof(settings.theme), 1, f);
    fread(&settings.aim, sizeof(settings.aim), 1, f);
    fread(&settings.trigger, sizeof(settings.trigger), 1, f);
    fread(&settings.visuals, sizeof(settings.visuals), 1, f);
    fread(&settings.misc, sizeof(settings.misc), 1, f);
    if (!feof(f)) fread(&settings.aimExtra, sizeof(settings.aimExtra), 1, f);
    size_t hmCount = 0;
    if (fread(&hmCount, sizeof(hmCount), 1, f) == 1 && hmCount < 1024) {
        g_hmShapes.resize(hmCount);
        if (hmCount) fread(g_hmShapes.data(), sizeof(HitMarkerShape), hmCount, f);
    }
    
    // Section sizes
    size_t sectCount = 0;
    if (fread(&sectCount, sizeof(sectCount), 1, f) == 1 && sectCount < 256) {
        for (size_t i = 0; i < sectCount; i++) {
            size_t keyLen = 0;
            if (fread(&keyLen, sizeof(keyLen), 1, f) != 1) break;
            if (keyLen == 0 || keyLen > 256) break;
            std::string key(keyLen, '\0');
            if (fread(key.data(), 1, keyLen, f) != keyLen) break;
            float h = 0;
            if (fread(&h, sizeof(float), 1, f) != 1) break;
            if (h >= 80.0f && h <= 500.0f) gui::g_sectionHeights[key] = h;
        }
    }
    if (fread(&settings.grenadeTypeColors, sizeof(settings.grenadeTypeColors), 1, f) != 1) {
        static const float kDef[6][4] = {
            { 1.0f, 0.15f, 0.15f, 1.0f },
            { 1.0f, 1.0f, 0.25f, 1.0f },
            { 0.55f, 0.55f, 0.55f, 1.0f },
            { 1.0f, 0.5f, 0.0f, 1.0f },
            { 0.4f, 0.75f, 1.0f, 1.0f },
            { 1.0f, 0.35f, 0.0f, 1.0f },
        };
        memcpy(settings.grenadeTypeColors, kDef, sizeof(kDef));
    }
    if (fread(&settings.grenadeHelper, sizeof(settings.grenadeHelper), 1, f) != 1) {
        settings.grenadeHelper = {};
        settings.grenadeHelper.enabled = true;
        settings.grenadeHelper.drawOverlay = true;
        settings.grenadeHelper.selectedGlobalIndex = -1;
        settings.grenadeHelper.posTolerance = 40.f;
        settings.grenadeHelper.jumpThrowDelayMs = 120;
        settings.grenadeHelper.runThrowPreMs = 110;
        settings.grenadeHelper.standCircleMode = 0;
        settings.grenadeHelper.standRingRadius = 40.f;
        settings.grenadeHelper.overlayGrenadeMask = 0x3F;
        strcpy_s(settings.grenadeHelper.newLineupName, "spot");
    }
    if (settings.grenadeHelper.recordMode < 0 || settings.grenadeHelper.recordMode > 3)
        settings.grenadeHelper.recordMode = 0;
    if (settings.grenadeHelper.standCircleMode < 0 || settings.grenadeHelper.standCircleMode > 1)
        settings.grenadeHelper.standCircleMode = 0;
    settings.grenadeHelper.overlayGrenadeMask &= 0x3F;
    if (settings.grenadeHelper.overlayGrenadeMask == 0)
        settings.grenadeHelper.overlayGrenadeMask = 0x3F;

    fclose(f);
    PushNotification("Loaded config: " + name, 0.5f, 0.85f, 1.0f);
}

void DeleteConfig(const std::string& name) {
    std::string path = "C:\\ZitFem\\" + name + ".fem";
    fs::remove(path);
}

std::vector<std::string> GetConfigs() {
    std::vector<std::string> configs;
    CreateDirectoryA("C:\\ZitFem", NULL);
    if (fs::exists("C:\\ZitFem")) {
        for (const auto& entry : fs::directory_iterator("C:\\ZitFem")) {
            if (entry.path().extension() == ".fem") {
                configs.push_back(entry.path().stem().string());
            }
        }
    }
    return configs;
}
