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
#include <filesystem>
#include <fstream>
#include <cstring>
#include <winsock2.h>
#include <iphlpapi.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "../include/Memory.hpp"
#include "../include/Overlay.hpp"
#include "../include/Math.hpp"
#include "../include/Globals.hpp"
#include "gui.hpp"

#include "../offsets/client_dll.hpp"
#include "../offsets/offsets.hpp"

#pragma comment(lib, "iphlpapi.lib")

using namespace cs2_dumper::offsets::client_dll;
using namespace cs2_dumper::offsets::engine2_dll;
using namespace cs2_dumper::schemas::client_dll;

namespace fs = std::filesystem;

Memory mem;
Overlay overlay;
uintptr_t clientBase = 0;
uintptr_t engineBase = 0;
std::mutex cacheMutex;
static bool dummy_autowall = false;
constexpr uintptr_t EntityEntrySize = 112;

// Entity Cache Structure
struct CachedPlayer {
    int index = -1;
    uintptr_t pawn = 0;
    int health = 0;
    int team = 0;
    bool isEnemy = true;
    Vector3 origin;
    Vector3 headPos;
    std::string name;
    std::string weapon;
    uint16_t weaponId = 0;
    int ammo = -1;
    int maxAmmo = -1;
    int money = 0;
    int armor = 0;
    bool hasHelmet = false;
    bool isScoped = false;
    bool isDefusing = false;
    bool hasDefuseKit = false;
    int rank = 0;
    int wins = 0;
    float flashRemaining = 0.0f; // секунды до конца флешки
    float flashTotal = 0.0f;     // общая длительность для прогресс-бара
    float lastFlashBangTime = 0.0f;
    uint64_t flashStartTime = 0;
    int ping = 0;
    bool bonesValid = false;
    BoneData bones[28]; 
};

struct CachedEntity {
    Vector3 origin;
    std::string name;
    ImColor color;
    uint16_t weaponId = 0;
};

struct BombInfo {
    bool active = false;
    bool defusing = false;
    bool canDefuse = true;
    int site = 0;
    float blowRemaining = 0.0f;
    float defuseRemaining = 0.0f;
    float totalTime = 40.0f;
    Vector3 origin;
};

std::vector<CachedPlayer> players;
std::vector<CachedEntity> entities;
std::vector<CachedEntity> grenadeProjectiles;
std::vector<std::string> spectators;
std::vector<Vector3> smokePositions;
BombInfo cachedBomb;
uint64_t g_lastHitTime = 0;
struct HitLogEntry {
    std::string targetName;
    int dmg;
    int remainingHp;
    uint64_t time;
};
std::vector<HitLogEntry> g_hitLog;
std::mutex g_hitLogMutex;
std::vector<std::string> g_hitSounds;
std::string g_hitSoundsDir = "C:\\ZitFem\\sounds";
std::vector<HitMarkerShape> g_hmShapes;
int g_hmSelected = -1;

struct Notification {
    std::string text;
    uint64_t startTime;
    uint32_t duration;
    float color[4];
};
std::vector<Notification> g_notifications;
std::mutex g_notifMutex;

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
ViewMatrix cachedViewMatrix;
Vector3 cachedLocalEyePos;
Vector3 cachedLocalOrigin;
uintptr_t cachedLocalPawn;
int cachedLocalIndex = -1;
Vector3 cachedViewAngles;

// Skeleton connections
const std::pair<int, int> boneConnections[] = {
    // Spine
    {1, 3}, {3, 4}, {4, 23}, {23, 6}, {6, 7},
    // Left arm
    {6, 9}, {9, 10}, {10, 11},
    // Right arm
    {6, 13}, {13, 14}, {14, 15},
    // Left leg
    {1, 17}, {17, 18}, {18, 19},
    // Right leg
    {1, 20}, {20, 21}, {21, 22}
};

Settings settings;
std::vector<GrenadeLineupSpot> g_grenadeLineups;
void SaveGrenadeLineupsFile();

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

