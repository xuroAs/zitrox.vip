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

// ---------- Grenade helper (lineups per map) ----------
constexpr uint32_t kGhFileMagic = 0x3247485Au;
constexpr uint32_t kGhFileVer = 3;

struct GhSpotOnDiskV1 {
    char name[48];
    float standX, standY, standZ;
    float aimX, aimY, aimZ;
    float pitch, yaw;
    uint32_t mode, mapId;
};

struct GhSpotOnDiskV2 {
    char name[48];
    float standX, standY, standZ;
    float aimX, aimY, aimZ;
    float eyeX, eyeY, eyeZ;
    float pitch, yaw;
    uint32_t mode, mapId;
};

static Vector3 GhForwardFromAngles(const Vector3& ang) {
    const float r = 3.14159265f / 180.f;
    float p = ang.x * r, y = ang.y * r;
    float cp = cosf(p);
    return { cp * cosf(y), cp * sinf(y), -sinf(p) };
}

namespace {
enum GhPhase : int { GH_IDLE = 0, GH_AIM = 1, GH_RUN = 2, GH_JUMP = 3, GH_THROW = 4 };
int s_ghPhase = GH_IDLE;
uint64_t s_ghT0 = 0;
size_t s_ghSpotIdx = 0;
bool s_ghJumpSpaceSent = false;
bool s_ghThrowDown = false;
bool s_ghRunKeyDown = false;

static void GhReleaseRunForward() {
    if (!s_ghRunKeyDown) return;
    HWND hw = FindWindowA("SDL_app", "Counter-Strike 2");
    if (hw) SendMessage(hw, WM_KEYUP, (WPARAM)'W', 0);
    s_ghRunKeyDown = false;
}

static bool s_ghRecBindWasDown = false;
static bool s_ghRecSession = false;
static Vector3 s_ghRecStand{};
static bool s_ghRecThrowCaptured = false;
static Vector3 s_ghRecThrowEye{};
static float s_ghRecThrowPitch = 0.f, s_ghRecThrowYaw = 0.f;
static Vector3 s_ghRecThrowAim{};
static uint64_t s_ghRecThrowMs = 0;
static uint64_t s_ghRecLastWDownMs = 0;
static uint64_t s_ghRecLastSpaceDownMs = 0;
static uint8_t s_ghRecGrenadeType = 255;
static bool s_ghRecPrevLmb = false, s_ghRecPrevW = false, s_ghRecPrevSpace = false;

static void GrenadeHelperFinishRecordingSession() {
    if (!s_ghRecThrowCaptured) {
        PushNotification("No throw: hold record, LMB throw, release", 1.0f, 0.5f, 0.35f);
        return;
    }
    GrenadeLineupSpot s{};
    strncpy_s(s.name, settings.grenadeHelper.newLineupName, _TRUNCATE);
    s.standX = s_ghRecStand.x;
    s.standY = s_ghRecStand.y;
    s.standZ = s_ghRecStand.z;
    s.eyeX = s_ghRecThrowEye.x;
    s.eyeY = s_ghRecThrowEye.y;
    s.eyeZ = s_ghRecThrowEye.z;
    s.aimX = s_ghRecThrowAim.x;
    s.aimY = s_ghRecThrowAim.y;
    s.aimZ = s_ghRecThrowAim.z;
    s.pitch = s_ghRecThrowPitch;
    s.yaw = s_ghRecThrowYaw;
    s.mode = (uint32_t)settings.grenadeHelper.recordMode;
    s.mapId = (uint32_t)settings.grenadeHelper.mapIndex;
    s.grenadeType = s_ghRecGrenadeType;
    int rmode = settings.grenadeHelper.recordMode;
    if (rmode == 3 && s_ghRecLastWDownMs > 0 && s_ghRecThrowMs >= s_ghRecLastWDownMs) {
        int d = (int)(s_ghRecThrowMs - s_ghRecLastWDownMs);
        s.runThrowPreMs = (uint16_t)std::clamp(d, 40, 2000);
    }
    if ((rmode == 1 || rmode == 2) && s_ghRecLastSpaceDownMs > 0 && s_ghRecThrowMs >= s_ghRecLastSpaceDownMs) {
        int d = (int)(s_ghRecThrowMs - s_ghRecLastSpaceDownMs);
        s.jumpThrowDelayMs = (uint16_t)std::clamp(d, 40, 500);
    }
    g_grenadeLineups.push_back(s);
    settings.grenadeHelper.selectedGlobalIndex = (int)g_grenadeLineups.size() - 1;
    SaveGrenadeLineupsFile();
    PushNotification(std::string("Lineup saved: ") + s.name, 0.4f, 0.95f, 0.55f);
}

static void GrenadeHelperPollRecording(uintptr_t localPawn) {
    if (!settings.grenadeHelper.enabled) {
        if (s_ghRecSession) {
            s_ghRecSession = false;
            s_ghRecThrowCaptured = false;
        }
        s_ghRecBindWasDown = false;
        return;
    }
    int rk = settings.grenadeHelper.recordBind.key;
    if (!rk) {
        s_ghRecBindWasDown = false;
        return;
    }
    bool recDown = (GetAsyncKeyState(rk) & 0x8000) != 0;
    if (recDown && !s_ghRecBindWasDown) {
        if (s_ghPhase != GH_IDLE) {
            GhReleaseRunForward();
            s_ghPhase = GH_IDLE;
        }
        if (!localPawn) {
            PushNotification("Cannot record (no pawn)", 1.0f, 0.45f, 0.35f);
        } else {
            s_ghRecSession = true;
            s_ghRecStand = cachedLocalOrigin;
            s_ghRecThrowCaptured = false;
            s_ghRecLastWDownMs = 0;
            s_ghRecLastSpaceDownMs = 0;
            s_ghRecGrenadeType = 255;
            uint64_t t0 = GetTickCount64();
            s_ghRecPrevLmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            s_ghRecPrevW = (GetAsyncKeyState('W') & 0x8000) != 0;
            s_ghRecPrevSpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            if (s_ghRecPrevW) s_ghRecLastWDownMs = t0;
            if (s_ghRecPrevSpace) s_ghRecLastSpaceDownMs = t0;
        }
    } else if (!recDown && s_ghRecBindWasDown) {
        if (s_ghRecSession) {
            GrenadeHelperFinishRecordingSession();
            s_ghRecSession = false;
        }
    }
    s_ghRecBindWasDown = recDown;

    if (!s_ghRecSession || !localPawn) return;

    bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool wk = (GetAsyncKeyState('W') & 0x8000) != 0;
    bool spk = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    uint64_t now = GetTickCount64();

    if (wk && !s_ghRecPrevW) s_ghRecLastWDownMs = now;
    if (spk && !s_ghRecPrevSpace) s_ghRecLastSpaceDownMs = now;

    if (lmb && !s_ghRecPrevLmb && !s_ghRecThrowCaptured) {
        int gt = GrenadeTypeIndexFromWeaponId(GetWeaponID(localPawn));
        if (gt < 0) {
            PushNotification("Equip a grenade, then click", 0.9f, 0.55f, 0.35f);
        } else {
            s_ghRecThrowCaptured = true;
            s_ghRecThrowEye = cachedLocalEyePos;
            s_ghRecThrowPitch = cachedViewAngles.x;
            s_ghRecThrowYaw = cachedViewAngles.y;
            Vector3 fwd = GhForwardFromAngles(cachedViewAngles);
            s_ghRecThrowAim = cachedLocalEyePos + fwd * 8192.f;
            s_ghRecThrowMs = now;
            s_ghRecGrenadeType = (uint8_t)gt;
        }
    }
    s_ghRecPrevLmb = lmb;
    s_ghRecPrevW = wk;
    s_ghRecPrevSpace = spk;
}
} // namespace

