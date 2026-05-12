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

void RenderGrenadePrediction() {
    if (!settings.visuals.grenadePrediction || !cachedLocalPawn) return;

    uintptr_t activeWeapon = GetActiveWeapon(cachedLocalPawn);
    if (!activeWeapon) return;

    uint16_t wId = GetWeaponID(cachedLocalPawn);
    if (wId == 0) wId = GetWeaponEntityID(activeWeapon);
    if (GrenadeTypeIndexFromWeaponId(wId) < 0) return;

    // Mid-throw: траектория из руки не рисуется (как catalyst m_fThrowTime)
    float throwTime = mem.Read<float>(activeWeapon + 0x1CB8); // m_fThrowTime GameTime_t
    if (throwTime > 0.0f) return;

    bool pinPulled = mem.Read<bool>(activeWeapon + 0x1CB3);
    float strength = 1.0f;
    if (pinPulled) {
        strength = std::clamp(mem.Read<float>(activeWeapon + 0x1CC0), 0.0f, 1.0f); // m_flThrowStrength
        if (std::fabs(strength - 0.5f) <= 0.1f)
            strength = 0.5f;
    }

    uintptr_t vData = mem.Read<uintptr_t>(activeWeapon + 0x380 + 0x8);
    float throwVelocity = 750.0f;
    if (vData) throwVelocity = mem.Read<float>(vData + 0x860); // m_flThrowVelocity
    float throwVelClamped = std::clamp(throwVelocity * 0.9f, 15.0f, 750.0f);
    float throwSpeed = (strength * 0.7f + 0.3f) * throwVelClamped;

    // Углы как в catalyst::grenades::setup_throw (без инверсии всего pitch)
    Vector3 vAngles = cachedViewAngles;
    if (vAngles.x > 90.0f) vAngles.x -= 360.0f;
    else if (vAngles.x < -90.0f) vAngles.x += 360.0f;
    vAngles.x -= (90.0f - std::fabs(vAngles.x)) * 10.0f / 90.0f;

    float pitch = vAngles.x * (3.14159265f / 180.0f);
    float yaw = vAngles.y * (3.14159265f / 180.0f);

    Vector3 forward = {
        std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw),
        -std::sin(pitch)
    };

    Vector3 startPos = cachedLocalEyePos;
    startPos.z += strength * 12.0f - 12.0f;
    // Без BVH: как промах трассы в catalyst — отпускание ~на 16 u вперёд от глаз
    startPos = startPos + forward * 16.0f;

    Vector3 playerVel = mem.Read<Vector3>(cachedLocalPawn + 0x3FC); // m_vecAbsVelocity
    Vector3 velocity = forward * throwSpeed + playerVel * 1.25f;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImColor col = GrenadeColorForTypeIndex(GrenadeTypeIndexFromWeaponId(wId));
    
    Vector3 lastPos = startPos;
    float gravity = 800.0f * 0.4f; 
    float step = 0.03f; 

    for (int i = 1; i <= 60; i++) {
        float t = i * step;
        Vector3 nextPos = {
            startPos.x + velocity.x * t,
            startPos.y + velocity.y * t,
            startPos.z + velocity.z * t - 0.5f * gravity * t * t
        };

        Vector2 s1, s2;
        if (WorldToScreen(lastPos, s1, cachedViewMatrix, overlay.Width, overlay.Height) && 
            WorldToScreen(nextPos, s2, cachedViewMatrix, overlay.Width, overlay.Height)) {
            drawList->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), col, 2.0f);
        }
        lastPos = nextPos;
    }
}

bool DrawDynamicBox(ImDrawList* drawList, const CachedPlayer& p, const ViewMatrix& vm, ImColor col, ImColor outline) {
    Vector3 toLocal = cachedLocalOrigin - p.origin;
    float len = std::sqrt(toLocal.x * toLocal.x + toLocal.y * toLocal.y);
    if (len < 0.001f) return false;

    Vector3 forward = { toLocal.x / len, toLocal.y / len, 0.0f };
    Vector3 right = { -forward.y, forward.x, 0.0f };
    float height = std::clamp(p.headPos.z - p.origin.z + 8.0f, 55.0f, 90.0f);
    float halfW = 18.0f;
    float halfD = 12.0f;
    Vector3 base = p.origin;
    Vector3 top = { p.origin.x, p.origin.y, p.origin.z + height };

    Vector3 corners[8] = {
        { base.x + right.x * halfW + forward.x * halfD, base.y + right.y * halfW + forward.y * halfD, base.z },
        { base.x - right.x * halfW + forward.x * halfD, base.y - right.y * halfW + forward.y * halfD, base.z },
        { base.x - right.x * halfW - forward.x * halfD, base.y - right.y * halfW - forward.y * halfD, base.z },
        { base.x + right.x * halfW - forward.x * halfD, base.y + right.y * halfW - forward.y * halfD, base.z },
        { top.x + right.x * halfW + forward.x * halfD, top.y + right.y * halfW + forward.y * halfD, top.z },
        { top.x - right.x * halfW + forward.x * halfD, top.y - right.y * halfW + forward.y * halfD, top.z },
        { top.x - right.x * halfW - forward.x * halfD, top.y - right.y * halfW - forward.y * halfD, top.z },
        { top.x + right.x * halfW - forward.x * halfD, top.y + right.y * halfW - forward.y * halfD, top.z }
    };

    Vector2 screen[8];
    for (int i = 0; i < 8; i++) {
        if (!WorldToScreen(corners[i], screen[i], vm, overlay.Width, overlay.Height)) return false;
    }

    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    for (auto& edge : edges) {
        ImVec2 a(screen[edge[0]].x, screen[edge[0]].y);
        ImVec2 b(screen[edge[1]].x, screen[edge[1]].y);
        drawList->AddLine(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), outline, 3.0f);
        drawList->AddLine(a, b, col, 1.0f);
    }
    return true;
}
static void RenderOverlayModelGlow(ImDrawList* drawList, const CachedPlayer& p, const ViewMatrix& vm, float boxHeight) {
    if (!settings.visuals.glow || !p.bonesValid) return;

    ImVec4 base(
        settings.visuals.glowColor[0],
        settings.visuals.glowColor[1],
        settings.visuals.glowColor[2],
        std::clamp(settings.visuals.glowColor[3] * settings.visuals.glowIntensity, 0.0f, 1.0f)
    );

    const float scale = std::clamp(boxHeight / 115.0f, 0.55f, 2.6f);
    const float outerThick = std::clamp(9.0f * scale, 4.0f, 18.0f);
    const float midThick = std::clamp(5.5f * scale, 2.5f, 12.0f);
    const float dotRadius = std::clamp(4.5f * scale, 2.5f, 9.0f);
    const ImU32 outerCol = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, base.w * 0.12f));
    const ImU32 midCol = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, base.w * 0.22f));
    const ImU32 innerCol = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, base.w * 0.36f));
    const ImU32 dotOuterCol = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, base.w * 0.18f));
    const ImU32 dotInnerCol = ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, base.w * 0.32f));

    for (const auto& c : kBoneConnections) {
        const Vector3& a = p.bones[c.first].position;
        const Vector3& b = p.bones[c.second].position;
        if (a.IsZero() || b.IsZero()) continue;
        if ((a - b).Length() > 80.0f) continue;

        Vector2 s1, s2;
        if (!WorldToScreen(a, s1, vm, overlay.Width, overlay.Height) ||
            !WorldToScreen(b, s2, vm, overlay.Width, overlay.Height)) {
            continue;
        }

        ImVec2 p1(s1.x, s1.y);
        ImVec2 p2(s2.x, s2.y);
        drawList->AddLine(p1, p2, outerCol, outerThick);
        drawList->AddLine(p1, p2, midCol, midThick);
        drawList->AddLine(p1, p2, innerCol, 1.4f);
    }

    static const int glowBones[] = { 6, 4, 1, 9, 13, 17, 20 };
    for (int boneId : glowBones) {
        if (p.bones[boneId].position.IsZero()) continue;
        Vector2 sp;
        if (!WorldToScreen(p.bones[boneId].position, sp, vm, overlay.Width, overlay.Height)) continue;
        ImVec2 center(sp.x, sp.y);
        drawList->AddCircleFilled(center, dotRadius * 2.4f, dotOuterCol, 24);
        drawList->AddCircleFilled(center, dotRadius, dotInnerCol, 20);
    }
}

