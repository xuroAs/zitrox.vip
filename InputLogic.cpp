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

// Глобальное состояние: пользователь физически держит ЛКМ
volatile bool g_userHoldingLMB = false;
HHOOK g_mouseHook = nullptr;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* info = (MSLLHOOKSTRUCT*)lParam;
        // Игнорируем события от mouse_event/SendInput (LLMHF_INJECTED флаг)
        if (!(info->flags & LLMHF_INJECTED)) {
            if (wParam == WM_LBUTTONDOWN) g_userHoldingLMB = true;
            else if (wParam == WM_LBUTTONUP) g_userHoldingLMB = false;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

DWORD WINAPI MouseHookThread(LPVOID) {
    g_mouseHook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleA(nullptr), 0);
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

// Auto Pistol: отслеживает физическое состояние ЛКМ и спамит клики

void RunAutoPistol() {
    if (!settings.aim.autoPistol) return;
    if (!cachedLocalPawn) return;

    uint16_t wId = GetWeaponID(cachedLocalPawn);
    bool isPistol = (wId == 1 || wId == 2 || wId == 3 || wId == 4 || wId == 30 || wId == 32 || wId == 36 || wId == 61 || wId == 63 || wId == 64);
    if (!isPistol) return;

    static HWND gameHwnd = nullptr;
    if (!gameHwnd) gameHwnd = FindWindowA("SDL_app", "Counter-Strike 2");
    if (!gameHwnd || GetForegroundWindow() != gameHwnd) return;

    // Используем переменную, обновляемую из low-level mouse hook (не сбивается mouse_event)
    if (g_userHoldingLMB) {
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

// estimate damage if shot will go through (used by triggerbot wallbang & autowall)
static float EstimateShotDamage(uintptr_t localPawn, float distance) {
    uint16_t wId = GetWeaponID(localPawn);
    float baseDmg = 0.0f;
    switch (wId) {
        case 9:  baseDmg = 115.0f; break;
        case 40: baseDmg = 88.0f; break;
        case 11: baseDmg = 80.0f; break;
        case 38: baseDmg = 80.0f; break;
        case 7:  baseDmg = 36.0f; break;
        case 39: baseDmg = 30.0f; break;
        case 16: baseDmg = 33.0f; break;
        case 60: baseDmg = 33.0f; break;
        case 8:  baseDmg = 28.0f; break;
        case 10: baseDmg = 30.0f; break;
        case 13: baseDmg = 30.0f; break;
        case 1:  baseDmg = 53.0f; break;
        case 64: baseDmg = 38.0f; break;
        case 14: baseDmg = 32.0f; break;
        case 28: baseDmg = 35.0f; break;
        case 19: baseDmg = 26.0f; break;
        case 17: baseDmg = 29.0f; break;
        case 23: baseDmg = 27.0f; break;
        case 24: baseDmg = 35.0f; break;
        case 26: baseDmg = 27.0f; break;
        case 33: baseDmg = 29.0f; break;
        case 34: baseDmg = 26.0f; break;
        case 32: baseDmg = 35.0f; break;
        case 61: baseDmg = 35.0f; break;
        case 36: baseDmg = 31.0f; break;
        case 30: baseDmg = 33.0f; break;
        case 4:  baseDmg = 26.0f; break;
        case 3:  baseDmg = 32.0f; break;
        case 63: baseDmg = 31.0f; break;
        case 2:  baseDmg = 36.0f; break;
        default: baseDmg = 0.0f; break;
    }
    if (baseDmg <= 0.0f) return 0.0f;
    baseDmg *= 0.40f; // штраф за стену
    baseDmg *= std::exp(-distance * 0.0008f);
    return baseDmg;
}

void RunTriggerbot(uintptr_t localPawn) {
    if (!settings.trigger.enabled || !KeyBindUpdate(settings.trigger.hotkey)) return;
    
    static auto lastTargetTime = std::chrono::steady_clock::now();
    static uintptr_t lastTargetPawn = 0;
    
    int localTeam = mem.Read<uint8_t>(localPawn + 0x3EB);
    Vector3 vAngles = mem.Read<Vector3>(clientBase + dwViewAngles);
    
    // ============ WALLBANG MODE ============
    // Идём по всем игрокам и геометрически проверяем — попадает ли прицел на их кости.
    // Это позволяет триггеру работать ДАЖЕ когда m_iIDEntIndex не установлен (за стеной).
    if (settings.trigger.wallbang) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const auto& p : players) {
            if (p.team == localTeam) continue;
            if (p.health <= 0 || p.health > 100) continue;
            if (!p.bonesValid) continue;
            
            // какие кости разрешены
            std::vector<int> bonesToCheck;
            if (settings.trigger.hbFlags & 1) bonesToCheck.push_back(6);
            if (settings.trigger.hbFlags & 2) bonesToCheck.push_back(5);
            if (settings.trigger.hbFlags & 4) { bonesToCheck.push_back(4); bonesToCheck.push_back(2); bonesToCheck.push_back(0); }
            if (settings.trigger.hbFlags & 8) { for (int b : {8,9,10,13,14,15}) bonesToCheck.push_back(b); }
            if (settings.trigger.hbFlags & 16) { for (int b : {17,18,19,20,21,22}) bonesToCheck.push_back(b); }
            if (bonesToCheck.empty()) {
                // если фильтр выключен — все ключевые
                for (int b : {6,5,4,2,0,9,10,14,15,18,19,21,22}) bonesToCheck.push_back(b);
            }
            
            float bestFov = 999.0f;
            for (int b : bonesToCheck) {
                if (p.bones[b].position.IsZero()) continue;
                Vector3 angle = CalculateAngle(cachedLocalEyePos, p.bones[b].position, vAngles);
                Vector3 delta = angle - vAngles;
                while (delta.y > 180.0f) delta.y -= 360.0f;
                while (delta.y < -180.0f) delta.y += 360.0f;
                float fov = std::sqrt(delta.x*delta.x + delta.y*delta.y);
                if (fov < bestFov) bestFov = fov;
            }
            
            // допуск зависит от дистанции — на близких хитбокс шире
            float dist = (p.origin - cachedLocalEyePos).Length();
            float tolerance = settings.trigger.wallbangTolerance; // обычно 1.0-2.5
            if (dist < 200.0f) tolerance *= 1.5f;
            
            if (bestFov > tolerance) continue;
            
            // проверяем видимость
            bool visible = true;
            if (cachedLocalIndex >= 1 && cachedLocalIndex <= 64) {
                uint32_t spottedMask = mem.Read<uint32_t>(p.pawn + 0x1C38 + 0xC);
                int bitIndex = cachedLocalIndex - 1;
                visible = (spottedMask & (1u << bitIndex)) != 0;
            }
            
            // если враг невидим — нужен auto wall
            if (!visible) {
                if (!settings.aimExtra.autoWall) continue;
                float dmg = EstimateShotDamage(localPawn, dist);
                if (p.armor <= 0) dmg *= 1.4f;
                bool meetsMinDmg = dmg >= settings.aimExtra.autoWallMinDmg;
                bool willKill = dmg >= (float)p.health;
                if (!meetsMinDmg && !willKill) continue;
            }
            
            // нашли цель!
            if (lastTargetPawn != p.pawn) {
                lastTargetTime = std::chrono::steady_clock::now();
                lastTargetPawn = p.pawn;
            }
            
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastTargetTime).count();
            if (elapsed < settings.trigger.delay) return;
            
            int shots = settings.trigger.burstMode ? settings.trigger.burstCount : 1;
            for (int i = 0; i < shots; i++) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                if (i + 1 < shots) std::this_thread::sleep_for(std::chrono::milliseconds(60));
            }
            lastTargetTime = std::chrono::steady_clock::now();
            return;
        }
        lastTargetPawn = 0;
        return;
    }
    
    // ============ CLASSIC MODE (через m_iIDEntIndex) ============
    int crosshairId = mem.Read<int>(localPawn + 0x344C);
    static int lastCrosshairId = 0;

    if (crosshairId > 0 && crosshairId < 2048) {
        if (crosshairId != lastCrosshairId) {
            lastTargetTime = std::chrono::steady_clock::now();
            lastCrosshairId = crosshairId;
        }

        uintptr_t entityList = mem.Read<uintptr_t>(clientBase + dwEntityList);
        uintptr_t entity = GetEntityByIndex(entityList, crosshairId);
        if (entity) {
            int health = mem.Read<int>(entity + 0x34C);
            if (health > 0 && health <= 100) {
                int team = mem.Read<uint8_t>(entity + 0x3EB);
                if (team != localTeam) {
                    bool hitboxOk = true;
                    if (settings.trigger.hitboxFilter && players.size()) {
                        hitboxOk = false;
                        for (const auto& p : players) {
                            if (p.pawn != entity || !p.bonesValid) continue;
                            
                            std::vector<int> bonesToCheck;
                            if (settings.trigger.hbFlags & 1) bonesToCheck.push_back(6);
                            if (settings.trigger.hbFlags & 2) bonesToCheck.push_back(5);
                            if (settings.trigger.hbFlags & 4) { bonesToCheck.push_back(4); bonesToCheck.push_back(2); bonesToCheck.push_back(0); }
                            if (settings.trigger.hbFlags & 8) { for (int b : {8,9,10,13,14,15}) bonesToCheck.push_back(b); }
                            if (settings.trigger.hbFlags & 16) { for (int b : {17,18,19,20,21,22}) bonesToCheck.push_back(b); }
                            if (bonesToCheck.empty()) bonesToCheck.push_back(6);
                            
                            float bestFov = 999.0f;
                            for (int b : bonesToCheck) {
                                if (p.bones[b].position.IsZero()) continue;
                                Vector3 angle = CalculateAngle(cachedLocalEyePos, p.bones[b].position, vAngles);
                                Vector3 delta = angle - vAngles;
                                while (delta.y > 180.0f) delta.y -= 360.0f;
                                while (delta.y < -180.0f) delta.y += 360.0f;
                                float fov = std::sqrt(delta.x*delta.x + delta.y*delta.y);
                                if (fov < bestFov) bestFov = fov;
                            }
                            if (bestFov < 1.5f) hitboxOk = true;
                            break;
                        }
                    }
                    
                    if (!hitboxOk) return;
                    
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastTargetTime).count();
                    if (elapsed >= settings.trigger.delay) {
                        int shots = settings.trigger.burstMode ? settings.trigger.burstCount : 1;
                        for (int i = 0; i < shots; i++) {
                            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                            std::this_thread::sleep_for(std::chrono::milliseconds(15));
                            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                            if (i + 1 < shots) std::this_thread::sleep_for(std::chrono::milliseconds(60));
                        }
                        lastTargetTime = std::chrono::steady_clock::now();
                    }
                }
            }
        }
    } else {
        lastCrosshairId = 0;
    }
}

