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

int GetWeaponMaxAmmo(uint16_t id) {
    switch (id) {
        case 1: return 7; case 2: return 30; case 3: return 20; case 4: return 20; case 7: return 30;
        case 8: return 30; case 9: return 5; case 10: return 25; case 11: return 20; case 13: return 35;
        case 14: return 100; case 16: return 30; case 17: return 30; case 19: return 50; case 23: return 30;
        case 24: return 25; case 25: return 7; case 26: return 64; case 27: return 5; case 28: return 150;
        case 29: return 7; case 30: return 18; case 31: return 1; case 32: return 13; case 33: return 30;
        case 34: return 30; case 35: return 8; case 36: return 13; case 38: return 20; case 39: return 30;
        case 40: return 10; case 60: return 20; case 61: return 12; case 63: return 12; case 64: return 8;
        default: return -1;
    }
}

std::string GetEntityDesignerNameLower(uintptr_t entity) {
    if (!entity) return {};
    uintptr_t nameBase = mem.Read<uintptr_t>(entity + 0x10);
    if (!nameBase) return {};
    uintptr_t namePtr = mem.Read<uintptr_t>(nameBase + 0x20);
    if (!namePtr) return {};
    std::string d = mem.ReadString(namePtr);
    std::transform(d.begin(), d.end(), d.begin(), ::tolower);
    return d;
}

/** Map weapon_* / weapon_bayonet designer name to item definition index (longest match first). */
static uint16_t KnifeDefFromDesignerName(const std::string& d) {
    if (d.empty()) return 0;
    static const std::pair<const char*, uint16_t> kMap[] = {
        {"weapon_knife_m9_bayonet", 508},
        {"weapon_knife_survival_bowie", 514},
        {"weapon_knife_falchion_advanced", 512},
        {"weapon_knife_gypsy_jackknife", 520},
        {"weapon_knife_butterfly", 515},
        {"weapon_knife_widowmaker", 523},
        {"weapon_knife_stiletto", 522},
        {"weapon_knife_skeleton", 524},
        {"weapon_knife_outdoor", 521},
        {"weapon_knife_push", 516},
        {"weapon_knife_tactical", 509},
        {"weapon_knife_karambit", 507},
        {"weapon_knife_gut", 506},
        {"weapon_knife_flip", 505},
        {"weapon_knife_css", 503},
        {"weapon_knife_cord", 517},
        {"weapon_knife_canis", 518},
        {"weapon_knife_ursus", 519},
        {"weapon_knife_kukri", 525},
        {"weapon_bayonet", 500},
        {"weapon_knife_ct", 51},
        {"weapon_knife_t", 50},
        {"weapon_knife", 42},
    };
    for (const auto& e : kMap) {
        if (d.find(e.first) != std::string::npos) return e.second;
    }
    return 0;
}

uint16_t ResolveKnifeWeaponId(uint16_t rawId, const std::string& designerLower) {
    uint16_t fromName = KnifeDefFromDesignerName(designerLower);
    if (fromName != 0) return fromName;
    return rawId;
}

static const char* GetKnifeDisplayName(uint16_t id) {
    switch (id) {
        case 42: return "Knife";
        case 50: return "Knife (T)";
        case 51: return "Knife (CT)";
        case 59: return "Knife";
        case 500: return "Bayonet";
        case 503: return "Classic Knife";
        case 505: return "Flip Knife";
        case 506: return "Gut Knife";
        case 507: return "Karambit";
        case 508: return "M9 Bayonet";
        case 509: return "Huntsman Knife";
        case 512: return "Falchion Knife";
        case 514: return "Bowie Knife";
        case 515: return "Butterfly Knife";
        case 516: return "Shadow Daggers";
        case 517: return "Paracord Knife";
        case 518: return "Survival Knife";
        case 519: return "Ursus Knife";
        case 520: return "Navaja Knife";
        case 521: return "Nomad Knife";
        case 522: return "Stiletto Knife";
        case 523: return "Talon Knife";
        case 524: return "Skeleton Knife";
        case 525: return "Kukri Knife";
        default:
            if (id >= 500 && id <= 530) return "Knife";
            return nullptr;
    }
}