bool GhKeyEdge(int vk) {
    if (!vk) return false;
    static std::unordered_map<int, bool> s_wasDown;
    bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = d && !s_wasDown[vk];
    s_wasDown[vk] = d;
    return edge;
}

void LoadGrenadeLineupsFile() {
    FILE* f = nullptr;
    fopen_s(&f, "C:\\ZitFem\\grenade_lineups.zgh", "rb");
    if (!f) return;
    uint32_t magic = 0, ver = 0, count = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != kGhFileMagic) {
        fclose(f);
        return;
    }
    fread(&ver, 4, 1, f);
    fread(&count, 4, 1, f);
    if (count > 4096u) {
        fclose(f);
        return;
    }
    g_grenadeLineups.clear();
    g_grenadeLineups.reserve(count);
    if (ver >= kGhFileVer) {
        g_grenadeLineups.resize(count);
        if (count) fread(g_grenadeLineups.data(), sizeof(GrenadeLineupSpot), count, f);
    } else if (ver >= 2) {
        for (uint32_t i = 0; i < count; i++) {
            GhSpotOnDiskV2 o{};
            if (fread(&o, sizeof(o), 1, f) != 1) break;
            GrenadeLineupSpot s{};
            memcpy(s.name, o.name, sizeof(s.name));
            s.standX = o.standX;
            s.standY = o.standY;
            s.standZ = o.standZ;
            s.aimX = o.aimX;
            s.aimY = o.aimY;
            s.aimZ = o.aimZ;
            s.eyeX = o.eyeX;
            s.eyeY = o.eyeY;
            s.eyeZ = o.eyeZ;
            s.pitch = o.pitch;
            s.yaw = o.yaw;
            s.mode = o.mode;
            s.mapId = o.mapId;
            s.grenadeType = 255;
            s.runThrowPreMs = 0;
            s.jumpThrowDelayMs = 0;
            g_grenadeLineups.push_back(s);
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            GhSpotOnDiskV1 o{};
            if (fread(&o, sizeof(o), 1, f) != 1) break;
            GrenadeLineupSpot s{};
            memcpy(s.name, o.name, sizeof(s.name));
            s.standX = o.standX;
            s.standY = o.standY;
            s.standZ = o.standZ;
            s.aimX = o.aimX;
            s.aimY = o.aimY;
            s.aimZ = o.aimZ;
            s.pitch = o.pitch;
            s.yaw = o.yaw;
            s.mode = o.mode;
            s.mapId = o.mapId;
            s.eyeX = o.standX;
            s.eyeY = o.standY;
            s.eyeZ = o.standZ + 64.f;
            s.grenadeType = 255;
            s.runThrowPreMs = 0;
            s.jumpThrowDelayMs = 0;
            g_grenadeLineups.push_back(s);
        }
    }
    fclose(f);
}

