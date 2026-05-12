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
#include <cctype>
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

void ClearEntityCacheState() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    players.clear();
    entities.clear();
    grenadeProjectiles.clear();
    spectators.clear();
    smokePositions.clear();
    cachedBomb = {};
    cachedLocalPawn = 0;
    cachedLocalIndex = -1;
    cachedInGame = false;
}
void EntityCacheThread() {
    while (true) {
        if (!clientBase) { ClearEntityCacheState(); std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
        uintptr_t entityList = mem.Read<uintptr_t>(clientBase + dwEntityList);
        if (!entityList) { ClearEntityCacheState(); std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        uintptr_t localController = mem.Read<uintptr_t>(clientBase + dwLocalPlayerController);
        if (!localController) { ClearEntityCacheState(); std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        uint32_t localPawnHandle = mem.Read<uint32_t>(localController + 0x90C);
        uintptr_t localPawn = GetEntityByHandle(entityList, localPawnHandle);
        if (!localPawn) { ClearEntityCacheState(); std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        uintptr_t localScene = mem.Read<uintptr_t>(localPawn + 0x330);
        if (!localScene) { ClearEntityCacheState(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

        Vector3 localOrigin = mem.Read<Vector3>(localScene + 0xC8);
        
        // Find local controller index (for SpottedByMask check)
        if (cachedLocalIndex == -1) {
            for (int i = 1; i <= 64; i++) {
                uintptr_t ctrl = GetEntityByIndex(entityList, i);
                if (ctrl == localController) { cachedLocalIndex = i; break; }
            }
        }

        int localTeam = mem.Read<uint8_t>(localPawn + 0x3EB);
        bool localInGame = (localTeam == 2 || localTeam == 3);
        if (!localInGame) { ClearEntityCacheState(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        { std::lock_guard<std::mutex> lock(cacheMutex); cachedLocalPawn = localPawn; cachedLocalOrigin = localOrigin; cachedLocalEyePos = localOrigin + mem.Read<Vector3>(localPawn + 0xE70); cachedViewMatrix = mem.Read<ViewMatrix>(clientBase + dwViewMatrix); cachedViewAngles = mem.Read<Vector3>(clientBase + dwViewAngles); cachedInGame = localInGame; }
        {
            static int lastRoundStartCount = -1;
            uintptr_t gameRules = mem.Read<uintptr_t>(clientBase + dwGameRules);
            if (gameRules) {
                int roundStartCount = (int)mem.Read<uint8_t>(gameRules + C_CSGameRules::m_nRoundStartCount);
                if (lastRoundStartCount == -1) {
                    lastRoundStartCount = roundStartCount;
                } else if (roundStartCount != lastRoundStartCount) {
                    int roundsPlayed = mem.Read<int>(gameRules + C_CSGameRules::m_totalRoundsPlayed);
                    LuaEngine::NotifyRoundStart((std::max)(1, roundsPlayed + 1));
                    lastRoundStartCount = roundStartCount;
                }
            }
        }
        
        static int fC = 0;
        static std::unordered_map<uintptr_t, PlayerSlowCache> playerSlowCache;
        fC++;
        bool needWorldEntities =
            settings.visuals.bombESP ||
            settings.visuals.chickenEsp ||
            settings.visuals.droppedWeapons ||
            settings.visuals.grenadeWorldEsp ||
            settings.aim.rifle.checkSmoke ||
            settings.aim.pistol.checkSmoke ||
            settings.aim.sniper.checkSmoke;
        bool scanWorldEntities = needWorldEntities && (fC % 10) == 0;
        std::vector<CachedPlayer> tP; std::vector<CachedEntity> tE; std::vector<CachedEntity> tGrenades; std::vector<std::string> tS; std::vector<Vector3> tSmokes;
        BombInfo tBomb;

        if (!scanWorldEntities) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            tE = entities;
            tGrenades = grenadeProjectiles;
            tSmokes = smokePositions;
            tBomb = cachedBomb;
        }

        int maxEntityIndex = scanWorldEntities ? 2048 : 65;
        for (int i = 1; i < maxEntityIndex; ++i) {
            uintptr_t entity = GetEntityByIndex(entityList, i); 
            if (!entity || entity == localPawn) continue;
            
            if (i <= 64) {
                uint32_t handle = mem.Read<uint32_t>(entity + 0x90C); // m_hPlayerPawn
                if (handle == 0 || handle == 0xFFFFFFFF) continue;

                uintptr_t pawn = GetEntityByHandle(entityList, handle); 
                if (!pawn || pawn == localPawn) continue;
                
                int health = mem.Read<int>(pawn + 0x34C);
                int team = mem.Read<uint8_t>(pawn + 0x3EB);
                PlayerSlowCache& slow = playerSlowCache[entity];
                if (slow.name.empty() || (fC % 60) == 0) {
                    uintptr_t namePtr = mem.Read<uintptr_t>(entity + 0x860);
                    slow.name = namePtr ? mem.ReadString(namePtr) : "unknown";
                }
                const std::string& playerName = slow.name;
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
                
                if (settings.visuals.radarHack && p.team != localTeam) {
                    mem.Write<bool>(pawn + 0x1C38 + 0x8, true);
                    if (cachedLocalIndex >= 1 && cachedLocalIndex <= 64) {
                        uintptr_t maskAddr = pawn + 0x1C38 + 0xC + ((cachedLocalIndex - 1) >= 32 ? 4 : 0);
                        uint32_t mask = mem.Read<uint32_t>(maskAddr);
                        mask |= (1u << ((cachedLocalIndex - 1) & 31));
                        mem.Write<uint32_t>(maskAddr, mask);
                    }
                }
                
                p.name = playerName;

                {
                    uintptr_t activeWeapon = GetActiveWeapon(pawn);
                    if (activeWeapon != slow.activeWeapon || slow.weaponName.empty() || (fC % 30) == 0) {
                        slow.activeWeapon = activeWeapon;
                        uint16_t rawWid = GetWeaponEntityID(activeWeapon);
                        std::string wDesign = GetEntityDesignerNameLower(activeWeapon);
                        slow.weaponId = ResolveKnifeWeaponId(rawWid, wDesign);
                        slow.weaponName = GetWeaponName(slow.weaponId);
                        slow.maxAmmo = GetWeaponMaxAmmo(slow.weaponId);
                    }
                    p.weaponId = slow.weaponId;
                    p.weapon = slow.weaponName;
                    p.ammo = activeWeapon ? mem.Read<int>(activeWeapon + 0x16D8) : -1;
                    p.maxAmmo = slow.maxAmmo;
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
            static int lastLocalTotalHits = -1;
            static uint64_t lastLocalServerHitTime = 0;
            static int pendingLocalHitEvents = 0;
            uintptr_t bulletServices = mem.Read<uintptr_t>(localPawn + 0x1468);
            int localTotalHits = bulletServices ? mem.Read<int>(bulletServices + 0x48) : 0;
            uint64_t now = GetTickCount64();

            if (lastLocalTotalHits < 0) lastLocalTotalHits = localTotalHits;
            if (localTotalHits > lastLocalTotalHits) {
                pendingLocalHitEvents += (localTotalHits - lastLocalTotalHits);
                if (pendingLocalHitEvents > 8) pendingLocalHitEvents = 8;
                lastLocalServerHitTime = now;
            }
            if (localTotalHits < lastLocalTotalHits) {
                pendingLocalHitEvents = 0;
                lastLocalServerHitTime = 0;
            }
            lastLocalTotalHits = localTotalHits;

            for (const auto& np : tP) {
                if (!np.isEnemy) continue;
                auto it = lastHealth.find(np.pawn);
                if (it != lastHealth.end() && np.health < it->second) {
                    bool localDamage = (pendingLocalHitEvents > 0 && lastLocalServerHitTime > 0 && now - lastLocalServerHitTime <= 350);
                    if (!localDamage) {
                        lastHealth[np.pawn] = np.health;
                        continue;
                    }
                    pendingLocalHitEvents--;
                    g_lastHitTime = now;
                    int dmg = it->second - np.health;
                    {
                        std::lock_guard<std::mutex> lk(g_hitLogMutex);
                        HitLogEntry entry;
                        entry.targetName = np.name;
                        entry.dmg = dmg;
                        entry.remainingHp = np.health;
                        entry.time = now;
                        g_hitLog.push_back(entry);
                        if (g_hitLog.size() > 8) g_hitLog.erase(g_hitLog.begin());
                    }
                    LuaEngine::NotifyHit(np.name, dmg, np.health);
                    if (np.health > 0 && settings.misc.hitSound && !g_hitSounds.empty() && settings.misc.hitSoundIndex >= 0 && settings.misc.hitSoundIndex < (int)g_hitSounds.size()) {
                        std::string path = g_hitSoundsDir + "\\" + g_hitSounds[settings.misc.hitSoundIndex];
                        PlaySoundA(path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
                    }
                }
                lastHealth[np.pawn] = np.health;
            }
        }
        
        LuaGamePlayerInfo localLuaInfo;
        localLuaInfo.index = cachedLocalIndex;
        localLuaInfo.health = mem.Read<int>(localPawn + 0x34C);
        localLuaInfo.team = localTeam;
        localLuaInfo.enemy = false;
        localLuaInfo.x = localOrigin.x; localLuaInfo.y = localOrigin.y; localLuaInfo.z = localOrigin.z;
        Vector3 localEye = localOrigin + mem.Read<Vector3>(localPawn + 0xE70);
        localLuaInfo.eyeX = localEye.x; localLuaInfo.eyeY = localEye.y; localLuaInfo.eyeZ = localEye.z;
        localLuaInfo.headX = localEye.x; localLuaInfo.headY = localEye.y; localLuaInfo.headZ = localEye.z + 6.0f;
        Vector3 localAngles = mem.Read<Vector3>(clientBase + dwViewAngles);
        localLuaInfo.pitch = localAngles.x;
        localLuaInfo.yaw = localAngles.y;
        localLuaInfo.weaponId = (int)GetWeaponID(localPawn);
        localLuaInfo.weapon = GetWeaponName((uint16_t)localLuaInfo.weaponId);
        {
            uintptr_t activeWeapon = GetActiveWeapon(localPawn);
            localLuaInfo.ammo = activeWeapon ? mem.Read<int>(activeWeapon + 0x16D8) : -1;
        }
        localLuaInfo.maxAmmo = -1;
        localLuaInfo.armor = mem.Read<int>(localPawn + 0x1C7C);
        localLuaInfo.scoped = mem.Read<bool>(localPawn + 0x1C50);
        uintptr_t localControllerNamePtr = mem.Read<uintptr_t>(localController + 0x860);
        localLuaInfo.name = localControllerNamePtr ? mem.ReadString(localControllerNamePtr) : "local";
        localLuaInfo.ping = mem.Read<int>(localController + 0x828);
        {
            uintptr_t moneyServices = mem.Read<uintptr_t>(localController + 0x808);
            if (moneyServices) localLuaInfo.money = mem.Read<int>(moneyServices + 0x40);
            uintptr_t itemServices = mem.Read<uintptr_t>(localController + 0x80C);
            if (itemServices) localLuaInfo.helmet = mem.Read<bool>(itemServices + 0x49);
        }

        std::vector<LuaGamePlayerInfo> luaPlayers;
        luaPlayers.reserve(tP.size());
        for (const auto& p : tP) {
            LuaGamePlayerInfo info;
            info.index = p.index;
            info.health = p.health;
            info.team = p.team;
            info.enemy = p.isEnemy;
            info.x = p.origin.x; info.y = p.origin.y; info.z = p.origin.z;
            info.headX = p.headPos.x; info.headY = p.headPos.y; info.headZ = p.headPos.z;
            info.eyeX = p.headPos.x; info.eyeY = p.headPos.y; info.eyeZ = p.headPos.z;
            info.name = p.name;
            info.weapon = p.weapon;
            info.weaponId = (int)p.weaponId;
            info.ammo = p.ammo;
            info.maxAmmo = p.maxAmmo;
            info.armor = p.armor;
            info.helmet = p.hasHelmet;
            info.scoped = p.isScoped;
            info.defusing = p.isDefusing;
            info.money = p.money;
            info.ping = p.ping;
            luaPlayers.push_back(std::move(info));
        }
        LuaEngine::UpdateGameSnapshot(true, localLuaInfo, luaPlayers);

        { std::lock_guard<std::mutex> lock(cacheMutex); players = tP; entities = tE; grenadeProjectiles = tGrenades; spectators = tS; smokePositions = tSmokes; cachedBomb = tBomb; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