const char* GetWeaponIcon(uint16_t id) {
    switch (id) {
        case 1: return "A"; // deagle
        case 2: return "B"; // elite
        case 3: return "C"; // fiveseven
        case 4: return "D"; // glock
        case 7: return "W"; // ak47
        case 8: return "U"; // aug
        case 9: return "Z"; // awp
        case 10: return "R"; // famas
        case 11: return "X"; // g3sg1
        case 13: return "Q"; // galilar
        case 14: return "g"; // m249
        case 16: return "S"; // m4a4
        case 17: return "K"; // mac10
        case 19: return "O"; // p90
        case 23: return "N"; // mp5sd
        case 24: return "L"; // ump45
        case 25: return "b"; // xm1014
        case 26: return "M"; // bizon
        case 27: return "d"; // mag7
        case 28: return "f"; // negev
        case 29: return "c"; // sawedoff
        case 30: return "H"; // tec9
        case 31: return "h"; // taser (zeus)
        case 32: return "E"; // hkp2000
        case 33: return "N"; // mp7
        case 34: return "S"; // mp9
        case 35: return "e"; // nova
        case 36: return "F"; // p250
        case 38: return "Y"; // scar20
        case 39: return "V"; // sg556
        case 40: return "a"; // ssg08
        case 42: case 59: return "]"; // knife
        case 43: return "i"; // flashbang
        case 44: return "j"; // hegrenade
        case 45: return "k"; // smokegrenade
        case 46: return "l"; // molotov
        case 47: return "m"; // decoy
        case 48: return "n"; // incgrenade
        case 49: return "o"; // c4
        case 50: return "p"; // knife_t
        case 51: return "q"; // knife_ct
        case 52: return "r"; // mp5sd
        case 53: return "s"; // ump45
        case 54: return "t"; // xm1014
        case 55: return "u"; // bizon
        case 56: return "v"; // mag7
        case 57: return "w"; // negev
        case 58: return "x"; // sawedoff
        case 60: return "T"; // m4a1_silencer
        case 61: return "G"; // usp_silencer
        case 63: return "I"; // cz75a
        case 64: return "J"; // revolver
        default: if (id >= 500 && id <= 530) return "]"; // cosmetic knives (same glyph in icon font)
                 return "W";
    }
}

const char* GetKeyName(int vk) {
    static char buf[32];
    switch (vk) {
        case VK_LBUTTON: return "LMB"; case VK_RBUTTON: return "RMB"; case VK_MBUTTON: return "MMB";
        case VK_XBUTTON1: return "Mouse 4"; case VK_XBUTTON2: return "Mouse 5"; case VK_CONTROL: return "Ctrl";
        case VK_SHIFT: return "Shift"; case VK_MENU: return "Alt"; case VK_SPACE: return "Space";
        case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete"; case VK_HOME: return "Home";
        case VK_END: return "End"; case VK_PRIOR: return "PgUp"; case VK_NEXT: return "PgDn";
        case VK_CAPITAL: return "Caps"; case VK_ESCAPE: return "Esc"; case VK_TAB: return "Tab";
        case 0: return "None";
        default: 
            if (vk >= 'A' && vk <= 'Z') { sprintf_s(buf, "%c", (char)vk); return buf; }
            if (vk >= '0' && vk <= '9') { sprintf_s(buf, "%c", (char)vk); return buf; }
            if (vk >= VK_F1 && vk <= VK_F12) { sprintf_s(buf, "F%d", vk - VK_F1 + 1); return buf; }
            sprintf_s(buf, "ID: %d", vk); return buf;
    }
}

const char* GetWeaponName(uint16_t id) {
    switch (id) {
        case 1: return "Deagle"; case 2: return "Dualies"; case 3: return "Five-Seven"; case 4: return "Glock";
        case 7: return "AK-47"; case 8: return "AUG"; case 9: return "AWP"; case 10: return "Famas";
        case 11: return "G3SG1"; case 13: return "Galil"; case 14: return "M249"; case 16: return "M4A4";
        case 17: return "Mac-10"; case 19: return "P90"; case 23: return "MP5-SD"; case 24: return "UMP-45";
        case 25: return "XM1014"; case 26: return "Bizon"; case 27: return "Mag-7"; case 28: return "Negev";
        case 29: return "Sawed-Off"; case 30: return "Tec-9"; case 31: return "Zeus"; case 32: return "P2000";
        case 33: return "MP7"; case 34: return "MP9"; case 35: return "Nova"; case 36: return "P250";
        case 38: return "SCAR-20"; case 39: return "SG 553"; case 40: return "SSG 08";
        case 42: case 50: case 51: case 59: {
            const char* kn = GetKnifeDisplayName(id);
            return kn ? kn : "Knife";
        }
        case 43: return "Flashbang"; case 44: return "HE Grenade";
        case 45: return "Smoke"; case 46: return "Molotov"; case 47: return "Decoy"; case 48: return "Incendiary";
        case 49: return "C4"; case 60: return "M4A1-S"; case 61: return "USP-S"; case 63: return "CZ75-Auto";
        case 64: return "R8 Revolver";
        default: 
            if (const char* kn = GetKnifeDisplayName(id)) return kn;
            static char buf[32];
            sprintf_s(buf, "Weapon [%d]", id);
            return buf;
    }
}