void SaveGrenadeLineupsFile() {
    CreateDirectoryA("C:\\ZitFem", NULL);
    FILE* f = nullptr;
    fopen_s(&f, "C:\\ZitFem\\grenade_lineups.zgh", "wb");
    if (!f) return;
    uint32_t magic = kGhFileMagic, ver = kGhFileVer;
    uint32_t count = (uint32_t)g_grenadeLineups.size();
    fwrite(&magic, 4, 1, f);
    fwrite(&ver, 4, 1, f);
    fwrite(&count, 4, 1, f);
    if (count) fwrite(g_grenadeLineups.data(), sizeof(GrenadeLineupSpot), count, f);
    fclose(f);
    PushNotification("Grenade lineups saved", 0.45f, 0.85f, 1.0f);
}

static void GrenadeHelperTryStartPlayback(uintptr_t localPawn) {
    if (s_ghPhase != GH_IDLE) return;
    int idx = settings.grenadeHelper.selectedGlobalIndex;
    if (idx < 0 || (size_t)idx >= g_grenadeLineups.size()) {
        PushNotification("Select a lineup in menu", 1.0f, 0.45f, 0.35f);
        return;
    }
    const auto& sp = g_grenadeLineups[(size_t)idx];
    if (sp.mapId != (uint32_t)settings.grenadeHelper.mapIndex) {
        PushNotification("Lineup is for another map tab", 1.0f, 0.5f, 0.35f);
        return;
    }
    float dx = cachedLocalOrigin.x - sp.standX;
    float dy = cachedLocalOrigin.y - sp.standY;
    if (sqrtf(dx * dx + dy * dy) > settings.grenadeHelper.posTolerance ||
        fabsf(cachedLocalOrigin.z - sp.standZ) > 28.f) {
        PushNotification("Stand on the lineup circle", 1.0f, 0.4f, 0.4f);
        return;
    }
    int gti = GrenadeTypeIndexFromWeaponId(GetWeaponID(localPawn));
    if (gti < 0) {
        PushNotification("Equip a grenade", 1.0f, 0.55f, 0.35f);
        return;
    }
    if (sp.grenadeType < 6u && gti != (int)sp.grenadeType) {
        PushNotification("Wrong grenade for this lineup", 1.0f, 0.55f, 0.35f);
        return;
    }
    s_ghSpotIdx = (size_t)idx;
    s_ghPhase = GH_AIM;
    s_ghJumpSpaceSent = false;
    s_ghThrowDown = false;
    s_ghRunKeyDown = false;
}