static std::string GetEntityDesignerNameLower(uintptr_t entity) {
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

static uint16_t ResolveKnifeWeaponId(uint16_t rawId, const std::string& designerLower) {
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
static uint16_t GrenadeWeaponIdFromTypeIndex(int idx) {
    static const uint16_t kIds[6] = { 44, 43, 45, 46, 47, 48 };
    if (idx >= 0 && idx < 6) return kIds[idx];
    return 0;
}

/** Horizontal radius in world units (XY plane) — approximate effect footprint for world ESP ring. */
static float GrenadeWorldEffectRadius(uint16_t weaponId) {
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

static void DrawWorldCircleXY(ImDrawList* dl, const Vector3& center, float radius,
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

static ImColor GrenadeColorForTypeIndex(int idx) {
    if (idx >= 0 && idx < 6) {
        const float* c = settings.grenadeTypeColors[idx];
        return ImColor(c[0], c[1], c[2], c[3]);
    }
    return ImColor(settings.visuals.grenadeColor[0], settings.visuals.grenadeColor[1], settings.visuals.grenadeColor[2], settings.visuals.grenadeColor[3]);
}

bool IsSniperWeapon(uint16_t id) {
    return id == 9 || id == 40 || id == 38 || id == 11;
}

static bool IsKnifeWeapon(uint16_t id) {
    return id == 42 || id == 50 || id == 51 || id == 59 || (id >= 500 && id <= 530);
}

static bool IsGrenadeWeapon(uint16_t id) {
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

static void RenderGrenadeHelperWorld(ImDrawList* dl, const ViewMatrix& vm, int sw, int sh) {
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

void EntityCacheThread() {
    while (true) {
        if (!clientBase) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        uintptr_t entityList = mem.Read<uintptr_t>(clientBase + dwEntityList);
        if (!entityList) { std::lock_guard<std::mutex> lock(cacheMutex); players.clear(); entities.clear(); grenadeProjectiles.clear(); cachedLocalPawn = 0; cachedLocalIndex = -1; std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        uintptr_t localController = mem.Read<uintptr_t>(clientBase + dwLocalPlayerController);
        if (!localController) { std::lock_guard<std::mutex> lock(cacheMutex); players.clear(); entities.clear(); grenadeProjectiles.clear(); cachedLocalPawn = 0; cachedLocalIndex = -1; std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        uint32_t localPawnHandle = mem.Read<uint32_t>(localController + 0x90C);
        uintptr_t localPawn = GetEntityByHandle(entityList, localPawnHandle);
        if (!localPawn) { std::lock_guard<std::mutex> lock(cacheMutex); players.clear(); entities.clear(); grenadeProjectiles.clear(); cachedLocalPawn = 0; cachedLocalIndex = -1; std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        uintptr_t localScene = mem.Read<uintptr_t>(localPawn + 0x330);
        if (!localScene) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

        Vector3 localOrigin = mem.Read<Vector3>(localScene + 0xC8);
        
        // Find local controller index (for SpottedByMask check)
        if (cachedLocalIndex == -1) {
            for (int i = 1; i <= 64; i++) {
                uintptr_t ctrl = GetEntityByIndex(entityList, i);
                if (ctrl == localController) { cachedLocalIndex = i; break; }
            }
        }

        { std::lock_guard<std::mutex> lock(cacheMutex); cachedLocalPawn = localPawn; cachedLocalOrigin = localOrigin; cachedLocalEyePos = localOrigin + mem.Read<Vector3>(localPawn + 0xE70); cachedViewMatrix = mem.Read<ViewMatrix>(clientBase + dwViewMatrix); cachedViewAngles = mem.Read<Vector3>(clientBase + dwViewAngles); }
        
        static int fC = 0; fC++; std::vector<CachedPlayer> tP; std::vector<CachedEntity> tE; std::vector<CachedEntity> tGrenades; std::vector<std::string> tS; std::vector<Vector3> tSmokes;
        BombInfo tBomb;
        int localTeam = mem.Read<uint8_t>(localPawn + 0x3EB);

        for (int i = 1; i < 2048; ++i) {
            uintptr_t entity = GetEntityByIndex(entityList, i); 
            if (!entity || entity == localPawn) continue;
            
            if (i <= 64) {
                uint32_t handle = mem.Read<uint32_t>(entity + 0x90C); // m_hPlayerPawn
                if (handle == 0 || handle == 0xFFFFFFFF) continue;

                uintptr_t pawn = GetEntityByHandle(entityList, handle); 
                if (!pawn || pawn == localPawn) continue;
                
                int health = mem.Read<int>(pawn + 0x34C);
                int team = mem.Read<uint8_t>(pawn + 0x3EB);
                uintptr_t namePtr = mem.Read<uintptr_t>(entity + 0x860);
                std::string playerName = namePtr ? mem.ReadString(namePtr) : "unknown";
                if (health <= 0) {
                    uintptr_t observerServices = mem.Read<uintptr_t>(pawn + 0x11F8);
                    if (observerServices) {
                        uint32_t observerHandle = mem.Read<uint32_t>(observerServices + 0x4C);
                        uintptr_t observerTarget = GetEntityByHandle(entityList, observerHandle);
                        if (observerTarget == localPawn) tS.push_back(playerName);
                    }
                }
                
                if (health <= 0 || health > 100) continue;

                CachedPlayer p; 
                p.pawn = pawn; 
                p.health = health; 
                p.team = team;
                p.isEnemy = (team != localTeam);
                
                // Read flags
                p.armor = mem.Read<int>(pawn + 0x1C7C); // m_ArmorValue
                p.isScoped = mem.Read<bool>(pawn + 0x1C50); // m_bIsScoped
                p.isDefusing = mem.Read<bool>(pawn + 0x1C52); // m_bIsDefusing
                {
                    static std::map<uintptr_t, std::pair<float, uint64_t>> flashData;
                    float flashBangTime = mem.Read<float>(pawn + 0x13EC); // m_flFlashBangTime
                    float flashTotal = mem.Read<float>(pawn + 0x1400);    // m_flFlashDuration
                    
                    if (flashData.find(pawn) == flashData.end()) {
                        flashData[pawn] = { flashBangTime, 0 };
                    }
                    
                    if (flashBangTime != flashData[pawn].first && flashTotal > 0.0f) {
                        flashData[pawn].first = flashBangTime;
                        flashData[pawn].second = GetTickCount64();
                    }
                    
                    float remaining = 0.0f;
                    if (flashData[pawn].second > 0 && flashTotal > 0.0f) {
                        float elapsed = (GetTickCount64() - flashData[pawn].second) / 1000.0f;
                        remaining = flashTotal - elapsed;
                        if (remaining < 0.0f) {
                            remaining = 0.0f;
                            flashData[pawn].second = 0;
                        }
                    }
                    
                    p.flashRemaining = remaining;
                    p.flashTotal = (flashTotal > 0.1f) ? flashTotal : 5.0f;
                }
                p.ping = mem.Read<int>(entity + 0x828); // m_iPing

                uintptr_t moneyServices = mem.Read<uintptr_t>(entity + 0x808); // m_pInGameMoneyServices
                if (moneyServices) p.money = mem.Read<int>(moneyServices + 0x40); // m_iAccount

                uintptr_t itemServices = mem.Read<uintptr_t>(pawn + 0x11E8); // m_pItemServices
                if (itemServices) p.hasHelmet = mem.Read<bool>(itemServices + 0x49); // m_bHasHelmet
                
                p.hasDefuseKit = mem.Read<bool>(entity + 0x920); // m_bPawnHasDefuser
                p.rank = mem.Read<int>(entity + 0x880); // m_iCompetitiveRanking
                p.wins = mem.Read<int>(entity + 0x884); // m_iCompetitiveWins
                uintptr_t gSN = mem.Read<uintptr_t>(pawn + 0x330); if (!gSN) continue;
                
                if (settings.visuals.radarHack && p.team != localTeam) mem.Write<bool>(pawn + 0x1C38 + 0x8, true);
                
                if (fC % 20 == 0 || p.name.empty())
                    p.name = playerName;

                {
                    uintptr_t activeWeapon = GetActiveWeapon(pawn);
                    uint16_t rawWid = GetWeaponEntityID(activeWeapon);
                    std::string wDesign = GetEntityDesignerNameLower(activeWeapon);
                    p.weaponId = ResolveKnifeWeaponId(rawWid, wDesign);
                    p.weapon = GetWeaponName(p.weaponId);
                    p.ammo = activeWeapon ? mem.Read<int>(activeWeapon + 0x16D8) : -1;
                    p.maxAmmo = GetWeaponMaxAmmo(p.weaponId);
                }
                
                // Skeleton fix
                uintptr_t boneArray = mem.Read<uintptr_t>(gSN + 0x150 + 0x80);
                if (boneArray) {
                    mem.Read(boneArray, p.bones);
                }
                
                if (p.bones[6].position.IsZero()) {
                    uintptr_t altBones = mem.Read<uintptr_t>(gSN + 0x1F0);
                    if (altBones) mem.Read(altBones, p.bones);
                }

                // If still zero, try the origin + manual head offset for at least boxes
                p.origin = mem.Read<Vector3>(gSN + 0xC8); 
                bool validPose = !p.bones[0].position.IsZero() && !p.bones[6].position.IsZero() && (p.bones[6].position - p.bones[0].position).Length() > 25.0f && (p.bones[6].position - p.bones[0].position).Length() < 120.0f;
                if (!validPose) {
                    for (auto& bone : p.bones) bone = {};
                }
                p.bonesValid = validPose;

                Vector3 viewOffset = mem.Read<Vector3>(pawn + 0xE70);
                p.headPos = p.origin + viewOffset;
                if (p.headPos.IsZero() || (p.headPos - p.origin).Length() < 30.0f || (p.headPos - p.origin).Length() > 95.0f) {
                    p.headPos = p.origin;
                    p.headPos.z += 72.0f;
                }
                
                tP.push_back(p);
            } else {
                uintptr_t gSN = mem.Read<uintptr_t>(entity + 0x330); if (!gSN) continue;
                
                uintptr_t nameBase = mem.Read<uintptr_t>(entity + 0x10);
                if (!nameBase) continue;
                uintptr_t namePtr = mem.Read<uintptr_t>(nameBase + 0x20);
                if (!namePtr) continue;

                std::string dN = mem.ReadString(namePtr);
                std::transform(dN.begin(), dN.end(), dN.begin(), ::tolower);

                if (dN.find("planted_c4") != std::string::npos) {
                    if (settings.visuals.bombESP) {
                        bool ticking = mem.Read<bool>(entity + 0x1160); // m_bBombTicking
                        bool exploded = mem.Read<bool>(entity + 0x1195); // m_bHasExploded
                        bool defused = mem.Read<bool>(entity + 0x11B4); // m_bBombDefused
                        if (ticking && !exploded && !defused) {
                            tBomb.active = true;
                            tBomb.site = mem.Read<int>(entity + 0x1164); // m_nBombSite
                            float blowTime = mem.Read<float>(entity + 0x1190); // m_flC4Blow
                            tBomb.canDefuse = !mem.Read<bool>(entity + 0x1194); // m_bCannotBeDefused
                            tBomb.defusing = mem.Read<bool>(entity + 0x119C); // m_bBeingDefused
                            float defuseEndTime = mem.Read<float>(entity + 0x11B0); // m_flDefuseCountDown
                            tBomb.totalTime = mem.Read<float>(entity + 0x1198); // m_flTimerLength
                            if (tBomb.totalTime <= 0.1f) tBomb.totalTime = 40.0f;
                            tBomb.origin = mem.Read<Vector3>(gSN + 0xC8);
                            
                            uintptr_t globalVars = mem.Read<uintptr_t>(clientBase + dwGlobalVars);
                            float curTime = globalVars ? mem.Read<float>(globalVars + 0x2C) : 0.0f;
                            tBomb.blowRemaining = (blowTime > curTime) ? (blowTime - curTime) : 0.0f;
                            tBomb.defuseRemaining = (defuseEndTime > curTime) ? (defuseEndTime - curTime) : 0.0f;
                        }
                    }
                    continue;
                }

                if (!settings.visuals.chickenEsp && !settings.visuals.droppedWeapons && !settings.aim.rifle.checkSmoke && !settings.aim.pistol.checkSmoke && !settings.aim.sniper.checkSmoke && !settings.visuals.grenadeWorldEsp) continue;

                if (settings.visuals.grenadeWorldEsp) {
                    const char* glab = nullptr;
                    int gti = -1;
                    if (dN.find("hegrenade") != std::string::npos) { glab = "HE"; gti = 0; }
                    else if (dN.find("flashbang") != std::string::npos) glab = "Flash";
                    else if (dN.find("smokegrenade") != std::string::npos) glab = "Smoke";
                    else if (dN.find("molotov") != std::string::npos) glab = "Molly";
                    else if (dN.find("decoy") != std::string::npos) glab = "Decoy";
                    else if (dN.find("incendiary") != std::string::npos || dN.find("incgrenade") != std::string::npos) glab = "Inc";
                    else if (dN.find("inferno") != std::string::npos) { glab = "Molly"; gti = 3; }

                    bool grenadeCandidate = dN.find("projectile") != std::string::npos || dN.find("inferno") != std::string::npos;
                    if (glab && grenadeCandidate) {
                        Vector3 org = mem.Read<Vector3>(gSN + 0xC8);
                        if (org.IsZero()) org = mem.Read<Vector3>(gSN + 0x80);
                        if (!org.IsZero()) {
                            CachedEntity ge;
                            ge.origin = org;
                            ge.name = glab;
                            if (gti < 0) gti = GrenadeTypeIndexFromWorldLabel(glab);
                            ge.weaponId = GrenadeWeaponIdFromTypeIndex(gti);
                            ge.color = GrenadeColorForTypeIndex(gti);
                            tGrenades.push_back(ge);
                        }
                    }
                }

                bool smokeVolumeForAim =
                    dN.find("smokegrenade_projectile") != std::string::npos
                    || (dN.find("smoke") != std::string::npos && dN.find("weapon_") == std::string::npos);
                if (smokeVolumeForAim) {
                    Vector3 smokeOrigin = mem.Read<Vector3>(gSN + 0xC8);
                    if (!smokeOrigin.IsZero()) tSmokes.push_back(smokeOrigin);
                }
                else if (settings.visuals.chickenEsp && dN.find("chicken") != std::string::npos) { 
                    CachedEntity ce; ce.origin = mem.Read<Vector3>(gSN + 0x80); ce.name = "Chicken"; 
                    ce.color = ImColor(settings.visuals.chickenColor[0], settings.visuals.chickenColor[1], settings.visuals.chickenColor[2], settings.visuals.chickenColor[3]); 
                    tE.push_back(ce); 
                }
                else if (settings.visuals.droppedWeapons && dN.find("weapon_") != std::string::npos) { 
                    uint32_t owner = mem.Read<uint32_t>(entity + 0x520); // m_hOwnerEntity
                    if ((owner & 0x7FF) == 0 || owner == 0xFFFFFFFF) {
                        CachedEntity ce; ce.origin = mem.Read<Vector3>(gSN + 0x80); 
                        if (ce.origin.IsZero()) ce.origin = mem.Read<Vector3>(gSN + 0xC8);
                        ce.weaponId = ResolveKnifeWeaponId(GetWeaponEntityID(entity), dN);
                        ce.name = GetWeaponName(ce.weaponId);
                        ce.color = ImColor(settings.visuals.droppedWeaponColor[0], settings.visuals.droppedWeaponColor[1], settings.visuals.droppedWeaponColor[2], settings.visuals.droppedWeaponColor[3]); 
                        tE.push_back(ce);
                    }
                }
            }
        }
        // Hit Detection - сравниваем health врагов с прошлым кадром
        {
            static std::map<uintptr_t, int> lastHealth;
            for (const auto& np : tP) {
                if (!np.isEnemy) continue;
                auto it = lastHealth.find(np.pawn);
                if (it != lastHealth.end() && np.health < it->second) {
                    g_lastHitTime = GetTickCount64();
                    int dmg = it->second - np.health;
                    {
                        std::lock_guard<std::mutex> lk(g_hitLogMutex);
                        HitLogEntry entry;
                        entry.targetName = np.name;
                        entry.dmg = dmg;
                        entry.remainingHp = np.health;
                        entry.time = GetTickCount64();
                        g_hitLog.push_back(entry);
                        if (g_hitLog.size() > 8) g_hitLog.erase(g_hitLog.begin());
                    }
                    if (np.health > 0 && settings.misc.hitSound && !g_hitSounds.empty() && settings.misc.hitSoundIndex >= 0 && settings.misc.hitSoundIndex < (int)g_hitSounds.size()) {
                        std::string path = g_hitSoundsDir + "\\" + g_hitSounds[settings.misc.hitSoundIndex];
                        PlaySoundA(path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
                    }
                }
                lastHealth[np.pawn] = np.health;
            }
        }
        
        { std::lock_guard<std::mutex> lock(cacheMutex); players = tP; entities = tE; grenadeProjectiles = tGrenades; spectators = tS; smokePositions = tSmokes; cachedBomb = tBomb; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

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
        if (!cachedLocalPawn || !clientBase) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

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

    for (const auto& c : boneConnections) {
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

    std::lock_guard<std::mutex> lock(cacheMutex);
    for (const auto& p : players) {
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
                for (const auto& c : boneConnections) {
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
        }

    // Entities (Dropped weapons, etc)
    for (const auto& ce : entities) { 
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

    // Grenade projectiles in flight (world ESP) — same cacheMutex scope as players/entities above
    if (settings.visuals.grenadeWorldEsp && settings.visuals.enabled) {
        ImFont* grenadeIconFont = overlay.IconFont ? overlay.IconFont : ImGui::GetFont();
        for (const auto& g : grenadeProjectiles) {
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
    if (settings.visuals.bombESP && cachedBomb.active) {
        ImColor bombCol = ImColor(settings.visuals.bombColor[0], settings.visuals.bombColor[1], settings.visuals.bombColor[2], settings.visuals.bombColor[3]);
        ImColor blackCol = ImColor(0, 0, 0, 255);
        
        Vector2 sB;
        bool onScreen = WorldToScreen(cachedBomb.origin, sB, cVM, overlay.Width, overlay.Height);
        
        ImGui::PushFont(overlay.ESPFont);
        float boxX = 12.0f;
        float boxY = overlay.Height - 130.0f;
        float boxW = 240.0f;
        float boxH = 92.0f;
        
        drawList->AddRectFilled(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), ImColor(0, 0, 0, 180), 4.0f);
        drawList->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), bombCol, 4.0f, 0, 1.5f);
        
        char title[64];
        sprintf_s(title, "BOMB PLANTED [SITE %s]", cachedBomb.site == 0 ? "A" : "B");
        drawList->AddText(ImVec2(boxX + 9, boxY + 7), blackCol, title);
        drawList->AddText(ImVec2(boxX + 8, boxY + 6), bombCol, title);
        
        float blowRem = cachedBomb.blowRemaining;
        float blowFrac = std::clamp(blowRem / cachedBomb.totalTime, 0.0f, 1.0f);
        
        char blowBuf[64]; sprintf_s(blowBuf, "Explodes in %.1fs", blowRem);
        drawList->AddText(ImVec2(boxX + 9, boxY + 26), blackCol, blowBuf);
        drawList->AddText(ImVec2(boxX + 8, boxY + 25), ImColor(255, 200, 80, 255), blowBuf);
        
        drawList->AddRectFilled(ImVec2(boxX + 8, boxY + 42), ImVec2(boxX + boxW - 8, boxY + 50), ImColor(40, 40, 40, 255));
        drawList->AddRectFilled(ImVec2(boxX + 8, boxY + 42), ImVec2(boxX + 8 + (boxW - 16) * blowFrac, boxY + 50), bombCol);
        
        if (!cachedBomb.canDefuse) {
            drawList->AddText(ImVec2(boxX + 9, boxY + 56), blackCol, "CANNOT BE DEFUSED");
            drawList->AddText(ImVec2(boxX + 8, boxY + 55), bombCol, "CANNOT BE DEFUSED");
        } else if (cachedBomb.defusing) {
            float defRem = cachedBomb.defuseRemaining;
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
        
        for (const auto& p : players) {
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
    RenderNotifications();
}

int main(int argc, char* argv[]) {
    /* 
    // Robust launcher key check (Temporarily disabled for testing)
    std::string cmdLine = GetCommandLineA();
    if (cmdLine.find("-zitrox_launcher_key") == std::string::npos) {
        MessageBoxA(NULL, "Please launch the software via Zitrox Loader.", "Access Denied", MB_OK | MB_ICONERROR);
        return 0;
    }
    */

    SetConsoleTitleA("Zitrox 1.0.1"); 
RefreshHitSounds();
CreateDirectoryA("C:\\ZitFem", NULL);
LoadGrenadeLineupsFile();
while (!mem.Attach("cs2.exe")) std::this_thread::sleep_for(std::chrono::milliseconds(500));
clientBase = mem.GetModuleAddress("client.dll"); while (!clientBase) { clientBase = mem.GetModuleAddress("client.dll"); std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
engineBase = mem.GetModuleAddress("engine2.dll");
std::thread cacheThread(EntityCacheThread); std::thread logicThread(LogicThread); std::thread autoAcceptThread(AutoAcceptThread); cacheThread.detach(); logicThread.detach(); autoAcceptThread.detach();
CreateThread(nullptr, 0, MouseHookThread, nullptr, 0, nullptr);
if (!overlay.Setup("SDL_app", "Counter-Strike 2")) return 1; overlay.ToggleTransparency(!settings.menuOpen);
overlay.RenderCallback = RenderESP;
while (!settings.requestUnload) overlay.Render();

// Cleanup при выгрузке
if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
overlay.Cleanup();
ExitProcess(0);
return 0;
}