bool KeyBindUpdate(KeyBind& bind) {
    if (bind.mode == ALWAYS_ON) {
        bind.active = true;
        return true;
    }

    if (bind.key == 0) {
        bind.active = false;
        return false;
    }

    bool pressed = (GetAsyncKeyState(bind.key) & 0x8000);

    if (bind.mode == HOLD) {
        bind.active = pressed;
    } else if (bind.mode == TOGGLE) {
        static std::map<int, bool> lastState;
        if (pressed) {
            if (!lastState[bind.key]) {
                bind.active = !bind.active;
            }
            lastState[bind.key] = true;
        } else {
            lastState[bind.key] = false;
        }
    }

    return bind.active;
}

uintptr_t GetActiveWeapon(uintptr_t pawn) {
    uintptr_t weaponServices = mem.Read<uintptr_t>(pawn + 0x11E0); 
    if (!weaponServices) return 0;
    
    uint32_t weaponHandle = mem.Read<uint32_t>(weaponServices + 0x60);
    if (weaponHandle == 0xFFFFFFFF) return 0;

    uintptr_t entityList = mem.Read<uintptr_t>(clientBase + dwEntityList);
    if (!entityList) return 0;

    uintptr_t listEntry = mem.Read<uintptr_t>(entityList + 0x8 * ((weaponHandle & 0x7FFF) >> 9) + 16);
    if (!listEntry) return 0;

    return mem.Read<uintptr_t>(listEntry + EntityEntrySize * (weaponHandle & 0x1FF));
}

uintptr_t GetEntityByHandle(uintptr_t entityList, uint32_t handle) {
    if (!entityList || !handle || handle == 0xFFFFFFFF) return 0;
    uintptr_t listEntry = mem.Read<uintptr_t>(entityList + 0x8 * ((handle & 0x7FFF) >> 9) + 0x10);
    if (!listEntry) return 0;
    uintptr_t entity = mem.Read<uintptr_t>(listEntry + EntityEntrySize * (handle & 0x1FF));
    return entity > 0x10000 ? entity : 0;
}

uintptr_t GetEntityByIndex(uintptr_t entityList, int index) {
    if (!entityList || index < 0) return 0;
    uintptr_t listEntry = mem.Read<uintptr_t>(entityList + 0x8 * (index >> 9) + 0x10);
    if (!listEntry) return 0;
    uintptr_t entity = mem.Read<uintptr_t>(listEntry + EntityEntrySize * (index & 0x1FF));
    return entity > 0x10000 ? entity : 0;
}

uint16_t GetWeaponEntityID(uintptr_t weapon) {
    if (!weapon) return 0;
    uint16_t id = mem.Read<uint16_t>(weapon + 0x1180 + 0x50 + 0x1BA);
    if (id == 0 || id > 1000) id = mem.Read<uint16_t>(weapon + 0x1BA);
    return id;
}

uint16_t GetWeaponID(uintptr_t pawn) {
    uintptr_t activeWeapon = GetActiveWeapon(pawn);
    if (!activeWeapon) return 0;
    uint16_t raw = GetWeaponEntityID(activeWeapon);
    return ResolveKnifeWeaponId(raw, GetEntityDesignerNameLower(activeWeapon));
}

int GrenadeTypeIndexFromWeaponId(uint16_t weaponId) {
    switch (weaponId) {
        case 44: return 0;  // HE
        case 43: return 1;  // Flash
        case 45: return 2;  // Smoke
        case 46: return 3;  // Molotov
        case 47: return 4;  // Decoy
        case 48: return 5;  // Incendiary
        default: return -1;
    }
}

int GrenadeTypeIndexFromWorldLabel(const char* glab) {
    if (!glab) return -1;
    if (!strcmp(glab, "HE")) return 0;
    if (!strcmp(glab, "Flash")) return 1;
    if (!strcmp(glab, "Smoke")) return 2;
    if (!strcmp(glab, "Molly")) return 3;
    if (!strcmp(glab, "Decoy")) return 4;
    if (!strcmp(glab, "Inc")) return 5;
    return -1;
}

/** Matches `GrenadeTypeIndexFromWeaponId` order: HE, Flash, Smoke, Molotov, Decoy, Inc. */
uint16_t GrenadeWeaponIdFromTypeIndex(int idx) {
    static const uint16_t kIds[6] = { 44, 43, 45, 46, 47, 48 };
    if (idx >= 0 && idx < 6) return kIds[idx];
    return 0;
}