static void GrenadeHelperTick(uintptr_t localPawn) {
    if (s_ghPhase == GH_IDLE || !localPawn || !clientBase) return;
    if (GrenadeTypeIndexFromWeaponId(GetWeaponID(localPawn)) < 0) {
        GhReleaseRunForward();
        s_ghPhase = GH_IDLE;
        return;
    }
    auto& sp = g_grenadeLineups[s_ghSpotIdx];
    Vector3 aimPt(sp.aimX, sp.aimY, sp.aimZ);
    HWND hw = FindWindowA("SDL_app", "Counter-Strike 2");

    if (s_ghPhase == GH_AIM) {
        Vector3 cur = mem.Read<Vector3>(clientBase + dwViewAngles);
        Vector3 want = CalculateAngle(cachedLocalEyePos, aimPt, cur);
        float dy = want.y - cur.y;
        while (dy > 180.0f) dy -= 360.0f;
        while (dy < -180.0f) dy += 360.0f;
        float dx = want.x - cur.x;
        cur.x += dx * 0.28f;
        cur.y += dy * 0.28f;
        if (cur.x > 89.0f) cur.x = 89.0f;
        if (cur.x < -89.0f) cur.x = -89.0f;
        mem.Write<Vector3>(clientBase + dwViewAngles, cur);
        if (fabsf(dx) < 1.05f && fabsf(dy) < 1.05f) {
            if (sp.mode == 0u) {
                s_ghPhase = GH_THROW;
                s_ghThrowDown = false;
                s_ghT0 = GetTickCount64();
            } else if (sp.mode == 3u) {
                s_ghPhase = GH_RUN;
                s_ghT0 = GetTickCount64();
                s_ghRunKeyDown = false;
            } else {
                s_ghPhase = GH_JUMP;
                s_ghT0 = GetTickCount64();
                s_ghJumpSpaceSent = false;
            }
        }
        return;
    }

    if (s_ghPhase == GH_RUN) {
        uint64_t dt = GetTickCount64() - s_ghT0;
        if (!s_ghRunKeyDown && hw) {
            SendMessage(hw, WM_KEYDOWN, (WPARAM)'W', 0);
            s_ghRunKeyDown = true;
        }
        uint64_t runMs = sp.runThrowPreMs ? (uint64_t)sp.runThrowPreMs : (uint64_t)settings.grenadeHelper.runThrowPreMs;
        if (dt >= runMs) {
            s_ghPhase = GH_THROW;
            s_ghThrowDown = false;
            s_ghT0 = GetTickCount64();
        }
        return;
    }

    if (s_ghPhase == GH_JUMP) {
        if (sp.mode == 2u) {
            if (hw) SendMessage(hw, WM_KEYDOWN, VK_SPACE, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(22));
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            if (hw) SendMessage(hw, WM_KEYUP, VK_SPACE, 0);
            GhReleaseRunForward();
            s_ghPhase = GH_IDLE;
            return;
        }
        uint64_t dt = GetTickCount64() - s_ghT0;
        if (!s_ghJumpSpaceSent && hw) {
            SendMessage(hw, WM_KEYDOWN, VK_SPACE, 0);
            s_ghJumpSpaceSent = true;
        }
        uint64_t jmpMs = sp.jumpThrowDelayMs ? (uint64_t)sp.jumpThrowDelayMs : (uint64_t)settings.grenadeHelper.jumpThrowDelayMs;
        if (dt >= jmpMs) {
            if (hw) SendMessage(hw, WM_KEYUP, VK_SPACE, 0);
            s_ghPhase = GH_THROW;
            s_ghThrowDown = false;
            s_ghT0 = GetTickCount64();
            s_ghJumpSpaceSent = false;
        }
        return;
    }

    if (s_ghPhase == GH_THROW) {
        if (!s_ghThrowDown) {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            s_ghThrowDown = true;
            s_ghT0 = GetTickCount64();
        } else if (GetTickCount64() - s_ghT0 > 20) {
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            s_ghThrowDown = false;
            s_ghPhase = GH_IDLE;
            GhReleaseRunForward();
        }
    }
}

void RunGrenadeHelper(uintptr_t localPawn) {
    if (!settings.grenadeHelper.enabled || !localPawn || !clientBase) return;
    if (settings.menuOpen) return;
    if (overlay.targetWindow && GetForegroundWindow() != overlay.targetWindow) return;
    GrenadeHelperPollRecording(localPawn);
    int pk = settings.grenadeHelper.playBind.key;
    if (pk && GhKeyEdge(pk)) GrenadeHelperTryStartPlayback(localPawn);
    GrenadeHelperTick(localPawn);
}

