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

void RunRCS(uintptr_t localPawn, Vector3& currentAngles, WeaponConfig* cfg) {
    if (!cfg || !cfg->rcs) return;
    static Vector3 oldPunch = { 0, 0, 0 };
    static float residualX = 0, residualY = 0;
    
    int shotsFired = mem.Read<int>(localPawn + 0x1C64);
    if (shotsFired > 1) {
        uintptr_t aimPunchServices = mem.Read<uintptr_t>(localPawn + 0x1490);
        if (!aimPunchServices) return;

        Vector3 aimPunch = mem.Read<Vector3>(aimPunchServices + 0x50);
        Vector3 delta = (aimPunch - oldPunch) * 2.0f;
        
        // Use a default sensitivity or read from dwSensitivity if possible
        float sensitivity = 1.0f; 
        uintptr_t sensPtr = mem.Read<uintptr_t>(clientBase + dwSensitivity);
        if (sensPtr) sensitivity = mem.Read<float>(sensPtr + dwSensitivity_sensitivity);

        float mouseX = (delta.y / (sensitivity * 0.022f)) * cfg->rcsY;
        float mouseY = -(delta.x / (sensitivity * 0.022f)) * cfg->rcsX;

        float totalX = mouseX + residualX;
        float totalY = mouseY + residualY;

        int finalX = static_cast<int>(totalX);
        int finalY = static_cast<int>(totalY);

        residualX = totalX - finalX;
        residualY = totalY - finalY;

        if (finalX != 0 || finalY != 0) {
            mouse_event(MOUSEEVENTF_MOVE, finalX, finalY, 0, 0);
        }

        oldPunch = aimPunch;
    } else {
        oldPunch = { 0, 0, 0 };
        residualX = 0; residualY = 0;
    }
}

void RunAutoStop(uintptr_t localPawn) {
    Vector3 velocity = mem.Read<Vector3>(localPawn + 0x3FC); // m_vecAbsVelocity
    if (std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y) > 10.0f) {
        if (GetAsyncKeyState('W')) keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
        if (GetAsyncKeyState('S')) keybd_event('S', 0, KEYEVENTF_KEYUP, 0);
        if (GetAsyncKeyState('A')) keybd_event('A', 0, KEYEVENTF_KEYUP, 0);
        if (GetAsyncKeyState('D')) keybd_event('D', 0, KEYEVENTF_KEYUP, 0);
    }
}

