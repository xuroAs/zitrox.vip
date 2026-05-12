#include "../include/ZitroxState.hpp"

Memory mem;
Overlay overlay;
uintptr_t clientBase = 0;
uintptr_t engineBase = 0;
std::mutex cacheMutex;

std::vector<CachedPlayer> players;
std::vector<CachedEntity> entities;
std::vector<CachedEntity> grenadeProjectiles;
std::vector<std::string> spectators;
std::vector<Vector3> smokePositions;
BombInfo cachedBomb;
uint64_t g_lastHitTime = 0;
std::vector<HitLogEntry> g_hitLog;
std::mutex g_hitLogMutex;
std::vector<std::string> g_hitSounds;
std::string g_hitSoundsDir = "C:\\ZitFem\\sounds";
std::vector<HitMarkerShape> g_hmShapes;
int g_hmSelected = -1;

std::vector<Notification> g_notifications;
std::mutex g_notifMutex;

ViewMatrix cachedViewMatrix;
Vector3 cachedLocalEyePos;
Vector3 cachedLocalOrigin;
uintptr_t cachedLocalPawn = 0;
int cachedLocalIndex = -1;
Vector3 cachedViewAngles;
bool cachedInGame = false;

Settings settings;
std::vector<GrenadeLineupSpot> g_grenadeLineups;

volatile bool g_userHoldingLMB = false;
HHOOK g_mouseHook = nullptr;