static bool KnifeEnemyFacingUs(uintptr_t enemyPawn, const Vector3& enemyHead) {
    if (!enemyPawn) return false;
    Vector3 enemyAng = mem.Read<Vector3>(enemyPawn + C_CSPlayerPawn::m_angEyeAngles);
    return IsLookingAtTarget(enemyAng, enemyHead, cachedLocalEyePos, 46.0f);
}

void RunMiscBots(uintptr_t localPawn, const Vector3& localOrigin) {
    if (!settings.misc.zeusBot && !settings.misc.knifeBot) return;
    if (overlay.targetWindow && GetForegroundWindow() != overlay.targetWindow) return;
    int localTeam = mem.Read<uint8_t>(localPawn + 0x3EB);
    Vector3 vAngles = mem.Read<Vector3>(clientBase + dwViewAngles);
    uint16_t wId = GetWeaponID(localPawn);
    static uint64_t lastKnifeComboMs = 0;
    static uintptr_t lastKnifeTargetPawn = 0;

    bool knifeDone = false;
    for (const auto& p : players) {
        if (p.team == localTeam) continue;
        if (p.health <= 0) continue;
        
        // проверяем несколько точек тела — голова, центр, ноги
        Vector3 center = { p.origin.x, p.origin.y, p.origin.z + 36.0f };
        float distHead = (p.headPos - cachedLocalEyePos).Length();
        float distCenter = (center - cachedLocalEyePos).Length();
        float distFeet = (p.origin - cachedLocalEyePos).Length();
        float minDist = distHead;
        if (distCenter < minDist) minDist = distCenter;
        if (distFeet < minDist) minDist = distFeet;
        
        // на ближнем расстоянии (<40 HU) — расширяем угол FOV
        bool veryClose = minDist < 40.0f;
        float zeusFov = veryClose ? 35.0f : 7.5f;
        float knifeFov = veryClose ? 60.0f : 14.0f;
        
        bool zeusLooking = IsLookingAtTarget(vAngles, cachedLocalEyePos, p.headPos, zeusFov) ||
                           IsLookingAtTarget(vAngles, cachedLocalEyePos, center, zeusFov) ||
                           IsLookingAtTarget(vAngles, cachedLocalEyePos, p.origin, zeusFov);
        
        bool knifeLooking = IsLookingAtTarget(vAngles, cachedLocalEyePos, p.headPos, knifeFov) ||
                            IsLookingAtTarget(vAngles, cachedLocalEyePos, center, knifeFov) ||
                            IsLookingAtTarget(vAngles, cachedLocalEyePos, p.origin, knifeFov);
        
        if (settings.misc.zeusBot && wId == 31 && minDist <= 200.0f && zeusLooking) {
            mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        }
        
        if (minDist > 115.0f && p.pawn == lastKnifeTargetPawn)
            lastKnifeTargetPawn = 0;

        if (!knifeDone && settings.misc.knifeBot && IsKnifeWeapon(wId) && knifeLooking && p.pawn) {
            bool facingUs = KnifeEnemyFacingUs(p.pawn, p.headPos);
            float stabDist = facingUs ? 70.0f : 56.0f;
            if (minDist > stabDist) continue;
            if (GetTickCount64() - lastKnifeComboMs < 850) continue;
            if (p.pawn == lastKnifeTargetPawn && GetTickCount64() - lastKnifeComboMs < 1250) continue;

            bool enemyFacingUs = facingUs;
            auto doMouseClick = [](DWORD down, DWORD up) {
                mouse_event(down, 0, 0, 0, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                mouse_event(up, 0, 0, 0, 0);
            };
            if (enemyFacingUs) {
                doMouseClick(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                doMouseClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
            } else {
                doMouseClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
                std::this_thread::sleep_for(std::chrono::milliseconds(140));
                doMouseClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
            }
            lastKnifeComboMs = GetTickCount64();
            lastKnifeTargetPawn = p.pawn;
            knifeDone = true;
        }
    }
}

void AutoAcceptThread() {
    while (true) {
        if (settings.misc.autoAccept && engineBase) {
            uintptr_t netClient = mem.Read<uintptr_t>(engineBase + dwNetworkGameClient);
            int signOnState = netClient ? mem.Read<int>(netClient + dwNetworkGameClient_signOnState) : 0;
            // SignOnState < 6 = не в игре. Если ждем матч, signOnState либо 0, либо 1
            if (signOnState < 2) {
                HWND cs2 = FindWindowA("SDL_app", "Counter-Strike 2");
                if (cs2 && GetForegroundWindow() == cs2) {
                    // нажимаем Y (accept) — но только если "Accept" реально появилось.
                    // упрощенно: спамим раз в 2 секунды кнопку Y
                    SendMessage(cs2, WM_KEYDOWN, 'Y', 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    SendMessage(cs2, WM_KEYUP, 'Y', 0);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

void LogicThread() {
    while (true) {
        // Menu Toggle logic — единственный обработчик клавиши меню
        bool menuWas = settings.misc.menuKey.active;
        KeyBindUpdate(settings.misc.menuKey);
        if (settings.misc.menuKey.key != 0 && settings.misc.menuKey.active != menuWas) {
            settings.menuOpen = settings.misc.menuKey.active;
            overlay.ToggleTransparency(!settings.menuOpen);
        } else if (settings.menuOpen != settings.misc.menuKey.active) {
            // если меню закрыли через крестик — синхронизируем active
            settings.misc.menuKey.active = settings.menuOpen;
        }

        if (!cachedInGame || !cachedLocalPawn || !clientBase) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        // Panic Key logic
        bool panicWas = settings.misc.panicKey.active;
        KeyBindUpdate(settings.misc.panicKey);
        if (settings.misc.panicKey.key != 0 && settings.misc.panicKey.active && !panicWas) {
            settings.aim.enabled = false;
            settings.visuals.enabled = false;
            settings.trigger.enabled = false;
            settings.misc.bhop = false;
            settings.misc.noFlash = false;
            settings.misc.panicKey.active = false; // Reset it immediately
            PushNotification("PANIC! All features disabled", 1.0f, 0.3f, 0.3f, 4000);
        }

        if (!settings.menuOpen) {
            bool aimWas = settings.aim.enabledBind.active;
            KeyBindUpdate(settings.aim.enabledBind);
            if (settings.aim.enabledBind.key != 0 && settings.aim.enabledBind.active != aimWas) settings.aim.enabled = settings.aim.enabledBind.active;

            bool chickenWas = settings.aim.chickenAimbotBind.active;
            KeyBindUpdate(settings.aim.chickenAimbotBind);
            if (settings.aim.chickenAimbotBind.key != 0 && settings.aim.chickenAimbotBind.active != chickenWas) settings.aim.chickenAimbot = settings.aim.chickenAimbotBind.active;
        }
        RunBhop(cachedLocalPawn);
        RunNoFlash(cachedLocalPawn);
        WeaponConfig* currentCfg = GetCurrentWeaponConfig(cachedLocalPawn);
        RunRCS(cachedLocalPawn, cachedViewAngles, currentCfg);
        RunAutoPistol();
        RunAimbot(cachedLocalPawn, cachedLocalEyePos, cachedViewAngles, cachedLocalIndex);
        RunTriggerbot(cachedLocalPawn); RunMiscBots(cachedLocalPawn, cachedLocalOrigin);
        RunGrenadeHelper(cachedLocalPawn);
        LuaEngine::RunTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