/** Horizontal radius in world units (XY plane) — approximate effect footprint for world ESP ring. */
float GrenadeWorldEffectRadius(uint16_t weaponId) {
    switch (weaponId) {
        case 44: return 340.0f;  // HE
        case 43: return 220.0f;  // Flash
        case 45: return 168.0f;  // Smoke
        case 46: return 145.0f;  // Molotov
        case 47: return 85.0f;   // Decoy
        case 48: return 145.0f;  // Incendiary
        default: return 130.0f;
    }
}

void DrawWorldCircleXY(ImDrawList* dl, const Vector3& center, float radius,
    const ViewMatrix& vm, int scrW, int scrH, ImU32 col, float thickness, int segs = 56) {
    if (radius <= 0.f || !dl) return;
    Vector2 prev{};
    bool hasPrev = false;
    const float twoPi = 6.28318530718f;
    for (int i = 0; i <= segs; i++) {
        float a = (i == segs) ? 0.f : (float)i * (twoPi / (float)segs);
        Vector3 wp(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius, center.z);
        Vector2 sp;
        if (!WorldToScreen(wp, sp, vm, scrW, scrH)) {
            hasPrev = false;
            continue;
        }
        if (hasPrev)
            dl->AddLine(ImVec2(prev.x, prev.y), ImVec2(sp.x, sp.y), col, thickness);
        prev = sp;
        hasPrev = true;
    }
}

ImColor GrenadeColorForTypeIndex(int idx) {
    if (idx >= 0 && idx < 6) {
        const float* c = settings.grenadeTypeColors[idx];
        return ImColor(c[0], c[1], c[2], c[3]);
    }
    return ImColor(settings.visuals.grenadeColor[0], settings.visuals.grenadeColor[1], settings.visuals.grenadeColor[2], settings.visuals.grenadeColor[3]);
}

bool IsSniperWeapon(uint16_t id) {
    return id == 9 || id == 40 || id == 38 || id == 11;
}

bool IsKnifeWeapon(uint16_t id) {
    return id == 42 || id == 50 || id == 51 || id == 59 || (id >= 500 && id <= 530);
}

bool IsGrenadeWeapon(uint16_t id) {
    return id >= 43 && id <= 48;
}

WeaponConfig* GetBaseWeaponConfigForWeaponId(uint16_t id) {
    if (id == 1 || id == 2 || id == 3 || id == 4 || id == 30 || id == 32 || id == 36 || id == 61 || id == 63 || id == 64)
        return &settings.aim.pistol;
    if (IsSniperWeapon(id)) return &settings.aim.sniper;
    return &settings.aim.rifle;
}

WeaponConfig* GetCurrentWeaponConfig(uintptr_t localPawn) {
    uint16_t id = GetWeaponID(localPawn);
    if (id > 0 && id < 65 && settings.aim.useSpecific[id]) {
        WeaponConfig* spec = &settings.aim.specific[id];
        // Слот specific по умолчанию / после старого .fem часто с aimKey.key == 0 → KeyBindUpdate сразу false
        if (spec->aimKey.key == 0) {
            WeaponConfig* base = GetBaseWeaponConfigForWeaponId(id);
            if (base) *spec = *base;
        }
        return spec;
    }
    return GetBaseWeaponConfigForWeaponId(id);
}

Vector3 CalculateAngle(const Vector3& localPos, const Vector3& enemyPos, const Vector3& viewAngles) {
    Vector3 delta = enemyPos - localPos;
    float deltaLen = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    Vector3 angle;
    angle.x = -std::asin(delta.z / deltaLen) * (180.0f / 3.1415926535f);
    angle.y = std::atan2(delta.y, delta.x) * (180.0f / 3.1415926535f);
    angle.z = 0.0f;
    return angle;
}

float Dot2D(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y;
}

bool IsLineNearSmoke(const Vector3& start, const Vector3& end) {
    Vector3 line = end - start;
    float len2 = Dot2D(line, line);
    if (len2 < 1.0f) return false;

    for (const auto& smoke : smokePositions) {
        Vector3 toSmoke = smoke - start;
        float t = std::clamp(Dot2D(toSmoke, line) / len2, 0.0f, 1.0f);
        Vector3 closest = start + line * t;
        if ((closest - smoke).Length2D() < 150.0f && std::abs(closest.z - smoke.z) < 160.0f) return true;
    }
    return false;
}

bool IsLookingAtTarget(const Vector3& viewAngles, const Vector3& from, const Vector3& to, float maxFov) {
    Vector3 angle = CalculateAngle(from, to, viewAngles);
    Vector3 delta = angle - viewAngles;
    while (delta.y > 180.0f) delta.y -= 360.0f;
    while (delta.y < -180.0f) delta.y += 360.0f;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y) <= maxFov;
}
