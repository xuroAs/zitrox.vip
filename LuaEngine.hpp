#pragma once
#include <string>
#include <vector>

struct LuaPlayerRenderInfo {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    int health = 0;
    int team = 0;
    bool enemy = false;
    std::string name;
    std::string weapon;
};

struct LuaGamePlayerInfo {
    int index = -1;
    int health = 0;
    int team = 0;
    bool enemy = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float headX = 0.0f;
    float headY = 0.0f;
    float headZ = 0.0f;
    float eyeX = 0.0f;
    float eyeY = 0.0f;
    float eyeZ = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    std::string name;
    std::string weapon;
    int weaponId = 0;
    int ammo = -1;
    int maxAmmo = -1;
    int armor = 0;
    bool helmet = false;
    bool scoped = false;
    bool defusing = false;
    int money = 0;
    int ping = 0;
};

struct LuaMenuGlowStyle {
    bool enabled = false;
    float r = 0.58f;
    float g = 0.74f;
    float b = 0.22f;
    float a = 0.35f;
    float thickness = 10.0f;
    float rounding = 12.0f;
};

namespace LuaEngine {
    void Initialize();
    void Shutdown();
    void RefreshScripts();
    void ReloadScripts();
    bool LoadScript(int scriptIndex);
    bool UnloadScript(int scriptIndex);
    void UnloadAllScripts();
    void RunTick();
    void RunRender();
    void RunPlayerEsp(const LuaPlayerRenderInfo& info);
    void RunMenuRender(float x, float y, float w, float h);
    LuaMenuGlowStyle GetMenuGlowStyle();
    void UpdateGameSnapshot(bool inGame, const LuaGamePlayerInfo& localPlayer, const std::vector<LuaGamePlayerInfo>& playerList);
    void NotifyHit(const std::string& targetName, int damage, int remainingHp);
    void NotifyRoundStart(int roundNumber);
    void RenderManager();
    void RenderMenu(int tabIndex);
    int GetTabCount();
    const char* GetTabName(int tabIndex);
    bool IsAvailable();
    const char* GetStatus();
}