void RunAimbot(uintptr_t localPawn, const Vector3& localEyePos, const Vector3& currentAngles, int localIndex) {
    if (!settings.aim.enabled) return;
    if (settings.menuOpen) return;
    if (overlay.targetWindow && GetForegroundWindow() != overlay.targetWindow) return;
    uint16_t currentWeaponId = GetWeaponID(localPawn);
    if (IsKnifeWeapon(currentWeaponId) || IsGrenadeWeapon(currentWeaponId)) return;
    WeaponConfig* cfg = GetCurrentWeaponConfig(localPawn);
    if (!cfg) return;
    
    if (!KeyBindUpdate(cfg->aimKey)) return;
    
    Vector3 vAngles = mem.Read<Vector3>(clientBase + dwViewAngles);
    static Vector2 moveRemainder = { 0.f, 0.f };
    
    Vector3 punch = { 0, 0, 0 };
    // Recoil-aware removed by user request
    
    if (cfg->checkFlash) {
        float flashBangTime = mem.Read<float>(localPawn + 0x13EC);
        float flashTotal = mem.Read<float>(localPawn + 0x1400);
        uintptr_t globalVars = mem.Read<uintptr_t>(clientBase + dwGlobalVars);
        float curTime = globalVars ? mem.Read<float>(globalVars + 0x2C) : 0.0f;
        
        float remaining = 0.0f;
        if (flashBangTime > 0.0f && curTime > 0.0f && flashTotal > 0.0f) {
            if (flashBangTime > curTime) remaining = flashBangTime - curTime;
            else remaining = flashTotal - (curTime - flashBangTime);
        }
        if (remaining > 0.0f) return;
    }
    if (cfg->checkJump && !(mem.Read<uint32_t>(localPawn + 0x3F8) & (1 << 0))) return;

    if (cfg->autoScope) {
        bool isScoped = mem.Read<bool>(localPawn + 0x1C50);
        if (!isScoped) {
            uint16_t wId = GetWeaponID(localPawn);
            if (IsSniperWeapon(wId)) {
                mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    if (cfg->autoStop) RunAutoStop(localPawn);

    float effectiveFov = cfg->fov;
    if (settings.aimExtra.dynamicFov) {
        bool scoped = mem.Read<bool>(localPawn + 0x1C50); // m_bIsScoped
        if (scoped) effectiveFov = cfg->fov * 0.4f;
    }
    float closestFov = effectiveFov; 
    Vector3 bestTarget = {0,0,0};
    bool found = false;
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    for (const auto& p : players) {
        Vector3 targetPos;
        if (cfg->teamCheck && !p.isEnemy) continue;

        if (p.bonesValid) {
            float bestBoneFov = 999.0f;
            std::vector<int> bonesToCheck;

            if (cfg->hitboxes & 1) bonesToCheck.push_back(6); // Head
            if (cfg->hitboxes & 2) bonesToCheck.push_back(5); // Neck
            if (cfg->hitboxes & 4) { bonesToCheck.push_back(4); bonesToCheck.push_back(2); bonesToCheck.push_back(0); } // Chest/Stomach/Pelvis
            if (cfg->hitboxes & 8) { bonesToCheck.push_back(8); bonesToCheck.push_back(9); bonesToCheck.push_back(10); bonesToCheck.push_back(13); bonesToCheck.push_back(14); bonesToCheck.push_back(15); } // Arms
            if (cfg->hitboxes & 16) { bonesToCheck.push_back(17); bonesToCheck.push_back(18); bonesToCheck.push_back(19); bonesToCheck.push_back(20); bonesToCheck.push_back(21); bonesToCheck.push_back(22); } // Legs

            if (bonesToCheck.empty()) bonesToCheck.push_back(6); // Default to head if nothing selected

            for (int b : bonesToCheck) {
                if (p.bones[b].position.IsZero()) continue;
                Vector3 angle = CalculateAngle(localEyePos, p.bones[b].position, vAngles);
                Vector3 delta = angle - vAngles;
                
                while (delta.y > 180.0f) delta.y -= 360.0f;
                while (delta.y < -180.0f) delta.y += 360.0f;
                if (delta.x > 89.0f) delta.x = 89.0f;
                if (delta.x < -89.0f) delta.x = -89.0f;

                float fov = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                if (fov < bestBoneFov) {
                    bestBoneFov = fov;
                    targetPos = p.bones[b].position;
                }
            }
        } else {
            targetPos = p.headPos;
        }

        if (targetPos.IsZero()) targetPos = p.headPos;
        if ((targetPos - p.origin).Length() > 120.0f) targetPos = p.headPos;
        if (targetPos.IsZero()) continue;
        if (cfg->checkSmoke && IsLineNearSmoke(localEyePos, targetPos)) continue;
        
        // Wall Check (через m_bSpottedByMask - проверяет видит ли локальный игрок именно эту цель)
        bool visible = true;
        if (localIndex >= 1 && localIndex <= 64) {
            uint32_t spottedMask = mem.Read<uint32_t>(p.pawn + 0x1C38 + 0xC); // EntitySpottedState_t + m_bSpottedByMask
            int bitIndex = localIndex - 1;
            visible = (spottedMask & (1u << bitIndex)) != 0;
        }
        
        if (!visible) {
            // Невидимый враг: проверяем cначала auto wall (если включён) — он может разрешить
            // стрелять даже при включённом checkWall.
            bool allowedByAutoWall = false;
            if (settings.aimExtra.autoWall) {
                uint16_t wId = GetWeaponID(localPawn);
                float dist = (targetPos - localEyePos).Length();
                // base damage оружия (~ за 1 пулю в тело)
                float baseDmg = 0.0f;
                switch (wId) {
                    case 9:  baseDmg = 115.0f; break; // AWP
                    case 40: baseDmg = 88.0f; break;  // SSG08
                    case 11: baseDmg = 80.0f; break;  // G3SG1
                    case 38: baseDmg = 80.0f; break;  // SCAR-20
                    case 7:  baseDmg = 36.0f; break;  // AK47
                    case 39: baseDmg = 30.0f; break;  // SG553
                    case 16: baseDmg = 33.0f; break;  // M4A4
                    case 60: baseDmg = 33.0f; break;  // M4A1-S
                    case 8:  baseDmg = 28.0f; break;  // AUG
                    case 10: baseDmg = 30.0f; break;  // FAMAS
                    case 13: baseDmg = 30.0f; break;  // Galil
                    case 1:  baseDmg = 53.0f; break;  // Deagle
                    case 64: baseDmg = 38.0f; break;  // Revolver
                    case 14: baseDmg = 32.0f; break;  // M249
                    case 28: baseDmg = 35.0f; break;  // Negev
                    case 19: baseDmg = 26.0f; break;  // P90
                    case 17: baseDmg = 29.0f; break;  // MAC10
                    case 23: baseDmg = 27.0f; break;  // MP5
                    case 24: baseDmg = 35.0f; break;  // UMP
                    case 26: baseDmg = 27.0f; break;  // PP-Bizon
                    case 33: baseDmg = 29.0f; break;  // MP7
                    case 34: baseDmg = 26.0f; break;  // MP9
                    case 32: baseDmg = 35.0f; break;  // P2000
                    case 61: baseDmg = 35.0f; break;  // USP-S
                    case 36: baseDmg = 31.0f; break;  // P250
                    case 30: baseDmg = 33.0f; break;  // Tec-9
                    case 4:  baseDmg = 26.0f; break;  // Glock
                    case 3:  baseDmg = 32.0f; break;  // Five-SeveN
                    case 63: baseDmg = 31.0f; break;  // CZ75
                    case 2:  baseDmg = 36.0f; break;  // Dual Berettas
                    default: baseDmg = 0.0f; break;
                }
                if (baseDmg > 0.0f) {
                    // ослабление за стену (приблизительно 40% от базы — одна стена)
                    float thruWall = baseDmg * 0.40f;
                    // distance falloff (мягкий)
                    thruWall *= std::exp(-dist * 0.0008f);
                    // если у врага брони мало — стрелять выгоднее
                    if (p.armor <= 0) thruWall *= 1.4f;
                    
                    // Разрешаем если: либо нанесём минимальный урон (юзер-настройка),
                    // либо просто убьём врага (если у него HP меньше нашего урона).
                    bool meetsMinDmg = thruWall >= settings.aimExtra.autoWallMinDmg;
                    bool willKill = thruWall >= (float)p.health;
                    if (meetsMinDmg || willKill) allowedByAutoWall = true;
                }
            }
            
            if (!allowedByAutoWall) {
                // Auto Wall не разрешил — фолбек на старый Check Wall
                if (cfg->checkWall) continue;
            }
        }

        Vector3 angle = CalculateAngle(localEyePos, targetPos, vAngles);
        Vector3 delta = angle - vAngles;
        while (delta.y > 180.0f) delta.y -= 360.0f;
        while (delta.y < -180.0f) delta.y += 360.0f;
        if (delta.x > 89.0f) delta.x = 89.0f;
        if (delta.x < -89.0f) delta.x = -89.0f;
        
        float fov = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (fov < closestFov) { 
            closestFov = fov; 
            bestTarget = angle;
            found = true; 
        }
    }

    if (settings.aim.chickenAimbot) {
        for (const auto& e : entities) {
            if (e.name != "Chicken") continue;

            Vector3 targetPos = e.origin;
            targetPos.z += 12.0f;

            Vector3 angle = CalculateAngle(localEyePos, targetPos, vAngles);
            Vector3 delta = angle - vAngles;
            while (delta.y > 180.0f) delta.y -= 360.0f;
            while (delta.y < -180.0f) delta.y += 360.0f;
            if (delta.x > 89.0f) delta.x = 89.0f;
            if (delta.x < -89.0f) delta.x = -89.0f;

            float fov = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (fov < closestFov) {
                closestFov = fov;
                bestTarget = angle;
                found = true;
            }
        }
    }
    
    if (found) {
        if (cfg->silent) {
            mem.Write<Vector3>(clientBase + dwViewAngles, bestTarget);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            mem.Write<Vector3>(clientBase + dwViewAngles, vAngles);
        } else {
            Vector3 delta = bestTarget - vAngles;
            while (delta.y > 180.0f) delta.y -= 360.0f;
            while (delta.y < -180.0f) delta.y += 360.0f;

            float smooth = (cfg->smooth < 1.0f) ? 1.0f : cfg->smooth;
            Vector3 newAngles = {
                vAngles.x + delta.x / smooth,
                vAngles.y + delta.y / smooth,
                0.0f
            };
            if (newAngles.x > 89.0f) newAngles.x = 89.0f;
            if (newAngles.x < -89.0f) newAngles.x = -89.0f;
            while (newAngles.y > 180.0f) newAngles.y -= 360.0f;
            while (newAngles.y < -180.0f) newAngles.y += 360.0f;
            mem.Write<Vector3>(clientBase + dwViewAngles, newAngles);
        }
    } else {
        moveRemainder = { 0.f, 0.f };
    }
}

// Animation helper
std::map<ImGuiID, float> anim_states;
float GetAnim(ImGuiID id, bool target, float speed = 12.0f) {
    float t = target ? 1.0f : 0.0f;
    if (anim_states.find(id) == anim_states.end()) anim_states[id] = t;
    if (std::abs(anim_states[id] - t) > 0.001f) {
        float s = ImGui::GetIO().DeltaTime * speed;
        if (anim_states[id] < t) anim_states[id] = (std::min)(t, anim_states[id] + s);
        else anim_states[id] = (std::max)(t, anim_states[id] - s);
    } else anim_states[id] = t;
    return anim_states[id];
}

void RunNoFlash(uintptr_t localPawn) {
    static bool s_noFlashWasOn = false;
    if (settings.misc.noFlash) {
        mem.Write<float>(localPawn + 0x13FC, 0.0f); // m_flFlashMaxAlpha
        s_noFlashWasOn = true;
    } else if (s_noFlashWasOn) {
        // После отключения восстанавливаем — иначе альфа остаётся 0 и флеш не виден
        mem.Write<float>(localPawn + 0x13FC, 255.0f);
        s_noFlashWasOn = false;
    }
}

void RunBhop(uintptr_t localPawn) {
    if (!settings.misc.bhop) return;
    static HWND gameHwnd = FindWindowA("SDL_app", "Counter-Strike 2");
    if (!gameHwnd) gameHwnd = FindWindowA("SDL_app", "Counter-Strike 2");
    
    bool spaceHeld = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    uint32_t flags = mem.Read<uint32_t>(localPawn + 0x3F8);
    bool onGround = (flags & (1 << 0)) != 0;
    
    // Bhop: пока зажат пробел, прыгаем в момент касания земли
    if (spaceHeld && onGround) {
        SendMessage(gameHwnd, WM_KEYDOWN, VK_SPACE, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        SendMessage(gameHwnd, WM_KEYUP, VK_SPACE, 0);
    }
}

struct GlowCacheState {
    bool enabled = false;
    uint32_t color = 0;
    uint64_t lastWriteMs = 0;
    uint64_t lastSeenMs = 0;
};

static std::unordered_map<uintptr_t, GlowCacheState> g_glowCache;

static uint32_t BuildGlowColor() {
    const float rf = std::clamp(settings.visuals.glowColor[0], 0.0f, 1.0f);
    const float gf = std::clamp(settings.visuals.glowColor[1], 0.0f, 1.0f);
    const float bf = std::clamp(settings.visuals.glowColor[2], 0.0f, 1.0f);
    const float af = std::clamp(settings.visuals.glowColor[3] * settings.visuals.glowIntensity, 0.0f, 1.0f);
    const uint8_t r = static_cast<uint8_t>(rf * 255.f);
    const uint8_t g = static_cast<uint8_t>(gf * 255.f);
    const uint8_t b = static_cast<uint8_t>(bf * 255.f);
    const uint8_t a = static_cast<uint8_t>(af * 255.f);
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8u) |
        (static_cast<uint32_t>(b) << 16u) | (static_cast<uint32_t>(a) << 24u);
}

// Движковый glow вынесен из рендера и кэширует последнее состояние, чтобы не спамить WPM.
static void ApplyEngineGlowToPawn(uintptr_t pawn, bool enable, uint32_t color, uint64_t nowMs) {
    GlowCacheState& state = g_glowCache[pawn];
    state.lastSeenMs = nowMs;

    const bool refresh = nowMs - state.lastWriteMs > 700;
    if (!enable) {
        if (!state.enabled && state.color == 0 && !refresh) return;
        const uintptr_t gp = pawn + C_BaseModelEntity::m_Glow;
        mem.Write<bool>(gp + CGlowProperty::m_bGlowing, false);
        mem.Write<uint32_t>(gp + CGlowProperty::m_glowColorOverride, 0);
        state.enabled = false;
        state.color = 0;
        state.lastWriteMs = nowMs;
        return;
    }

    if (state.enabled && state.color == color && !refresh) return;
    const uintptr_t gp = pawn + C_BaseModelEntity::m_Glow;
    mem.Write<uint32_t>(gp + CGlowProperty::m_glowColorOverride, color);
    mem.Write<int>(gp + CGlowProperty::m_iGlowType, 3);
    mem.Write<bool>(gp + CGlowProperty::m_bFlashing, false);
    mem.Write<bool>(gp + CGlowProperty::m_bEligibleForScreenHighlight, true);
    mem.Write<bool>(gp + CGlowProperty::m_bGlowing, true);
    state.enabled = true;
    state.color = color;
    state.lastWriteMs = nowMs;
}

static void RunGlowESP() {
    static uint64_t lastTickMs = 0;
    const uint64_t nowMs = GetTickCount64();
    const bool glowOn = settings.visuals.enabled && settings.visuals.glow;
    const uint64_t intervalMs = glowOn ? 35 : 120;
    if (nowMs - lastTickMs < intervalMs) return;
    lastTickMs = nowMs;

    std::vector<std::pair<uintptr_t, bool>> glowTargets;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        glowTargets.reserve(players.size());
        for (const auto& p : players) {
            if (!p.pawn) continue;
            const bool want = glowOn && (!settings.visuals.teamCheck || p.isEnemy);
            glowTargets.emplace_back(p.pawn, want);
        }
    }

    const uint32_t color = BuildGlowColor();
    for (const auto& target : glowTargets)
        ApplyEngineGlowToPawn(target.first, target.second, color, nowMs);

    for (auto it = g_glowCache.begin(); it != g_glowCache.end();) {
        if (nowMs - it->second.lastSeenMs <= 1500) {
            ++it;
            continue;
        }
        if (it->second.enabled)
            ApplyEngineGlowToPawn(it->first, false, 0, nowMs);
        if (!it->second.enabled && nowMs - it->second.lastSeenMs > 3000)
            it = g_glowCache.erase(it);
        else
            ++it;
    }
}