std::string GetServerIP() {
    if (!engineBase) return "offline";
    uintptr_t netClient = mem.Read<uintptr_t>(engineBase + dwNetworkGameClient);
    if (!netClient) return "offline";
    
    int signOnState = mem.Read<int>(netClient + dwNetworkGameClient_signOnState);
    if (signOnState < 6) return "offline";
    
    char ipBuf[64] = { 0 };
    mem.Read(netClient + 0x278, ipBuf);
    ipBuf[63] = 0;
    
    std::string ip(ipBuf);
    if (ip.empty() || ip.find_first_not_of(" \t\r\n") == std::string::npos) return "loopback";
    return ip;
}

std::string GetSteamName() {
    if (!cachedLocalPawn || !clientBase) return "user";
    uintptr_t localController = mem.Read<uintptr_t>(clientBase + dwLocalPlayerController);
    if (!localController) return "user";
    
    char nameBuf[128] = { 0 };
    mem.Read(localController + 0x6F4, nameBuf); // m_iszPlayerName
    nameBuf[127] = 0;
    
    std::string name(nameBuf);
    if (name.empty()) {
        uintptr_t namePtr = mem.Read<uintptr_t>(localController + 0x860);
        if (namePtr) name = mem.ReadString(namePtr);
    }
    return name.empty() ? "user" : name;
}

std::string GetLocalIp() {
    IP_ADAPTER_INFO adapters[16];
    DWORD size = sizeof(adapters);
    if (GetAdaptersInfo(adapters, &size) != NO_ERROR) return "0.0.0.0";
    for (PIP_ADAPTER_INFO adapter = adapters; adapter; adapter = adapter->Next) {
        std::string ip = adapter->IpAddressList.IpAddress.String;
        if (!ip.empty() && ip != "0.0.0.0" && ip.rfind("169.254", 0) != 0) return ip;
    }
    return "0.0.0.0";
}

void RenderOverlayRadar() {
    if (!settings.visuals.overlayRadar) return;

    ImGui::SetNextWindowSize(ImVec2(220, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, UI::bgColor);
    ImGui::PushStyleColor(ImGuiCol_Border, UI::borderOuter);
    if (!ImGui::Begin("Radar", &settings.visuals.overlayRadar, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 accent = ImGui::GetColorU32(UI::accentColor);
    ImU32 titleBg = ImGui::GetColorU32(ImVec4(UI::accentColor.x, UI::accentColor.y, UI::accentColor.z, 0.22f));
    draw->AddRectFilled(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + 28.0f), titleBg, 8.0f, ImDrawFlags_RoundCornersTop);
    draw->AddRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y), accent, 8.0f, 0, 1.2f);
    ImGui::SetCursorPos(ImVec2(12.0f, 7.0f));
    ImGui::TextColored(UI::accentColor, "Overlay Radar");
    ImGui::SetCursorPosY(34.0f);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    float side = (std::min)(size.x, size.y);
    if (side < 80.0f) side = 80.0f;

    ImVec2 center(pos.x + side * 0.5f, pos.y + side * 0.5f);
    float radius = side * 0.5f - 8.0f;
    ImU32 bg = ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.02f, 0.85f));
    ImU32 border = ImGui::GetColorU32(UI::accentColor);
    ImU32 grid = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.12f));

    draw->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), bg, 6.0f);
    draw->AddRect(pos, ImVec2(pos.x + side, pos.y + side), border, 6.0f);
    draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), grid);
    draw->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), grid);

    Vector3 localOrigin = cachedLocalOrigin;
    Vector3 viewAngles = cachedViewAngles;
    float yaw = viewAngles.y * 3.14159265f / 180.0f;
    ImVec2 forward(std::cos(yaw), std::sin(yaw));
    ImVec2 right(-std::sin(yaw), std::cos(yaw));
    float range = (std::max)(settings.visuals.radarRange, 200.0f);

    ImU32 localCol = ImGui::GetColorU32(ImVec4(0.35f, 0.75f, 1.0f, 1.0f));
    ImU32 localOutline = ImGui::GetColorU32(ImVec4(0, 0, 0, 0.85f));
    ImVec2 arrowDir(0.0f, -1.0f);
    ImVec2 arrowRightDir(1.0f, 0.0f);
    ImVec2 arrowTip(center.x + arrowDir.x * 14.0f, center.y + arrowDir.y * 14.0f);
    ImVec2 arrowBack(center.x - arrowDir.x * 8.0f, center.y - arrowDir.y * 8.0f);
    ImVec2 arrowLeft(arrowBack.x - arrowRightDir.x * 7.0f, arrowBack.y - arrowRightDir.y * 7.0f);
    ImVec2 arrowRight(arrowBack.x + arrowRightDir.x * 7.0f, arrowBack.y + arrowRightDir.y * 7.0f);
    draw->AddTriangleFilled(arrowTip, arrowLeft, arrowRight, localOutline);
    draw->AddTriangleFilled(
        ImVec2(center.x + arrowDir.x * 11.0f, center.y + arrowDir.y * 11.0f),
        ImVec2(center.x - arrowDir.x * 6.0f - arrowRightDir.x * 5.0f, center.y - arrowDir.y * 6.0f - arrowRightDir.y * 5.0f),
        ImVec2(center.x - arrowDir.x * 6.0f + arrowRightDir.x * 5.0f, center.y - arrowDir.y * 6.0f + arrowRightDir.y * 5.0f),
        localCol
    );

    std::lock_guard<std::mutex> lock(cacheMutex);
    for (const auto& p : players) {
        Vector3 delta = p.origin - localOrigin;
        float x = delta.x * right.x + delta.y * right.y;
        float y = delta.x * forward.x + delta.y * forward.y;
        x = std::clamp(x / range, -1.0f, 1.0f);
        y = std::clamp(y / range, -1.0f, 1.0f);

        ImVec2 point(center.x + x * radius, center.y - y * radius);
        ImU32 col = ImGui::GetColorU32(ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        draw->AddCircleFilled(point, 3.5f, col);
    }

    ImGui::Dummy(ImVec2(side, side));
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void RenderWatermark() {
    if (!settings.misc.watermark) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const char* brand = settings.theme == 0 ? "femboy.meow" : "zitrox.vip";

    if (settings.misc.watermarkStyle == 0) {
        draw->AddText(ImVec2(10.0f, 10.0f), ImGui::GetColorU32(UI::accentColor), brand);
        return;
    }

    std::string steamName = GetSteamName();
    std::string serverIp = GetServerIP();

    int fps = (int)ImGui::GetIO().Framerate;
    int ping = 0;
    uintptr_t localController = clientBase ? mem.Read<uintptr_t>(clientBase + dwLocalPlayerController) : 0;
    if (localController) ping = mem.Read<int>(localController + 0x828);

    char buf[384];
    sprintf_s(buf, "%s | %s | %d fps | %d ms | %s", brand, steamName.c_str(), fps, ping, serverIp.c_str());

    ImVec2 pos(10.0f, 10.0f);
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImVec2 size(textSize.x + 18.0f, textSize.y + 12.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.03f, 0.88f)), 5.0f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(UI::accentColor), 5.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + 4.0f, pos.y + size.y), ImGui::GetColorU32(UI::accentColor), 5.0f, ImDrawFlags_RoundCornersLeft);
    draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f)), buf);
}