static void GhDrawStandFootprint(ImDrawList* dl, const ViewMatrix& vm, int sw, int sh, const Vector3& stand,
    float radiusWorld, ImU32 col, bool threeD) {
    DrawWorldCircleXY(dl, stand, radiusWorld, vm, sw, sh, col, 2.0f, 48);
    if (!threeD) return;
    const float cylH = 72.f;
    const int nRad = 8;
    Vector3 top(stand.x, stand.y, stand.z + cylH);
    DrawWorldCircleXY(dl, top, radiusWorld, vm, sw, sh, col, 2.0f, 48);
    const float twoPi = 6.2831853f;
    for (int i = 0; i < nRad; i++) {
        float a = (i * twoPi) / nRad;
        float cx = cosf(a) * radiusWorld, sy = sinf(a) * radiusWorld;
        Vector3 b(stand.x + cx, stand.y + sy, stand.z);
        Vector3 t(stand.x + cx, stand.y + sy, stand.z + cylH);
        Vector2 sb, st;
        if (WorldToScreen(b, sb, vm, sw, sh) && WorldToScreen(t, st, vm, sw, sh))
            dl->AddLine(ImVec2(sb.x, sb.y), ImVec2(st.x, st.y), col, 1.8f);
    }
}

void RenderGrenadeHelperWorld(ImDrawList* dl, const ViewMatrix& vm, int sw, int sh) {
    if (!settings.grenadeHelper.enabled || !settings.grenadeHelper.drawOverlay) return;
    static const char* kGhGrenadeTags[] = { "HE", "Flash", "Smoke", "Molly", "Decoy", "Inc" };
    ImU32 colDim = IM_COL32(190, 130, 255, 100);
    const float ringR = settings.grenadeHelper.standRingRadius > 4.f ? settings.grenadeHelper.standRingRadius : 40.f;
    const bool circle3d = settings.grenadeHelper.standCircleMode != 0;
    ImFont* ghFont = settings.ESPFont ? settings.ESPFont : ImGui::GetFont();
    for (size_t i = 0; i < g_grenadeLineups.size(); i++) {
        const auto& s = g_grenadeLineups[i];
        if (s.mapId != (uint32_t)settings.grenadeHelper.mapIndex) continue;
        if (s.grenadeType < 6u &&
            !(settings.grenadeHelper.overlayGrenadeMask & (1u << s.grenadeType)))
            continue;
        ImColor colObj = (s.grenadeType < 6u) ? GrenadeColorForTypeIndex((int)s.grenadeType)
                                               : ImColor(190, 130, 255, 220);
        ImU32 col = colObj;
        Vector3 stand(s.standX, s.standY, s.standZ);
        GhDrawStandFootprint(dl, vm, sw, sh, stand, ringR, col, circle3d);
        Vector2 ps;
        bool standOnScreen = WorldToScreen(stand, ps, vm, sw, sh);
        if (standOnScreen) dl->AddCircleFilled(ImVec2(ps.x, ps.y), 3.0f, col);
        if (standOnScreen && s.grenadeType < 6u)
            dl->AddText(ghFont, 15.f, ImVec2(ps.x + 7.f, ps.y - 14.f), col, kGhGrenadeTags[s.grenadeType]);
        Vector3 aimWorld(s.aimX, s.aimY, s.aimZ);
        Vector3 eyeWorld(s.eyeX, s.eyeY, s.eyeZ);
        if (eyeWorld.IsZero()) {
            eyeWorld = Vector3(s.standX, s.standY, s.standZ + 64.f);
        }
        Vector3 toAim = aimWorld - eyeWorld;
        float dist = toAim.Length();
        Vector3 dir;
        if (dist > 1.f) {
            dir = toAim * (1.f / dist);
        } else {
            dir = GhForwardFromAngles({ s.pitch, s.yaw, 0.f });
        }
        float clip = std::clamp(dist * 0.12f, 120.f, 480.f);
        Vector3 visEnd = eyeWorld + dir * clip;
        Vector2 pv, pe;
        bool okE = WorldToScreen(eyeWorld, pe, vm, sw, sh);
        bool okV = WorldToScreen(visEnd, pv, vm, sw, sh);
        if (okE && okV) dl->AddLine(ImVec2(pe.x, pe.y), ImVec2(pv.x, pv.y), colDim, 1.8f);
        if (standOnScreen && okV) dl->AddLine(ImVec2(ps.x, ps.y), ImVec2(pv.x, pv.y), colDim, 1.8f);
        Vector2 pa;
        if (WorldToScreen(aimWorld, pa, vm, sw, sh)) {
            dl->AddRect(ImVec2(pa.x - 6.0f, pa.y - 6.0f), ImVec2(pa.x + 6.0f, pa.y + 6.0f), col, 0.0f, 0, 2.0f);
            dl->AddCircleFilled(ImVec2(pa.x, pa.y), 4.0f, col);
        }
    }
}