void RenderSpectatorList() {
    if (!settings.visuals.spectatorList || spectators.empty()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 pos(10.0f, 46.0f);
    float width = 150.0f;
    float height = 28.0f + spectators.size() * 17.0f;
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.03f, 0.82f)), 6.0f);
    draw->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(UI::accentColor), 6.0f);
    draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 7.0f), ImGui::GetColorU32(UI::accentColor), "spectators");

    float y = pos.y + 26.0f;
    for (const auto& name : spectators) {
        draw->AddText(ImVec2(pos.x + 10.0f, y), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f)), name.c_str());
        y += 17.0f;
    }
}

void RenderKeybindsOverlay() {
    if (!settings.misc.keybindsOverlay) return;
    
    struct BindEntry { const char* name; int key; };
    std::vector<BindEntry> binds;
    
    // Показываем только те бинды, которые СЕЙЧАС активны
    if (settings.aim.enabledBind.key && settings.aim.enabledBind.active)
        binds.push_back({ "Aimbot", settings.aim.enabledBind.key });
    if (settings.trigger.hotkey.key && settings.trigger.enabled && settings.trigger.hotkey.active)
        binds.push_back({ "Triggerbot", settings.trigger.hotkey.key });
    if (settings.aim.chickenAimbotBind.key && settings.aim.chickenAimbot && settings.aim.chickenAimbotBind.active)
        binds.push_back({ "Chicken Aim", settings.aim.chickenAimbotBind.key });
    if (binds.empty()) return;
    
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float lineH = 20.0f;
    float pad = 8.0f;
    
    // ширина по самой длинной строке
    float maxW = 0.0f;
    std::vector<std::string> lines;
    for (const auto& b : binds) {
        char buf[64];
        sprintf_s(buf, "%s [%s]", b.name, GetKeyName(b.key));
        lines.push_back(buf);
        ImVec2 sz = ImGui::CalcTextSize(buf);
        if (sz.x > maxW) maxW = sz.x;
    }
    
    float width = maxW + pad * 2 + 6.0f;
    float height = 26.0f + binds.size() * lineH + 4.0f;
    float x = overlay.Width - width - 12.0f;
    float y = 80.0f;
    
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.03f, 0.82f)), 6.0f);
    draw->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), ImGui::GetColorU32(UI::accentColor), 6.0f);
    draw->AddText(ImVec2(x + pad, y + 6), ImGui::GetColorU32(UI::accentColor), "keybinds");
    
    float ly = y + 26.0f;
    for (const auto& s : lines) {
        draw->AddText(ImVec2(x + pad, ly), ImColor(120, 240, 120), s.c_str());
        ly += lineH;
    }
}

void RenderHitLog() {
    if (!settings.misc.hitLogger) return;
    std::lock_guard<std::mutex> lk(g_hitLogMutex);
    
    uint64_t now = GetTickCount64();
    // удаляем старые (>5 сек)
    g_hitLog.erase(std::remove_if(g_hitLog.begin(), g_hitLog.end(), [now](const HitLogEntry& e) {
        return now - e.time > 5000;
    }), g_hitLog.end());
    
    if (g_hitLog.empty()) return;
    
    const int n = (int)g_hitLog.size();
    const float rowStep = 24.0f;
    const float rowH = 22.0f;
    const float padX = 8.0f;
    char buf[256];

    float maxInnerW = 200.0f;
    for (int i = n - 1; i >= 0; i--) {
        const auto& e = g_hitLog[i];
        sprintf_s(buf, "Hit %s for %d (%d hp left)", e.targetName.c_str(), e.dmg, e.remainingHp);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float need = ts.x + padX * 2.0f;
        if (need > maxInnerW) maxInnerW = need;
    }
    float width = std::clamp(maxInnerW, 220.0f, overlay.Width * 0.92f);
    float blockH = (float)n * rowStep;

    float& ax = settings.misc.hitLogAnchorX;
    float& ay = settings.misc.hitLogAnchorY;
    if (!std::isfinite(ax) || !std::isfinite(ay) || ax < -500.f || ay < -500.f || ax > overlay.Width + 500.f || ay > overlay.Height + 500.f) {
        ax = -1.0f;
        ay = -1.0f;
    }
    if (ax < 0.0f || ay < 0.0f) {
        ax = overlay.Width - width - 12.0f;
        ay = overlay.Height - 220.0f;
    }

    float x = std::clamp(ax, 2.0f, (std::max)(4.0f, overlay.Width - width - 2.0f));
    float y = std::clamp(ay, 2.0f, (std::max)(4.0f, overlay.Height - blockH - 2.0f));

    static bool s_hitLogDrag = false;
    static ImVec2 s_hitLogGrab(0.0f, 0.0f);
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse(io.MousePos.x, io.MousePos.y);
    bool hover = mouse.x >= x && mouse.x <= x + width && mouse.y >= y && mouse.y <= y + blockH;

    if (!settings.menuOpen && !io.WantCaptureMouse) {
        if (ImGui::IsMouseClicked(0) && hover) {
            s_hitLogDrag = true;
            s_hitLogGrab = ImVec2(mouse.x - x, mouse.y - y);
        }
    }
    if (!ImGui::IsMouseDown(0)) s_hitLogDrag = false;
    if (s_hitLogDrag) {
        x = mouse.x - s_hitLogGrab.x;
        y = mouse.y - s_hitLogGrab.y;
        x = std::clamp(x, 2.0f, (std::max)(4.0f, overlay.Width - width - 2.0f));
        y = std::clamp(y, 2.0f, (std::max)(4.0f, overlay.Height - blockH - 2.0f));
        ax = x;
        ay = y;
    } else {
        ax = x;
        ay = y;
    }

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (int i = n - 1; i >= 0; i--) {
        const auto& e = g_hitLog[i];
        uint64_t age = now - e.time;
        float alpha = 1.0f;
        if (age > 4000) alpha = 1.0f - (age - 4000) / 1000.0f;
        if (alpha < 0) alpha = 0;
        
        float rowY = y + (float)(n - 1 - i) * rowStep;
        sprintf_s(buf, "Hit %s for %d (%d hp left)", e.targetName.c_str(), e.dmg, e.remainingHp);
        
        draw->AddRectFilled(ImVec2(x, rowY), ImVec2(x + width, rowY + rowH), ImColor(0.0f, 0.0f, 0.0f, 0.7f * alpha), 4.0f);
        draw->AddRect(ImVec2(x, rowY), ImVec2(x + width, rowY + rowH), ImColor(UI::accentColor.x, UI::accentColor.y, UI::accentColor.z, alpha), 4.0f);
        draw->AddText(ImVec2(x + padX, rowY + 4.0f), ImColor(1.0f, 1.0f, 1.0f, alpha), buf);
    }
}

void RenderESP() {
    HWND activeHwnd = GetForegroundWindow();
    if (overlay.targetWindow && activeHwnd != overlay.targetWindow && activeHwnd != overlay.overlayWindow) return;

    RenderWatermark();

    if (!cachedInGame) {
        if (settings.menuOpen) gui::Render();
        RenderNotifications();
        return;
    }

    if (cachedLocalPawn && clientBase && settings.grenadeHelper.enabled && settings.grenadeHelper.drawOverlay) {
        ImDrawList* ghDl = ImGui::GetBackgroundDrawList();
        ViewMatrix ghVm = mem.Read<ViewMatrix>(clientBase + dwViewMatrix);
        RenderGrenadeHelperWorld(ghDl, ghVm, overlay.Width, overlay.Height);
    }

    if (!cachedLocalPawn || !settings.visuals.enabled) {
        if (settings.menuOpen) gui::Render();
        return;
    }
    
    gui::Render();
    RenderOverlayRadar();
    RenderGrenadePrediction();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ViewMatrix cVM = mem.Read<ViewMatrix>(clientBase + dwViewMatrix);
    ImColor outCol = ImColor(0, 0, 0, 180);

    if (settings.misc.sniperCrosshair && !settings.menuOpen) {
        uint16_t weaponId = GetWeaponID(cachedLocalPawn);
        bool scoped = mem.Read<bool>(cachedLocalPawn + 0x1C50);
        if (IsSniperWeapon(weaponId) && !scoped) {
            ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
            center.x = std::floor(center.x) + 0.5f;
            center.y = std::floor(center.y) + 0.5f;
            drawList->AddLine(ImVec2(center.x - 5.0f, center.y), ImVec2(center.x + 5.0f, center.y), ImColor(255, 255, 255, 230), 1.0f);
            drawList->AddLine(ImVec2(center.x, center.y - 5.0f), ImVec2(center.x, center.y + 5.0f), ImColor(255, 255, 255, 230), 1.0f);
        }
    }

    // Spread Circle
    if (settings.misc.spreadCircle && !settings.menuOpen) {
        uintptr_t activeWp = GetActiveWeapon(cachedLocalPawn);
        if (activeWp) {
            float accPenalty = mem.Read<float>(activeWp + 0x17D0); // m_fAccuracyPenalty
            if (accPenalty > 0.0f && accPenalty < 1.0f) {
                ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
                float radius = accPenalty * settings.misc.spreadScale;
                if (radius > 4.0f && radius < 800.0f) {
                    ImColor sCol = ImColor(settings.misc.spreadColor[0], settings.misc.spreadColor[1], settings.misc.spreadColor[2], settings.misc.spreadColor[3]);
                    drawList->AddCircle(center, radius, sCol, 64, 1.5f);
                }
            }
        }
    }
    
    if (settings.aim.enabled && settings.aim.showFov && !settings.menuOpen) { 
        WeaponConfig* cfg = GetCurrentWeaponConfig(cachedLocalPawn); 
        float displayFov = cfg->fov;
        if (settings.aimExtra.dynamicFov) {
            bool scoped = mem.Read<bool>(cachedLocalPawn + 0x1C50);
            if (scoped) displayFov = cfg->fov * 0.4f;
        }
        drawList->AddCircle(ImVec2((float)overlay.Width / 2.0f, (float)overlay.Height / 2.0f), displayFov * ((float)overlay.Height / 90.0f), ImGui::GetColorU32(UI::fovColor), 100); 
    }

    std::vector<CachedPlayer> playerSnapshot;
    std::vector<CachedEntity> entitySnapshot;
    std::vector<CachedEntity> grenadeSnapshot;
    BombInfo bombSnapshot;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        playerSnapshot = players;
        entitySnapshot = entities;
        grenadeSnapshot = grenadeProjectiles;
        bombSnapshot = cachedBomb;
    }

    for (const auto& p : playerSnapshot) {
        if (settings.visuals.teamCheck && !p.isEnemy) continue;
        
        Vector2 sF, sH;
        if (!WorldToScreen(p.origin, sF, cVM, overlay.Width, overlay.Height) ||
            !WorldToScreen(p.headPos, sH, cVM, overlay.Width, overlay.Height)) {
            continue;
        }

        float h = std::abs(sF.y - sH.y) * 1.22f;
        float w = h / 2.1f;
        float x = std::floor(sH.x - w * 0.5f);
        float y = std::floor(sH.y - h * 0.08f);

        // sanity: пропускаем только если бокс выродился (NaN/0)
        if (w < 4.0f || h < 8.0f || !std::isfinite(w) || !std::isfinite(h)) {
            continue;
        }
        // если враг впритую/огромный — клампим бокс к экрану, но не пропускаем
        if (w > overlay.Width * 1.5f) w = (float)overlay.Width * 1.5f;
        if (h > overlay.Height * 1.5f) h = (float)overlay.Height * 1.5f;

            ImColor bCol = ImColor(settings.visuals.boxColor[0], settings.visuals.boxColor[1], settings.visuals.boxColor[2], settings.visuals.boxColor[3]);
            ImColor nCol = ImColor(settings.visuals.nameColor[0], settings.visuals.nameColor[1], settings.visuals.nameColor[2], settings.visuals.nameColor[3]);

            RenderOverlayModelGlow(drawList, p, cVM, h);

            // Name
            if (settings.visuals.names && !p.name.empty()) {
                ImGui::PushFont(overlay.ESPFont);
                ImVec2 nSize = ImGui::CalcTextSize(p.name.c_str());
                drawList->AddText(ImVec2(x + w / 2 - nSize.x / 2 + 1, y - nSize.y - 2 + 1), outCol, p.name.c_str());
                drawList->AddText(ImVec2(x + w / 2 - nSize.x / 2, y - nSize.y - 2), nCol, p.name.c_str());
                ImGui::PopFont();
            }

            // Box
            if (settings.visuals.boxes) {
                if (settings.visuals.boxStyle == 2 && DrawDynamicBox(drawList, p, cVM, bCol, outCol)) {
                } else if (settings.visuals.boxStyle == 0) { // Full
                    drawList->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + h + 1), outCol);
                    drawList->AddRect(ImVec2(x + 1, y + 1), ImVec2(x + w - 1, y + h - 1), outCol);
                    drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), bCol);
                } else { // Cornered
                    float l = w / 4.0f;
                    auto DrawCorner = [&](ImVec2 pos, ImVec2 d1, ImVec2 d2, ImColor col, float thickness) {
                        drawList->AddLine(pos, ImVec2(pos.x + d1.x, pos.y + d1.y), col, thickness);
                        drawList->AddLine(pos, ImVec2(pos.x + d2.x, pos.y + d2.y), col, thickness);
                    };
                    // Outlines
                    DrawCorner(ImVec2(x - 1, y - 1), ImVec2(l, 0), ImVec2(0, l), outCol, 3.0f);
                    DrawCorner(ImVec2(x + w + 1, y - 1), ImVec2(-l, 0), ImVec2(0, l), outCol, 3.0f);
                    DrawCorner(ImVec2(x - 1, y + h + 1), ImVec2(l, 0), ImVec2(0, -l), outCol, 3.0f);
                    DrawCorner(ImVec2(x + w + 1, y + h + 1), ImVec2(-l, 0), ImVec2(0, -l), outCol, 3.0f);
                    // Main lines
                    DrawCorner(ImVec2(x, y), ImVec2(l, 0), ImVec2(0, l), bCol, 1.0f);
                    DrawCorner(ImVec2(x + w, y), ImVec2(-l, 0), ImVec2(0, l), bCol, 1.0f);
                    DrawCorner(ImVec2(x, y + h), ImVec2(l, 0), ImVec2(0, -l), bCol, 1.0f);
                    DrawCorner(ImVec2(x + w, y + h), ImVec2(-l, 0), ImVec2(0, -l), bCol, 1.0f);
                }
            }

            // Headshot Indicator (точка над головой если убьет хедшотом)
            if (settings.visuals.headshotIndicator && p.bonesValid) {
                uint16_t lwId = GetWeaponID(cachedLocalPawn);
                float baseDmg = 0.0f;
                switch (lwId) {
                    case 1: baseDmg = 25; break; // Deagle
                    case 2: baseDmg = 24; break; // Elite
                    case 3: baseDmg = 14; break; // Five-SeveN
                    case 4: baseDmg = 26; break; // Glock
                    case 30: baseDmg = 22; break; // Tec9
                    case 32: baseDmg = 35; break; // P2000
                    case 36: baseDmg = 22; break; // P250
                    case 61: baseDmg = 30; break; // USP-S
                    case 63: baseDmg = 25; break; // CZ75
                    case 64: baseDmg = 38; break; // Revolver
                    case 7: baseDmg = 36; break; // AK
                    case 8: baseDmg = 28; break; // AUG
                    case 10: baseDmg = 30; break; // FAMAS
                    case 13: baseDmg = 30; break; // Galil
                    case 16: baseDmg = 33; break; // M4A4
                    case 60: baseDmg = 33; break; // M4A1-S
                    case 39: baseDmg = 30; break; // SG553
                    case 9: baseDmg = 115; break; // AWP
                    case 11: baseDmg = 80; break; // G3SG1
                    case 38: baseDmg = 80; break; // SCAR-20
                    case 40: baseDmg = 88; break; // SSG08
                    case 14: baseDmg = 33; break; // M249
                    case 28: baseDmg = 35; break; // Negev
                    case 17: baseDmg = 30; break; // MAC10
                    case 19: baseDmg = 28; break; // P90
                    case 23: baseDmg = 28; break; // MP5
                    case 24: baseDmg = 26; break; // UMP
                    case 26: baseDmg = 27; break; // Bizon
                    case 33: baseDmg = 26; break; // MP7
                    case 34: baseDmg = 23; break; // MP9
                    case 25: baseDmg = 30; break; // XM1014
                    case 27: baseDmg = 35; break; // Mag7
                    case 29: baseDmg = 26; break; // Sawed-Off
                    case 35: baseDmg = 26; break; // Nova
                    default: baseDmg = 0; break;
                }
                if (baseDmg > 0.0f) {
                    float dist = (p.headPos - cachedLocalEyePos).Length();
                    float falloff = std::exp(-dist * 0.0006f);
                    float dmg = baseDmg * 4.0f * falloff; // head multiplier
                    if (p.hasHelmet) dmg *= 0.5f;
                    
                    Vector2 sHd;
                    if (WorldToScreen(p.headPos, sHd, cVM, overlay.Width, overlay.Height)) {
                        if (dmg >= p.health) {
                            ImColor hsCol = ImColor(settings.visuals.headshotColor[0], settings.visuals.headshotColor[1], settings.visuals.headshotColor[2], settings.visuals.headshotColor[3]);
                            drawList->AddCircleFilled(ImVec2(sHd.x, sHd.y - 12), 4.0f, hsCol);
                            drawList->AddCircle(ImVec2(sHd.x, sHd.y - 12), 5.0f, ImColor(0,0,0,200), 0, 1.5f);
                        }
                    }
                }
            }

            // Skeleton
            if (settings.visuals.skeleton && p.bonesValid) {
                ImColor sCol = ImColor(settings.visuals.skeletonColor[0], settings.visuals.skeletonColor[1], settings.visuals.skeletonColor[2], settings.visuals.skeletonColor[3]);
                for (const auto& c : kBoneConnections) {
                    if (p.bones[c.first].position.IsZero() || p.bones[c.second].position.IsZero()) continue;
                    if ((p.bones[c.first].position - p.bones[c.second].position).Length() > 80.0f) continue;

                    Vector2 v1, v2;
                    if (WorldToScreen(p.bones[c.first].position, v1, cVM, overlay.Width, overlay.Height) &&
                        WorldToScreen(p.bones[c.second].position, v2, cVM, overlay.Width, overlay.Height))
                        drawList->AddLine(ImVec2(v1.x, v1.y), ImVec2(v2.x, v2.y), sCol);
                }
            }

            // Health Bar
            if (settings.visuals.health) {
                float hpFraction = std::clamp(p.health / 100.0f, 0.0f, 1.0f);
                ImColor hpCol = ImColor(settings.visuals.healthColor[0], settings.visuals.healthColor[1], settings.visuals.healthColor[2], settings.visuals.healthColor[3]);
                
                drawList->AddRectFilled(ImVec2(x - 6, y - 1), ImVec2(x - 2, y + h + 1), outCol);
                drawList->AddRectFilled(ImVec2(x - 5, y + h - (h * hpFraction)), ImVec2(x - 3, y + h), hpCol);

                if (settings.visuals.healthText && p.health < 100) {
                    char hpBuf[16]; sprintf_s(hpBuf, "%d", p.health);
                    ImGui::PushFont(overlay.MinecraftFontSmall);
                    ImVec2 hpSize = ImGui::CalcTextSize(hpBuf);
                    drawList->AddText(ImVec2(x - 4 - hpSize.x / 2 + 1, y + h - (h * hpFraction) - hpSize.y + 1), outCol, hpBuf);
                    drawList->AddText(ImVec2(x - 4 - hpSize.x / 2, y + h - (h * hpFraction) - hpSize.y), hpCol, hpBuf);
                    ImGui::PopFont();
                }
            }

            if ((settings.visuals.ammoBar || settings.visuals.ammoText) && p.ammo >= 0 && p.maxAmmo > 0) {
                float ammoFraction = std::clamp(p.ammo / (float)p.maxAmmo, 0.0f, 1.0f);
                float barY = y + h + 3.0f;
                ImColor ammoCol = ImColor(settings.visuals.ammoColor[0], settings.visuals.ammoColor[1], settings.visuals.ammoColor[2], settings.visuals.ammoColor[3]);
                
                if (settings.visuals.ammoBar) {
                    drawList->AddRectFilled(ImVec2(x - 1, barY - 1), ImVec2(x + w + 1, barY + 4), outCol);
                    drawList->AddRectFilled(ImVec2(x, barY), ImVec2(x + w * ammoFraction, barY + 3), ammoCol);
                }
                if (settings.visuals.ammoText) {
                    char ammoBuf[16]; sprintf_s(ammoBuf, "%d", p.ammo);
                    ImGui::PushFont(overlay.MinecraftFontSmall);
                    ImVec2 aSize = ImGui::CalcTextSize(ammoBuf);
                    float textY = settings.visuals.ammoBar ? barY + 5.0f : barY;
                    drawList->AddText(ImVec2(x + w / 2 - aSize.x / 2 + 1, textY + 1), outCol, ammoBuf);
                    drawList->AddText(ImVec2(x + w / 2 - aSize.x / 2, textY), ammoCol, ammoBuf);
                    ImGui::PopFont();
                }
            }

            // Weapon
            if (settings.visuals.activeWeapon && !p.weapon.empty()) {
                float curY = y + h + 2.0f;
                if ((settings.visuals.ammoBar || settings.visuals.ammoText) && p.ammo >= 0 && p.maxAmmo > 0) {
                    curY += settings.visuals.ammoBar ? 6.0f : 0.0f;
                    if (settings.visuals.ammoText) {
                        ImGui::PushFont(overlay.MinecraftFontSmall);
                        curY += ImGui::CalcTextSize("0").y;
                        ImGui::PopFont();
                    }
                }
                if (settings.visuals.iconESP) {
                    const char* icon = GetWeaponIcon(p.weaponId);
                    if (icon && icon[0]) {
                        ImGui::PushFont(overlay.IconFont);
                        ImVec2 iSize = ImGui::CalcTextSize(icon);
                        ImColor iCol = ImColor(settings.visuals.weaponColor[0], settings.visuals.weaponColor[1], settings.visuals.weaponColor[2], settings.visuals.weaponColor[3]);
                        drawList->AddText(ImVec2(x + w / 2 - iSize.x / 2 + 1, curY + 1), outCol, icon);
                        drawList->AddText(ImVec2(x + w / 2 - iSize.x / 2, curY), iCol, icon);
                        ImGui::PopFont();
                        curY += iSize.y;
                    }
                }
                ImGui::PushFont(overlay.ESPFont);
                ImVec2 wSize = ImGui::CalcTextSize(p.weapon.c_str());
                ImColor wCol = ImColor(settings.visuals.weaponColor[0], settings.visuals.weaponColor[1], settings.visuals.weaponColor[2], settings.visuals.weaponColor[3]);
                drawList->AddText(ImVec2(x + w / 2 - wSize.x / 2 + 1, curY + 1), outCol, p.weapon.c_str());
                drawList->AddText(ImVec2(x + w / 2 - wSize.x / 2, curY), wCol, p.weapon.c_str());
                ImGui::PopFont();
            }

            // Flags
            if (settings.visuals.flags) {
                float flagX = x + w + 4.0f;
                float flagY = y;
                ImGui::PushFont(overlay.MinecraftFontSmall);
                auto AddFlag = [&](const char* text, ImColor col) {
                    drawList->AddText(ImVec2(flagX + 1, flagY + 1), outCol, text);
                    drawList->AddText(ImVec2(flagX, flagY), col, text);
                    flagY += ImGui::GetFontSize();
                };

                if (settings.visuals.moneyFlag) {
                    char mBuf[32]; sprintf_s(mBuf, "$%d", p.money);
                    AddFlag(mBuf, ImColor(120, 200, 80));
                }
                if (settings.visuals.armorFlag && p.armor > 0) {
                    AddFlag(p.hasHelmet ? "HK" : "K", ImColor(255, 255, 255));
                }
                if (settings.visuals.defuseKitESP && p.hasDefuseKit && p.team == 3) {
                    AddFlag("KIT", ImColor(80, 220, 255));
                }
                if (settings.visuals.rankReveal && p.rank > 0) {
                    char rBuf[48];
                    const char* rankNames[] = { "Unranked","Silver I","Silver II","Silver III","Silver IV","SE","SEM","Gold I","Gold II","Gold III","GM","MG I","MG II","MGE","DMG","LE","LEM","SMFC","Global" };
                    int rIdx = std::clamp(p.rank, 0, 18);
                    sprintf_s(rBuf, "%s [%d]", rankNames[rIdx], p.wins);
                    AddFlag(rBuf, ImColor(220, 180, 80));
                }
                if (settings.visuals.scopedFlag && p.isScoped) {
                    AddFlag("ZOOM", ImColor(80, 160, 255));
                }
                if (settings.visuals.defusingFlag && p.isDefusing) {
                    AddFlag("DEFUSING", ImColor(255, 40, 40));
                }
                if (settings.visuals.flashedFlag && p.flashRemaining > 0.0f) {
                    float flashPercent = std::clamp(p.flashRemaining / p.flashTotal, 0.0f, 1.0f); 
                    
                    char fBuf[32]; sprintf_s(fBuf, "FLASHED %.1fs", p.flashRemaining);
                    AddFlag(fBuf, ImColor(255, 255, 0));
                    
                    float barW = 30.0f;
                    float barH = 4.0f;
                    drawList->AddRectFilled(ImVec2(flagX, flagY), ImVec2(flagX + barW, flagY + barH), outCol);
                    drawList->AddRectFilled(ImVec2(flagX + 1, flagY + 1), ImVec2(flagX + 1 + (barW - 2) * flashPercent, flagY + barH - 1), ImColor(255, 255, 0));
                    flagY += barH + 2.0f;
                }
                if (settings.visuals.pingFlag) {
                    char pBuf[32]; sprintf_s(pBuf, "%dms", p.ping);
                    ImColor pCol = (p.ping < 50) ? ImColor(100, 255, 100) : (p.ping < 100) ? ImColor(255, 255, 100) : ImColor(255, 100, 100);
                    AddFlag(pBuf, pCol);
                }
                ImGui::PopFont();
            }
            LuaPlayerRenderInfo luaInfo;
            luaInfo.x = x;
            luaInfo.y = y;
            luaInfo.w = w;
            luaInfo.h = h;
            luaInfo.health = p.health;
            luaInfo.team = p.team;
            luaInfo.enemy = p.isEnemy;
            luaInfo.name = p.name;
            luaInfo.weapon = p.weapon;
            LuaEngine::RunPlayerEsp(luaInfo);
        }

    // Entities (Dropped weapons, etc)
    for (const auto& ce : entitySnapshot) { 
        Vector2 sP; 
        if (WorldToScreen(ce.origin, sP, cVM, overlay.Width, overlay.Height)) { 
            float curY = sP.y;
            if (settings.visuals.iconESP) {
                const char* icon = GetWeaponIcon(ce.weaponId);
                if (icon && icon[0]) {
                    ImGui::PushFont(overlay.IconFont);
                    ImVec2 iS = ImGui::CalcTextSize(icon);
                    drawList->AddText(ImVec2(sP.x - iS.x / 2 + 1, curY + 1), outCol, icon);
                    drawList->AddText(ImVec2(sP.x - iS.x / 2, curY), ce.color, icon);
                    ImGui::PopFont();
                    curY += iS.y;
                }
            }
            ImGui::PushFont(overlay.ESPFont); 
            ImVec2 tS = ImGui::CalcTextSize(ce.name.c_str()); 
            drawList->AddText(ImVec2(sP.x - tS.x / 2 + 1, curY + 1), outCol, ce.name.c_str()); 
            drawList->AddText(ImVec2(sP.x - tS.x / 2, curY), ce.color, ce.name.c_str()); 
            ImGui::PopFont(); 
        } 
    }

    // Grenade projectiles in flight (world ESP)
    if (settings.visuals.grenadeWorldEsp && settings.visuals.enabled) {
        ImFont* grenadeIconFont = overlay.IconFont ? overlay.IconFont : ImGui::GetFont();
        for (const auto& g : grenadeSnapshot) {
            Vector2 sG;
            if (!WorldToScreen(g.origin, sG, cVM, overlay.Width, overlay.Height)) continue;
            float rWorld = GrenadeWorldEffectRadius(g.weaponId);
            float ringA = (std::min)(0.55f, g.color.Value.w * 0.6f);
            ImU32 ringCol = ImGui::GetColorU32(ImVec4(g.color.Value.x, g.color.Value.y, g.color.Value.z, ringA));
            ImU32 ringOutline = ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, ringA * 0.85f));
            DrawWorldCircleXY(drawList, g.origin, rWorld, cVM, overlay.Width, overlay.Height, ringOutline, 2.25f);
            DrawWorldCircleXY(drawList, g.origin, rWorld, cVM, overlay.Width, overlay.Height, ringCol, 1.15f);
            const char* icon = GetWeaponIcon(g.weaponId);
            if (!icon || !icon[0]) icon = "?";
            ImGui::PushFont(grenadeIconFont);
            ImVec2 iS = ImGui::CalcTextSize(icon);
            float iconY = sG.y - iS.y * 0.5f;
            drawList->AddText(ImVec2(sG.x - iS.x * 0.5f + 1.0f, iconY + 1.0f), outCol, icon);
            drawList->AddText(ImVec2(sG.x - iS.x * 0.5f, iconY), g.color, icon);
            ImGui::PopFont();
            ImGui::PushFont(overlay.ESPFont);
            ImVec2 tS = ImGui::CalcTextSize(g.name.c_str());
            float nameY = iconY - tS.y - 3.0f;
            drawList->AddText(ImVec2(sG.x - tS.x / 2 + 1, nameY + 1), outCol, g.name.c_str());
            drawList->AddText(ImVec2(sG.x - tS.x / 2, nameY), g.color, g.name.c_str());
            ImGui::PopFont();
        }
    }
    // Local Player Flashed Indicator
    if (cachedLocalPawn && settings.visuals.flashedFlag) {
        static float lastFlashBangTime = 0.0f;
        static uint64_t flashStartTime = 0;
        
        float flashBangTime = mem.Read<float>(cachedLocalPawn + 0x13EC); // m_flFlashBangTime
        float flashTotalLocal = mem.Read<float>(cachedLocalPawn + 0x1400); // m_flFlashDuration
        
        if (flashBangTime != lastFlashBangTime && flashTotalLocal > 0.0f) {
            lastFlashBangTime = flashBangTime;
            flashStartTime = GetTickCount64();
        }
        
        float remaining = 0.0f;
        if (flashStartTime > 0 && flashTotalLocal > 0.0f) {
            float elapsed = (GetTickCount64() - flashStartTime) / 1000.0f;
            remaining = flashTotalLocal - elapsed;
            if (remaining < 0.0f) {
                remaining = 0.0f;
                flashStartTime = 0;
            }
        }
        float total = (flashTotalLocal > 0.1f) ? flashTotalLocal : 5.0f;
        
        if (remaining > 0.0f) {
            float flashPercent = std::clamp(remaining / total, 0.0f, 1.0f);
            char fBuf[32]; sprintf_s(fBuf, "FLASHED %.1fs", remaining);
            ImGui::PushFont(overlay.ESPFont);
            ImVec2 tSize = ImGui::CalcTextSize(fBuf);
            float cx = overlay.Width / 2.0f;
            float cy = overlay.Height / 2.0f + 100.0f;
            
            drawList->AddText(ImVec2(cx - tSize.x / 2 + 1, cy + 1), ImColor(0, 0, 0, 255), fBuf);
            drawList->AddText(ImVec2(cx - tSize.x / 2, cy), ImColor(255, 255, 0, 255), fBuf);
            
            float barW = 100.0f;
            float barH = 6.0f;
            drawList->AddRectFilled(ImVec2(cx - barW / 2, cy + tSize.y + 4), ImVec2(cx + barW / 2, cy + tSize.y + 4 + barH), ImColor(0, 0, 0, 200));
            drawList->AddRectFilled(ImVec2(cx - barW / 2 + 1, cy + tSize.y + 5), ImVec2(cx - barW / 2 + 1 + (barW - 2) * flashPercent, cy + tSize.y + 3 + barH), ImColor(255, 255, 0, 255));
            
            ImGui::PopFont();
        }
    }

    // Bomb ESP
    if (settings.visuals.bombESP && bombSnapshot.active) {
        ImColor bombCol = ImColor(settings.visuals.bombColor[0], settings.visuals.bombColor[1], settings.visuals.bombColor[2], settings.visuals.bombColor[3]);
        ImColor blackCol = ImColor(0, 0, 0, 255);
        
        Vector2 sB;
        bool onScreen = WorldToScreen(bombSnapshot.origin, sB, cVM, overlay.Width, overlay.Height);
        
        ImGui::PushFont(overlay.ESPFont);
        float boxX = 12.0f;
        float boxY = overlay.Height - 130.0f;
        float boxW = 240.0f;
        float boxH = 92.0f;
        
        drawList->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), ImColor(0, 0, 0, 180), 4.0f);
        drawList->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), bombCol, 4.0f, 0, 1.5f);
        
        char title[64];
        sprintf_s(title, "BOMB PLANTED [SITE %s]", bombSnapshot.site == 0 ? "A" : "B");
        drawList->AddText(ImVec2(boxX + 9, boxY + 7), blackCol, title);
        drawList->AddText(ImVec2(boxX + 8, boxY + 6), bombCol, title);
        
        float blowRem = bombSnapshot.blowRemaining;
        float blowFrac = std::clamp(blowRem / bombSnapshot.totalTime, 0.0f, 1.0f);
        
        char blowBuf[64]; sprintf_s(blowBuf, "Explodes in %.1fs", blowRem);
        drawList->AddText(ImVec2(boxX + 9, boxY + 26), blackCol, blowBuf);
        drawList->AddText(ImVec2(boxX + 8, boxY + 25), ImColor(255, 200, 80, 255), blowBuf);
        
        drawList->AddRectFilled(ImVec2(boxX + 8, boxY + 42), ImVec2(boxX + boxW - 8, boxY + 50), ImColor(40, 40, 40, 255));
        drawList->AddRectFilled(ImVec2(boxX + 8, boxY + 42), ImVec2(boxX + 8 + (boxW - 16) * blowFrac, boxY + 50), bombCol);
        
        if (!bombSnapshot.canDefuse) {
            drawList->AddText(ImVec2(boxX + 9, boxY + 56), blackCol, "CANNOT BE DEFUSED");
            drawList->AddText(ImVec2(boxX + 8, boxY + 55), bombCol, "CANNOT BE DEFUSED");
        } else if (bombSnapshot.defusing) {
            float defRem = bombSnapshot.defuseRemaining;
            char defBuf[64]; sprintf_s(defBuf, "Defuse: %.1fs", defRem);
            ImColor defCol = (defRem < blowRem) ? ImColor(80, 220, 80, 255) : ImColor(255, 60, 60, 255);
            drawList->AddText(ImVec2(boxX + 9, boxY + 56), blackCol, defBuf);
            drawList->AddText(ImVec2(boxX + 8, boxY + 55), defCol, defBuf);
            
            const char* status = (defRem < blowRem) ? "WILL DEFUSE" : "WONT MAKE IT";
            drawList->AddText(ImVec2(boxX + 9, boxY + 71), blackCol, status);
            drawList->AddText(ImVec2(boxX + 8, boxY + 70), defCol, status);
        } else {
            drawList->AddText(ImVec2(boxX + 9, boxY + 56), blackCol, "Not being defused");
            drawList->AddText(ImVec2(boxX + 8, boxY + 55), ImColor(200, 200, 200, 255), "Not being defused");
        }
        ImGui::PopFont();
        
        if (onScreen) {
            drawList->AddCircleFilled(ImVec2(sB.x, sB.y), 5.0f, bombCol);
            drawList->AddCircle(ImVec2(sB.x, sB.y), 6.0f, blackCol, 0, 2.0f);
            char bb[16]; sprintf_s(bb, "C4 %.1fs", blowRem);
            ImGui::PushFont(overlay.ESPFont);
            ImVec2 bbSize = ImGui::CalcTextSize(bb);
            drawList->AddText(ImVec2(sB.x - bbSize.x / 2 + 1, sB.y - bbSize.y - 9 + 1), blackCol, bb);
            drawList->AddText(ImVec2(sB.x - bbSize.x / 2, sB.y - bbSize.y - 9), bombCol, bb);
            ImGui::PopFont();
        }
    }

    // Out of FOV Arrows
    if (settings.visuals.outOfFovArrows) {
        float cx = overlay.Width / 2.0f;
        float cy = overlay.Height / 2.0f;
        float radius = settings.visuals.arrowsRadius;
        float arrSize = settings.visuals.arrowsSize;
        ImColor arrCol = ImColor(settings.visuals.arrowsColor[0], settings.visuals.arrowsColor[1], settings.visuals.arrowsColor[2], settings.visuals.arrowsColor[3]);
        
        Vector3 forward;
        float yaw = cachedViewAngles.y * 3.14159265f / 180.0f;
        float pitch = cachedViewAngles.x * 3.14159265f / 180.0f;
        forward.x = std::cos(pitch) * std::cos(yaw);
        forward.y = std::cos(pitch) * std::sin(yaw);
        forward.z = -std::sin(pitch);
        
        for (const auto& p : playerSnapshot) {
            if (settings.visuals.teamCheck && !p.isEnemy) continue;
            if (p.health <= 0) continue;
            
            Vector3 dir = p.origin - cachedLocalEyePos;
            dir.z = 0.0f;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 0.001f) continue;
            dir.x /= len; dir.y /= len;
            
            Vector3 fwd = forward; fwd.z = 0.0f;
            float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
            if (fl < 0.001f) continue;
            fwd.x /= fl; fwd.y /= fl;
            
            float dot = fwd.x * dir.x + fwd.y * dir.y;
            
            Vector2 sP;
            bool onScreen = WorldToScreen(p.origin, sP, cVM, overlay.Width, overlay.Height);
            if (onScreen && dot > 0.0f) continue;
            
            float angle = std::atan2(dir.x * fwd.y - dir.y * fwd.x, dir.x * fwd.x + dir.y * fwd.y);
            float screenAngle = -angle - 1.5707963f;
            
            float ax = cx + std::cos(screenAngle) * radius;
            float ay = cy + std::sin(screenAngle) * radius;
            
            float tipX = cx + std::cos(screenAngle) * (radius + arrSize);
            float tipY = cy + std::sin(screenAngle) * (radius + arrSize);
            
            float perpAngle = screenAngle + 1.5707963f;
            float halfBase = arrSize * 0.6f;
            float bx1 = ax + std::cos(perpAngle) * halfBase;
            float by1 = ay + std::sin(perpAngle) * halfBase;
            float bx2 = ax - std::cos(perpAngle) * halfBase;
            float by2 = ay - std::sin(perpAngle) * halfBase;
            
            drawList->AddTriangleFilled(ImVec2(tipX, tipY), ImVec2(bx1, by1), ImVec2(bx2, by2), arrCol);
            drawList->AddTriangle(ImVec2(tipX, tipY), ImVec2(bx1, by1), ImVec2(bx2, by2), ImColor(0, 0, 0, 200), 1.5f);
        }
    }

    // Hit Marker
    if (settings.misc.hitMarker && g_lastHitTime > 0) {
        uint64_t elapsed = GetTickCount64() - g_lastHitTime;
        if (elapsed < 350) {
            float alpha = 1.0f - (elapsed / 350.0f);
            float cx = overlay.Width / 2.0f;
            float cy = overlay.Height / 2.0f;
            
            if (settings.misc.customHitMarker && !g_hmShapes.empty()) {
                for (const auto& sh : g_hmShapes) {
                    ImColor c = ImColor(sh.color[0], sh.color[1], sh.color[2], sh.color[3] * alpha);
                    if (sh.type == HMS_LINE) {
                        drawList->AddLine(ImVec2(cx + sh.p1[0], cy + sh.p1[1]), ImVec2(cx + sh.p2[0], cy + sh.p2[1]), c, sh.thickness);
                    } else if (sh.type == HMS_RECT) {
                        ImVec2 a(cx + sh.p1[0], cy + sh.p1[1]);
                        ImVec2 b(cx + sh.p2[0], cy + sh.p2[1]);
                        if (sh.filled) drawList->AddRectFilled(a, b, c);
                        else drawList->AddRect(a, b, c, 0.0f, 0, sh.thickness);
                    } else if (sh.type == HMS_TRIANGLE) {
                        ImVec2 a(cx + sh.p1[0], cy + sh.p1[1]);
                        ImVec2 b(cx + sh.p2[0], cy + sh.p2[1]);
                        ImVec2 d(cx + sh.p3[0], cy + sh.p3[1]);
                        if (sh.filled) drawList->AddTriangleFilled(a, b, d, c);
                        else drawList->AddTriangle(a, b, d, c, sh.thickness);
                    }
                }
            } else {
                ImColor mCol = ImColor(settings.misc.hitMarkerColor[0], settings.misc.hitMarkerColor[1], settings.misc.hitMarkerColor[2], settings.misc.hitMarkerColor[3] * alpha);
                float gap = 4.0f, len = 6.0f, thick = 1.5f;
                drawList->AddLine(ImVec2(cx - gap - len, cy - gap - len), ImVec2(cx - gap, cy - gap), mCol, thick);
                drawList->AddLine(ImVec2(cx + gap, cy - gap), ImVec2(cx + gap + len, cy - gap - len), mCol, thick);
                drawList->AddLine(ImVec2(cx - gap - len, cy + gap + len), ImVec2(cx - gap, cy + gap), mCol, thick);
                drawList->AddLine(ImVec2(cx + gap, cy + gap), ImVec2(cx + gap + len, cy + gap + len), mCol, thick);
            }
        }
    }

    RenderSpectatorList();
    RenderHitLog();
    RenderKeybindsOverlay();
    LuaEngine::RunRender();
    RenderNotifications();
}
