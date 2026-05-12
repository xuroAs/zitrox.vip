#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "LuaEngine.hpp"
#include "gui.hpp"
#include "../include/Globals.hpp"
#include "../include/Overlay.hpp"

#include <windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <type_traits>
#include <system_error>

struct lua_State;
using lua_CFunction = int(*)(lua_State* L);
using lua_Integer = long long;
using lua_Number = double;
using lua_KContext = intptr_t;
using lua_KFunction = int(*)(lua_State* L, int status, lua_KContext ctx);

extern Overlay overlay;
extern bool cachedInGame;
void PushNotification(const std::string& text, float r, float g, float b, uint32_t durationMs);

namespace LuaEngine {
namespace {
    constexpr int LUA_MULTRET = -1;
    constexpr int LUA_REGISTRYINDEX = -1001000;
    constexpr int LUA_NOREF = -2;
    constexpr int LUA_TNIL = 0;
    constexpr int LUA_TBOOLEAN = 1;
    constexpr int LUA_TNUMBER = 3;
    constexpr int LUA_TSTRING = 4;
    constexpr int LUA_TTABLE = 5;
    constexpr int LUA_TFUNCTION = 6;

    enum class WidgetType { Text, Checkbox, Slider, Combo, Button };

    struct LuaApi {
        HMODULE module = nullptr;
        lua_State* state = nullptr;
        lua_State* (*L_newstate)() = nullptr;
        void (*L_openlibs)(lua_State*) = nullptr;
        void (*close)(lua_State*) = nullptr;
        int (*L_loadfilex)(lua_State*, const char*, const char*) = nullptr;
        int (*pcallk)(lua_State*, int, int, int, lua_KContext, lua_KFunction) = nullptr;
        void (*createtable)(lua_State*, int, int) = nullptr;
        void (*setglobal)(lua_State*, const char*) = nullptr;
        int (*getglobal)(lua_State*, const char*) = nullptr;
        void (*setfield)(lua_State*, int, const char*) = nullptr;
        int (*getfield)(lua_State*, int, const char*) = nullptr;
        void (*pushcclosure)(lua_State*, lua_CFunction, int) = nullptr;
        void (*pushnumber)(lua_State*, lua_Number) = nullptr;
        void (*pushinteger)(lua_State*, lua_Integer) = nullptr;
        void (*pushboolean)(lua_State*, int) = nullptr;
        const char* (*pushstring)(lua_State*, const char*) = nullptr;
        void (*pushnil)(lua_State*) = nullptr;
        void (*pushvalue)(lua_State*, int) = nullptr;
        const char* (*tolstring)(lua_State*, int, size_t*) = nullptr;
        lua_Number (*tonumberx)(lua_State*, int, int*) = nullptr;
        lua_Integer (*tointegerx)(lua_State*, int, int*) = nullptr;
        int (*toboolean)(lua_State*, int) = nullptr;
        int (*type)(lua_State*, int) = nullptr;
        int (*gettop)(lua_State*) = nullptr;
        void (*settop)(lua_State*, int) = nullptr;
        int (*L_ref)(lua_State*, int) = nullptr;
        void (*L_unref)(lua_State*, int, int) = nullptr;
        int (*rawgeti)(lua_State*, int, lua_Integer) = nullptr;
        void (*rawseti)(lua_State*, int, lua_Integer) = nullptr;
        size_t (*rawlen)(lua_State*, int) = nullptr;
        int (*next)(lua_State*, int) = nullptr;
    } lua;

    struct LuaWidget {
        int id = 0;
        int ownerScript = -1;
        WidgetType type = WidgetType::Text;
        std::string label;
        bool boolValue = false;
        float floatValue = 0.0f;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        int intValue = 0;
        std::vector<std::string> items;
        int callbackRef = LUA_NOREF;
    };

    struct LuaSection {
        int id = 0;
        int ownerScript = -1;
        std::string name;
        std::vector<int> widgetIds;
    };

    struct LuaTab {
        int id = 0;
        int ownerScript = -1;
        std::string name;
        std::vector<int> sectionIds;
    };

    struct LuaCallbackRef {
        int ownerScript = -1;
        int ref = LUA_NOREF;
    };

    struct LuaScript {
        int id = 0;
        std::filesystem::path path;
        std::string name;
        bool loaded = false;
    };

    std::recursive_mutex g_luaMutex;
    std::mutex g_mutex;
    std::vector<LuaTab> g_tabs;
    std::vector<LuaSection> g_sections;
    std::vector<LuaWidget> g_widgets;
    std::vector<LuaCallbackRef> g_tickCallbacks;
    std::vector<LuaCallbackRef> g_renderCallbacks;
    std::vector<LuaCallbackRef> g_playerEspCallbacks;
    std::vector<LuaCallbackRef> g_hitCallbacks;
    std::vector<LuaCallbackRef> g_roundStartCallbacks;
    std::vector<LuaCallbackRef> g_menuRenderCallbacks;
    std::vector<LuaScript> g_scripts;
    LuaGamePlayerInfo g_localPlayer;
    std::vector<LuaGamePlayerInfo> g_gamePlayers;
    bool g_gameSnapshotInGame = false;
    LuaMenuGlowStyle g_menuGlow;
    ImDrawList* g_drawList = nullptr;
    std::string g_status = "Lua not initialized";
    bool g_available = false;
    int g_loadingScriptIndex = -1;
    int g_loadingScriptId = -1;
    int g_selectedScriptIndex = 0;
    int g_nextTabId = 1;
    int g_nextSectionId = 1;
    int g_nextWidgetId = 1;
    int g_nextScriptId = 1;

    std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return value;
    }

    template <typename T>
    T* ById(std::vector<T>& list, int id) {
        if (id <= 0) return nullptr;
        for (auto& item : list) if (item.id == id) return &item;
        return nullptr;
    }

    const char* ToString(lua_State* L, int idx, const char* fallback = "") {
        size_t len = 0;
        const char* s = lua.tolstring ? lua.tolstring(L, idx, &len) : nullptr;
        return s ? s : fallback;
    }

    float ToFloat(lua_State* L, int idx, float fallback = 0.0f) {
        int ok = 0;
        lua_Number n = lua.tonumberx ? lua.tonumberx(L, idx, &ok) : 0.0;
        return ok ? (float)n : fallback;
    }

    int ToInt(lua_State* L, int idx, int fallback = 0) {
        int ok = 0;
        lua_Integer n = lua.tointegerx ? lua.tointegerx(L, idx, &ok) : 0;
        return ok ? (int)n : fallback;
    }

    bool IsFunction(lua_State* L, int idx) {
        return lua.type && lua.type(L, idx) == LUA_TFUNCTION;
    }

    int MakeRef(lua_State* L, int idx) {
        if (!IsFunction(L, idx)) return LUA_NOREF;
        lua.pushvalue(L, idx);
        return lua.L_ref(L, LUA_REGISTRYINDEX);
    }

    void Unref(int& ref) {
        if (lua.state && ref != LUA_NOREF) lua.L_unref(lua.state, LUA_REGISTRYINDEX, ref);
        ref = LUA_NOREF;
    }

    void UnrefCallbackList(std::vector<LuaCallbackRef>& refs) {
        for (auto& item : refs) Unref(item.ref);
        refs.clear();
    }

    void ClearScriptState() {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& w : g_widgets) Unref(w.callbackRef);
        UnrefCallbackList(g_tickCallbacks);
        UnrefCallbackList(g_renderCallbacks);
        UnrefCallbackList(g_playerEspCallbacks);
        UnrefCallbackList(g_hitCallbacks);
        UnrefCallbackList(g_roundStartCallbacks);
        UnrefCallbackList(g_menuRenderCallbacks);
        g_tabs.clear();
        g_sections.clear();
        g_widgets.clear();
        for (auto& script : g_scripts) script.loaded = false;
        g_nextTabId = 1;
        g_nextSectionId = 1;
        g_nextWidgetId = 1;
        if (lua.state) lua.settop(lua.state, 0);
    }

    void RemoveScriptStateById(int scriptId) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_widgets.begin(); it != g_widgets.end();) {
            if (it->ownerScript == scriptId) {
                Unref(it->callbackRef);
                it = g_widgets.erase(it);
            } else {
                ++it;
            }
        }
        g_sections.erase(std::remove_if(g_sections.begin(), g_sections.end(), [&](const LuaSection& section) {
            return section.ownerScript == scriptId;
        }), g_sections.end());
        g_tabs.erase(std::remove_if(g_tabs.begin(), g_tabs.end(), [&](const LuaTab& tab) {
            return tab.ownerScript == scriptId;
        }), g_tabs.end());
        auto removeCallbacks = [&](std::vector<LuaCallbackRef>& refs) {
            for (auto it = refs.begin(); it != refs.end();) {
                if (it->ownerScript == scriptId) {
                    Unref(it->ref);
                    it = refs.erase(it);
                } else {
                    ++it;
                }
            }
        };
        removeCallbacks(g_tickCallbacks);
        removeCallbacks(g_renderCallbacks);
        removeCallbacks(g_playerEspCallbacks);
        removeCallbacks(g_hitCallbacks);
        removeCallbacks(g_roundStartCallbacks);
        removeCallbacks(g_menuRenderCallbacks);
        for (auto& script : g_scripts) {
            if (script.id == scriptId) {
                script.loaded = false;
                break;
            }
        }
        if (lua.state) lua.settop(lua.state, 0);
    }

    void RemoveScriptState(int scriptIndex) {
        if (scriptIndex < 0 || scriptIndex >= (int)g_scripts.size()) return;
        RemoveScriptStateById(g_scripts[scriptIndex].id);
    }

    void ReportError(const std::string& prefix) {
        const char* err = lua.state ? ToString(lua.state, -1, "unknown error") : "Lua is not loaded";
        g_status = prefix + ": " + err;
        if (lua.state) lua.settop(lua.state, -2);
        PushNotification(g_status, 1.0f, 0.35f, 0.25f, 5000);
    }

    bool PCall(int nargs, int nresults = 0) {
        if (!lua.state) return false;
        int status = lua.pcallk(lua.state, nargs, nresults, 0, 0, nullptr);
        if (status != 0) {
            ReportError("Lua runtime error");
            return false;
        }
        return true;
    }

    void PushColor(lua_State* L, int r, int g, int b, int a) {
        lua.pushinteger(L, std::clamp(r, 0, 255));
        lua.pushinteger(L, std::clamp(g, 0, 255));
        lua.pushinteger(L, std::clamp(b, 0, 255));
        lua.pushinteger(L, std::clamp(a, 0, 255));
    }

    ImU32 ColorFromArgs(lua_State* L, int first) {
        int r = ToInt(L, first, 255);
        int g = ToInt(L, first + 1, 255);
        int b = ToInt(L, first + 2, 255);
        int a = ToInt(L, first + 3, 255);
        return IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), std::clamp(a, 0, 255));
    }

    bool* FindBoolSetting(const std::string& raw) {
        std::string p = ToLower(raw);
        if (p == "aim.enabled") return &settings.aim.enabled;
        if (p == "aim.autopistol") return &settings.aim.autoPistol;
        if (p == "aim.showfov") return &settings.aim.showFov;
        if (p == "aim.chicken") return &settings.aim.chickenAimbot;
        if (p == "trigger.enabled") return &settings.trigger.enabled;
        if (p == "trigger.hitboxfilter") return &settings.trigger.hitboxFilter;
        if (p == "trigger.burst") return &settings.trigger.burstMode;
        if (p == "trigger.wallbang") return &settings.trigger.wallbang;
        if (p == "visuals.enabled") return &settings.visuals.enabled;
        if (p == "visuals.teamcheck") return &settings.visuals.teamCheck;
        if (p == "visuals.boxes") return &settings.visuals.boxes;
        if (p == "visuals.skeleton") return &settings.visuals.skeleton;
        if (p == "visuals.glow") return &settings.visuals.glow;
        if (p == "visuals.health") return &settings.visuals.health;
        if (p == "visuals.healthtext") return &settings.visuals.healthText;
        if (p == "visuals.names") return &settings.visuals.names;
        if (p == "visuals.weapon") return &settings.visuals.activeWeapon;
        if (p == "visuals.ammobar") return &settings.visuals.ammoBar;
        if (p == "visuals.ammotext") return &settings.visuals.ammoText;
        if (p == "visuals.grenadeprediction") return &settings.visuals.grenadePrediction;
        if (p == "visuals.grenadeworld") return &settings.visuals.grenadeWorldEsp;
        if (p == "visuals.radarhack") return &settings.visuals.radarHack;
        if (p == "visuals.overlayradar") return &settings.visuals.overlayRadar;
        if (p == "visuals.bomb") return &settings.visuals.bombESP;
        if (p == "misc.bhop") return &settings.misc.bhop;
        if (p == "misc.noflash") return &settings.misc.noFlash;
        if (p == "misc.zeusbot") return &settings.misc.zeusBot;
        if (p == "misc.knifebot") return &settings.misc.knifeBot;
        if (p == "misc.watermark") return &settings.misc.watermark;
        if (p == "misc.hitmarker") return &settings.misc.hitMarker;
        if (p == "misc.hitlogger") return &settings.misc.hitLogger;
        if (p == "grenadehelper.enabled") return &settings.grenadeHelper.enabled;
        if (p == "grenadehelper.overlay") return &settings.grenadeHelper.drawOverlay;
        return nullptr;
    }

    float* FindFloatSetting(const std::string& raw) {
        std::string p = ToLower(raw);
        if (p == "aim.rifle.fov") return &settings.aim.rifle.fov;
        if (p == "aim.rifle.smooth") return &settings.aim.rifle.smooth;
        if (p == "aim.pistol.fov") return &settings.aim.pistol.fov;
        if (p == "aim.pistol.smooth") return &settings.aim.pistol.smooth;
        if (p == "aim.sniper.fov") return &settings.aim.sniper.fov;
        if (p == "aim.sniper.smooth") return &settings.aim.sniper.smooth;
        if (p == "trigger.delay") return &settings.trigger.delay;
        if (p == "trigger.wallbangtolerance") return &settings.trigger.wallbangTolerance;
        if (p == "visuals.glowintensity") return &settings.visuals.glowIntensity;
        if (p == "visuals.radarrange") return &settings.visuals.radarRange;
        if (p == "visuals.arrowsradius") return &settings.visuals.arrowsRadius;
        if (p == "visuals.arrowssize") return &settings.visuals.arrowsSize;
        if (p == "misc.spreadscale") return &settings.misc.spreadScale;
        if (p == "grenadehelper.postolerance") return &settings.grenadeHelper.posTolerance;
        if (p == "grenadehelper.standradius") return &settings.grenadeHelper.standRingRadius;
        return nullptr;
    }

    int* FindIntSetting(const std::string& raw) {
        std::string p = ToLower(raw);
        if (p == "theme") return &settings.theme;
        if (p == "visuals.boxstyle") return &settings.visuals.boxStyle;
        if (p == "misc.watermarkstyle") return &settings.misc.watermarkStyle;
        if (p == "grenadehelper.map") return &settings.grenadeHelper.mapIndex;
        if (p == "grenadehelper.mode") return &settings.grenadeHelper.recordMode;
        if (p == "grenadehelper.jumpdelay") return &settings.grenadeHelper.jumpThrowDelayMs;
        if (p == "grenadehelper.runpre") return &settings.grenadeHelper.runThrowPreMs;
        if (p == "grenadehelper.standmode") return &settings.grenadeHelper.standCircleMode;
        return nullptr;
    }

    int UiAddTab(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        LuaTab t;
        t.id = g_nextTabId++;
        t.ownerScript = g_loadingScriptId;
        t.name = ToString(L, 1, "Lua");
        if (t.name.empty()) t.name = "Lua";
        g_tabs.push_back(t);
        lua.pushinteger(L, t.id);
        return 1;
    }

    int UiAddSection(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int tabId = ToInt(L, 1, 0);
        LuaTab* tab = ById(g_tabs, tabId);
        if (!tab) { lua.pushinteger(L, 0); return 1; }
        LuaSection s;
        s.id = g_nextSectionId++;
        s.ownerScript = g_loadingScriptId;
        s.name = ToString(L, 2, "Section");
        if (s.name.empty()) s.name = "Section";
        g_sections.push_back(s);
        tab->sectionIds.push_back(s.id);
        lua.pushinteger(L, s.id);
        return 1;
    }

    int PushWidget(lua_State* L, LuaWidget w, int callbackIndex) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int sectionId = ToInt(L, 1, 0);
        LuaSection* section = ById(g_sections, sectionId);
        if (!section) { lua.pushinteger(L, 0); return 1; }
        w.id = g_nextWidgetId++;
        w.ownerScript = g_loadingScriptId;
        w.callbackRef = MakeRef(L, callbackIndex);
        g_widgets.push_back(w);
        section->widgetIds.push_back(w.id);
        lua.pushinteger(L, w.id);
        return 1;
    }

    int UiText(lua_State* L) {
        LuaWidget w;
        w.type = WidgetType::Text;
        w.label = ToString(L, 2, "Text");
        return PushWidget(L, w, 0);
    }

    int UiCheckbox(lua_State* L) {
        LuaWidget w;
        w.type = WidgetType::Checkbox;
        w.label = ToString(L, 2, "Checkbox");
        w.boolValue = lua.toboolean(L, 3) != 0;
        return PushWidget(L, w, 4);
    }

    int UiSlider(lua_State* L) {
        LuaWidget w;
        w.type = WidgetType::Slider;
        w.label = ToString(L, 2, "Slider");
        w.floatValue = ToFloat(L, 3, 0.0f);
        w.minValue = ToFloat(L, 4, 0.0f);
        w.maxValue = ToFloat(L, 5, 1.0f);
        if (w.maxValue < w.minValue) std::swap(w.maxValue, w.minValue);
        return PushWidget(L, w, 6);
    }

    int UiCombo(lua_State* L) {
        LuaWidget w;
        w.type = WidgetType::Combo;
        w.label = ToString(L, 2, "Combo");
        w.intValue = std::max(0, ToInt(L, 4, 1) - 1);
        if (lua.type(L, 3) == LUA_TTABLE) {
            size_t len = lua.rawlen(L, 3);
            for (size_t i = 1; i <= len; ++i) {
                lua.rawgeti(L, 3, (lua_Integer)i);
                w.items.push_back(ToString(L, -1, "item"));
                lua.settop(L, -2);
            }
        }
        if (w.items.empty()) w.items.push_back("empty");
        if (w.intValue >= (int)w.items.size()) w.intValue = 0;
        return PushWidget(L, w, 5);
    }

    int UiButton(lua_State* L) {
        LuaWidget w;
        w.type = WidgetType::Button;
        w.label = ToString(L, 2, "Button");
        return PushWidget(L, w, 3);
    }

    int UiGet(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        LuaWidget* w = ById(g_widgets, ToInt(L, 1, 0));
        if (!w) { lua.pushnil(L); return 1; }
        if (w->type == WidgetType::Checkbox) lua.pushboolean(L, w->boolValue ? 1 : 0);
        else if (w->type == WidgetType::Slider) lua.pushnumber(L, w->floatValue);
        else if (w->type == WidgetType::Combo) lua.pushinteger(L, w->intValue + 1);
        else lua.pushnil(L);
        return 1;
    }

    int UiSet(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        LuaWidget* w = ById(g_widgets, ToInt(L, 1, 0));
        if (!w) return 0;
        if (w->type == WidgetType::Checkbox) w->boolValue = lua.toboolean(L, 2) != 0;
        else if (w->type == WidgetType::Slider) w->floatValue = ToFloat(L, 2, w->floatValue);
        else if (w->type == WidgetType::Combo) w->intValue = std::clamp(ToInt(L, 2, w->intValue + 1) - 1, 0, (int)w->items.size() - 1);
        return 0;
    }

    int CallbacksOnTick(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int ref = MakeRef(L, 1);
        if (ref != LUA_NOREF) g_tickCallbacks.push_back({ g_loadingScriptId, ref });
        return 0;
    }

    int CallbacksOnRender(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int ref = MakeRef(L, 1);
        if (ref != LUA_NOREF) g_renderCallbacks.push_back({ g_loadingScriptId, ref });
        return 0;
    }

    int CallbacksOnPlayerEsp(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int ref = MakeRef(L, 1);
        if (ref != LUA_NOREF) g_playerEspCallbacks.push_back({ g_loadingScriptId, ref });
        return 0;
    }

    int CallbacksOnHit(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int ref = MakeRef(L, 1);
        if (ref != LUA_NOREF) g_hitCallbacks.push_back({ g_loadingScriptId, ref });
        return 0;
    }

    int CallbacksOnRoundStart(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int ref = MakeRef(L, 1);
        if (ref != LUA_NOREF) g_roundStartCallbacks.push_back({ g_loadingScriptId, ref });
        return 0;
    }

    int CallbacksOnMenuRender(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        int ref = MakeRef(L, 1);
        if (ref != LUA_NOREF) g_menuRenderCallbacks.push_back({ g_loadingScriptId, ref });
        return 0;
    }

    void PushGamePlayerTable(lua_State* L, const LuaGamePlayerInfo& p) {
        lua.createtable(L, 0, 23);
        lua.pushinteger(L, p.index); lua.setfield(L, -2, "index");
        lua.pushinteger(L, p.health); lua.setfield(L, -2, "health");
        lua.pushinteger(L, p.team); lua.setfield(L, -2, "team");
        lua.pushboolean(L, p.enemy ? 1 : 0); lua.setfield(L, -2, "enemy");
        lua.pushnumber(L, p.x); lua.setfield(L, -2, "x");
        lua.pushnumber(L, p.y); lua.setfield(L, -2, "y");
        lua.pushnumber(L, p.z); lua.setfield(L, -2, "z");
        lua.pushnumber(L, p.headX); lua.setfield(L, -2, "head_x");
        lua.pushnumber(L, p.headY); lua.setfield(L, -2, "head_y");
        lua.pushnumber(L, p.headZ); lua.setfield(L, -2, "head_z");
        lua.pushnumber(L, p.eyeX); lua.setfield(L, -2, "eye_x");
        lua.pushnumber(L, p.eyeY); lua.setfield(L, -2, "eye_y");
        lua.pushnumber(L, p.eyeZ); lua.setfield(L, -2, "eye_z");
        lua.pushnumber(L, p.pitch); lua.setfield(L, -2, "pitch");
        lua.pushnumber(L, p.yaw); lua.setfield(L, -2, "yaw");
        lua.pushstring(L, p.name.c_str()); lua.setfield(L, -2, "name");
        lua.pushstring(L, p.weapon.c_str()); lua.setfield(L, -2, "weapon");
        lua.pushinteger(L, p.weaponId); lua.setfield(L, -2, "weapon_id");
        lua.pushinteger(L, p.ammo); lua.setfield(L, -2, "ammo");
        lua.pushinteger(L, p.maxAmmo); lua.setfield(L, -2, "max_ammo");
        lua.pushinteger(L, p.armor); lua.setfield(L, -2, "armor");
        lua.pushboolean(L, p.helmet ? 1 : 0); lua.setfield(L, -2, "helmet");
        lua.pushboolean(L, p.scoped ? 1 : 0); lua.setfield(L, -2, "scoped");
        lua.pushboolean(L, p.defusing ? 1 : 0); lua.setfield(L, -2, "defusing");
        lua.pushinteger(L, p.money); lua.setfield(L, -2, "money");
        lua.pushinteger(L, p.ping); lua.setfield(L, -2, "ping");
    }

    int GameIsInGame(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        lua.pushboolean(L, g_gameSnapshotInGame ? 1 : 0);
        return 1;
    }

    int GameLocalPlayer(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_gameSnapshotInGame || g_localPlayer.index < 0) {
            lua.pushnil(L);
            return 1;
        }
        PushGamePlayerTable(L, g_localPlayer);
        return 1;
    }

    int GameGetPlayers(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        lua.createtable(L, (int)g_gamePlayers.size(), 0);
        int idx = 1;
        for (const auto& player : g_gamePlayers) {
            PushGamePlayerTable(L, player);
            lua.rawseti(L, -2, (lua_Integer)idx++);
        }
        return 1;
    }

    static bool SendVk(WORD vk) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vk;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vk;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        return SendInput(2, inputs, sizeof(INPUT)) == 2;
    }

    static void SendUnicodeText(const std::wstring& text) {
        for (wchar_t ch : text) {
            INPUT down{};
            down.type = INPUT_KEYBOARD;
            down.ki.wScan = ch;
            down.ki.dwFlags = KEYEVENTF_UNICODE;
            INPUT up = down;
            up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            INPUT pair[2] = { down, up };
            SendInput(2, pair, sizeof(INPUT));
        }
    }

    static bool SendChatMessage(const std::string& msg, bool team) {
        if (msg.empty()) return false;
        HWND game = FindWindowA("SDL_app", "Counter-Strike 2");
        if (!game || GetForegroundWindow() != game) return false;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
        if (wlen <= 1) return false;
        std::wstring wide((size_t)wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wide.data(), wlen);
        SendVk(team ? 'U' : 'Y');
        Sleep(40);
        SendUnicodeText(wide);
        Sleep(10);
        SendVk(VK_RETURN);
        return true;
    }

    int GameSay(lua_State* L) {
        lua.pushboolean(L, SendChatMessage(ToString(L, 1, ""), false) ? 1 : 0);
        return 1;
    }

    int GameSayTeam(lua_State* L) {
        lua.pushboolean(L, SendChatMessage(ToString(L, 1, ""), true) ? 1 : 0);
        return 1;
    }

    int MenuSetGlow(lua_State* L) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_menuGlow.enabled = lua.toboolean(L, 1) != 0;
        g_menuGlow.r = std::clamp(ToFloat(L, 2, g_menuGlow.r), 0.0f, 1.0f);
        g_menuGlow.g = std::clamp(ToFloat(L, 3, g_menuGlow.g), 0.0f, 1.0f);
        g_menuGlow.b = std::clamp(ToFloat(L, 4, g_menuGlow.b), 0.0f, 1.0f);
        g_menuGlow.a = std::clamp(ToFloat(L, 5, g_menuGlow.a), 0.0f, 1.0f);
        g_menuGlow.thickness = std::clamp(ToFloat(L, 6, g_menuGlow.thickness), 1.0f, 64.0f);
        g_menuGlow.rounding = std::clamp(ToFloat(L, 7, g_menuGlow.rounding), 0.0f, 40.0f);
        return 0;
    }

    int SettingsGetBool(lua_State* L) {
        bool* value = FindBoolSetting(ToString(L, 1));
        if (!value) { lua.pushnil(L); return 1; }
        lua.pushboolean(L, *value ? 1 : 0);
        return 1;
    }

    int SettingsSetBool(lua_State* L) {
        bool* value = FindBoolSetting(ToString(L, 1));
        if (value) *value = lua.toboolean(L, 2) != 0;
        return 0;
    }

    int SettingsGetFloat(lua_State* L) {
        float* value = FindFloatSetting(ToString(L, 1));
        if (!value) { lua.pushnil(L); return 1; }
        lua.pushnumber(L, *value);
        return 1;
    }

    int SettingsSetFloat(lua_State* L) {
        float* value = FindFloatSetting(ToString(L, 1));
        if (value) *value = ToFloat(L, 2, *value);
        return 0;
    }

    int SettingsGetInt(lua_State* L) {
        int* value = FindIntSetting(ToString(L, 1));
        if (!value) { lua.pushnil(L); return 1; }
        lua.pushinteger(L, *value);
        return 1;
    }

    int SettingsSetInt(lua_State* L) {
        int* value = FindIntSetting(ToString(L, 1));
        if (value) *value = ToInt(L, 2, *value);
        return 0;
    }

    int CheatNotify(lua_State* L) {
        PushNotification(ToString(L, 1, "Lua notification"), ToFloat(L, 2, 0.65f), ToFloat(L, 3, 0.9f), ToFloat(L, 4, 0.25f), (uint32_t)ToInt(L, 5, 3000));
        return 0;
    }

    int CheatIsInGame(lua_State* L) {
        lua.pushboolean(L, cachedInGame ? 1 : 0);
        return 1;
    }

    int RenderScreenSize(lua_State* L) {
        lua.pushinteger(L, overlay.Width);
        lua.pushinteger(L, overlay.Height);
        return 2;
    }

    int RenderText(lua_State* L) {
        if (!g_drawList) return 0;
        float x = ToFloat(L, 1);
        float y = ToFloat(L, 2);
        const char* text = ToString(L, 3, "");
        g_drawList->AddText(ImVec2(x, y), ColorFromArgs(L, 4), text);
        return 0;
    }

    int RenderLine(lua_State* L) {
        if (!g_drawList) return 0;
        g_drawList->AddLine(ImVec2(ToFloat(L, 1), ToFloat(L, 2)), ImVec2(ToFloat(L, 3), ToFloat(L, 4)), ColorFromArgs(L, 5), ToFloat(L, 9, 1.0f));
        return 0;
    }

    int RenderRect(lua_State* L) {
        if (!g_drawList) return 0;
        float x = ToFloat(L, 1), y = ToFloat(L, 2), w = ToFloat(L, 3), h = ToFloat(L, 4);
        g_drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), ColorFromArgs(L, 5), ToFloat(L, 10, 0.0f), 0, ToFloat(L, 9, 1.0f));
        return 0;
    }

    int RenderFilledRect(lua_State* L) {
        if (!g_drawList) return 0;
        float x = ToFloat(L, 1), y = ToFloat(L, 2), w = ToFloat(L, 3), h = ToFloat(L, 4);
        g_drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ColorFromArgs(L, 5), ToFloat(L, 9, 0.0f));
        return 0;
    }

    int RenderCircle(lua_State* L) {
        if (!g_drawList) return 0;
        g_drawList->AddCircle(ImVec2(ToFloat(L, 1), ToFloat(L, 2)), ToFloat(L, 3), ColorFromArgs(L, 4), ToInt(L, 8, 32), ToFloat(L, 9, 1.0f));
        return 0;
    }

    int RenderFilledCircle(lua_State* L) {
        if (!g_drawList) return 0;
        g_drawList->AddCircleFilled(ImVec2(ToFloat(L, 1), ToFloat(L, 2)), ToFloat(L, 3), ColorFromArgs(L, 4), ToInt(L, 8, 32));
        return 0;
    }

    void SetFunction(lua_State* L, const char* name, lua_CFunction fn) {
        lua.pushcclosure(L, fn, 0);
        lua.setfield(L, -2, name);
    }

    void RegisterApi(lua_State* L) {
        lua.createtable(L, 0, 9);
        SetFunction(L, "add_tab", UiAddTab);
        SetFunction(L, "add_section", UiAddSection);
        SetFunction(L, "text", UiText);
        SetFunction(L, "checkbox", UiCheckbox);
        SetFunction(L, "slider", UiSlider);
        SetFunction(L, "combo", UiCombo);
        SetFunction(L, "button", UiButton);
        SetFunction(L, "get", UiGet);
        SetFunction(L, "set", UiSet);
        lua.setglobal(L, "ui");

        lua.createtable(L, 0, 6);
        SetFunction(L, "on_tick", CallbacksOnTick);
        SetFunction(L, "on_render", CallbacksOnRender);
        SetFunction(L, "on_player_esp", CallbacksOnPlayerEsp);
        SetFunction(L, "on_hit", CallbacksOnHit);
        SetFunction(L, "on_round_start", CallbacksOnRoundStart);
        SetFunction(L, "on_menu_render", CallbacksOnMenuRender);
        lua.setglobal(L, "callbacks");

        lua.createtable(L, 0, 6);
        SetFunction(L, "get_bool", SettingsGetBool);
        SetFunction(L, "set_bool", SettingsSetBool);
        SetFunction(L, "get_float", SettingsGetFloat);
        SetFunction(L, "set_float", SettingsSetFloat);
        SetFunction(L, "get_int", SettingsGetInt);
        SetFunction(L, "set_int", SettingsSetInt);
        lua.setglobal(L, "settings");

        lua.createtable(L, 0, 2);
        SetFunction(L, "notify", CheatNotify);
        SetFunction(L, "is_in_game", CheatIsInGame);
        lua.setglobal(L, "cheat");

        lua.createtable(L, 0, 7);
        SetFunction(L, "screen_size", RenderScreenSize);
        SetFunction(L, "text", RenderText);
        SetFunction(L, "line", RenderLine);
        SetFunction(L, "rect", RenderRect);
        SetFunction(L, "filled_rect", RenderFilledRect);
        SetFunction(L, "circle", RenderCircle);
        SetFunction(L, "filled_circle", RenderFilledCircle);
        lua.setglobal(L, "render");

        lua.createtable(L, 0, 5);
        SetFunction(L, "is_in_game", GameIsInGame);
        SetFunction(L, "local_player", GameLocalPlayer);
        SetFunction(L, "get_players", GameGetPlayers);
        SetFunction(L, "say", GameSay);
        SetFunction(L, "say_team", GameSayTeam);
        lua.setglobal(L, "game");

        lua.createtable(L, 0, 1);
        SetFunction(L, "set_glow", MenuSetGlow);
        lua.setglobal(L, "menu");
    }

    template <typename Fn>
    void ForEachCallback(const std::vector<LuaCallbackRef>& callbacks, Fn pushArgs) {
        if (!lua.state) return;
        std::vector<int> refs;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto& callback : callbacks) {
                if (callback.ref != LUA_NOREF) refs.push_back(callback.ref);
            }
        }
        for (int ref : refs) {
            if (ref == LUA_NOREF) continue;
            lua.rawgeti(lua.state, LUA_REGISTRYINDEX, ref);
            int nargs = pushArgs(lua.state);
            PCall(nargs, 0);
        }
    }

    void CallWidgetCallback(const LuaWidget& w) {
        std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
        if (!lua.state || w.callbackRef == LUA_NOREF) return;
        lua.rawgeti(lua.state, LUA_REGISTRYINDEX, w.callbackRef);
        int nargs = 1;
        if (w.type == WidgetType::Checkbox) lua.pushboolean(lua.state, w.boolValue ? 1 : 0);
        else if (w.type == WidgetType::Slider) lua.pushnumber(lua.state, w.floatValue);
        else if (w.type == WidgetType::Combo) {
            lua.pushinteger(lua.state, w.intValue + 1);
            lua.pushstring(lua.state, w.items[w.intValue].c_str());
            nargs = 2;
        } else if (w.type == WidgetType::Button) nargs = 0;
        else lua.pushnil(lua.state);
        PCall(nargs, 0);
    }

    bool LoadLuaDll() {
        if (lua.module) return true;
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        std::vector<std::string> names = {
            (exeDir / "lua54.dll").string(),
            (exeDir / "lua53.dll").string(),
            "lua54.dll",
            "lua53.dll"
        };
        for (const auto& name : names) {
            lua.module = LoadLibraryA(name.c_str());
            if (lua.module) break;
        }
        if (!lua.module) {
            g_status = "Lua disabled: put lua54.dll near Zitrox.exe";
            return false;
        }

        auto load = [&](auto& fn, const char* name) -> bool {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(lua.module, name));
            return fn != nullptr;
        };

        bool ok = true;
        ok &= load(lua.L_newstate, "luaL_newstate");
        ok &= load(lua.L_openlibs, "luaL_openlibs");
        ok &= load(lua.close, "lua_close");
        ok &= load(lua.L_loadfilex, "luaL_loadfilex");
        ok &= load(lua.pcallk, "lua_pcallk");
        ok &= load(lua.createtable, "lua_createtable");
        ok &= load(lua.setglobal, "lua_setglobal");
        ok &= load(lua.getglobal, "lua_getglobal");
        ok &= load(lua.setfield, "lua_setfield");
        ok &= load(lua.getfield, "lua_getfield");
        ok &= load(lua.pushcclosure, "lua_pushcclosure");
        ok &= load(lua.pushnumber, "lua_pushnumber");
        ok &= load(lua.pushinteger, "lua_pushinteger");
        ok &= load(lua.pushboolean, "lua_pushboolean");
        ok &= load(lua.pushstring, "lua_pushstring");
        ok &= load(lua.pushnil, "lua_pushnil");
        ok &= load(lua.pushvalue, "lua_pushvalue");
        ok &= load(lua.tolstring, "lua_tolstring");
        ok &= load(lua.tonumberx, "lua_tonumberx");
        ok &= load(lua.tointegerx, "lua_tointegerx");
        ok &= load(lua.toboolean, "lua_toboolean");
        ok &= load(lua.type, "lua_type");
        ok &= load(lua.gettop, "lua_gettop");
        ok &= load(lua.settop, "lua_settop");
        ok &= load(lua.L_ref, "luaL_ref");
        ok &= load(lua.L_unref, "luaL_unref");
        ok &= load(lua.rawgeti, "lua_rawgeti");
        ok &= load(lua.rawseti, "lua_rawseti");
        ok &= load(lua.rawlen, "lua_rawlen");
        ok &= load(lua.next, "lua_next");

        if (!ok) {
            FreeLibrary(lua.module);
            lua = {};
            g_status = "Lua disabled: incompatible Lua DLL";
            return false;
        }
        return true;
    }

    std::filesystem::path ExeDirectory() {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        return std::filesystem::path(exePath).parent_path();
    }

    std::vector<std::filesystem::path> LuaDirectories() {
        std::vector<std::filesystem::path> dirs = {
            ExeDirectory() / "lua",
            std::filesystem::current_path() / "lua",
            std::filesystem::path("C:\\ZitFem\\lua")
        };
        std::vector<std::filesystem::path> unique;
        for (const auto& dir : dirs) {
            std::string dirText = ToLower(dir.string());
            bool duplicate = false;
            for (const auto& existing : unique) {
                if (ToLower(existing.string()) == dirText) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) unique.push_back(dir);
        }
        return unique;
    }

    void ScanScripts() {
        std::error_code ec;
        for (const auto& dir : LuaDirectories()) std::filesystem::create_directories(dir, ec);

        std::vector<LuaScript> found;
        for (const auto& dir : LuaDirectories()) {
            if (!std::filesystem::exists(dir, ec)) continue;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (ec || !entry.is_regular_file() || entry.path().extension() != ".lua") continue;
                LuaScript script;
                script.path = entry.path();
                script.name = entry.path().filename().string();
                auto old = std::find_if(g_scripts.begin(), g_scripts.end(), [&](const LuaScript& item) {
                    return ToLower(item.path.string()) == ToLower(script.path.string());
                });
                if (old != g_scripts.end()) {
                    script.id = old->id;
                    script.loaded = old->loaded;
                } else {
                    script.id = g_nextScriptId++;
                }
                bool duplicate = std::any_of(found.begin(), found.end(), [&](const LuaScript& item) {
                    return ToLower(item.path.string()) == ToLower(script.path.string());
                });
                if (!duplicate) found.push_back(script);
            }
        }
        std::sort(found.begin(), found.end(), [](const LuaScript& a, const LuaScript& b) {
            return ToLower(a.name) < ToLower(b.name);
        });
        g_scripts = found;
        if (g_selectedScriptIndex >= (int)g_scripts.size()) g_selectedScriptIndex = (int)g_scripts.size() - 1;
        if (g_selectedScriptIndex < 0) g_selectedScriptIndex = 0;
    }

    bool LoadScriptInternal(int scriptIndex) {
        if (!lua.state || scriptIndex < 0 || scriptIndex >= (int)g_scripts.size()) return false;
        RemoveScriptState(scriptIndex);
        std::string path = g_scripts[scriptIndex].path.string();
        int previousLoadingScript = g_loadingScriptIndex;
        int previousLoadingScriptId = g_loadingScriptId;
        g_loadingScriptIndex = scriptIndex;
        g_loadingScriptId = g_scripts[scriptIndex].id;
        if (lua.L_loadfilex(lua.state, path.c_str(), nullptr) != 0) {
            ReportError("Lua load error in " + g_scripts[scriptIndex].name);
            g_loadingScriptIndex = previousLoadingScript;
            g_loadingScriptId = previousLoadingScriptId;
            return false;
        }
        bool ok = PCall(0, 0);
        g_loadingScriptIndex = previousLoadingScript;
        g_loadingScriptId = previousLoadingScriptId;
        if (ok) {
            g_scripts[scriptIndex].loaded = true;
            g_status = "Lua loaded: " + g_scripts[scriptIndex].name;
            PushNotification(g_status, 0.65f, 0.9f, 0.25f, 2500);
        } else {
            RemoveScriptState(scriptIndex);
        }
        return ok;
    }
}

void Initialize() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (g_available) return;
    if (!LoadLuaDll()) return;
    lua.state = lua.L_newstate();
    if (!lua.state) {
        g_status = "Lua disabled: luaL_newstate failed";
        return;
    }
    lua.L_openlibs(lua.state);
    RegisterApi(lua.state);
    g_available = true;
    g_status = "Lua ready";
    RefreshScripts();
}

void Shutdown() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    ClearScriptState();
    if (lua.state) {
        lua.close(lua.state);
        lua.state = nullptr;
    }
    if (lua.module) {
        FreeLibrary(lua.module);
        lua.module = nullptr;
    }
    g_available = false;
    g_status = "Lua shutdown";
}

void ReloadScripts() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!lua.state) return;
    ClearScriptState();
    RegisterApi(lua.state);
    ScanScripts();
    int loaded = 0;
    for (int i = 0; i < (int)g_scripts.size(); i++) if (LoadScriptInternal(i)) loaded++;
    g_status = "Lua scripts loaded: " + std::to_string(loaded);
    if (loaded > 0) PushNotification(g_status, 0.65f, 0.9f, 0.25f, 2500);
}

void RefreshScripts() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    ScanScripts();
    g_status = "Lua scripts found: " + std::to_string(g_scripts.size());
}

bool LoadScript(int scriptIndex) {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!lua.state) return false;
    return LoadScriptInternal(scriptIndex);
}

bool UnloadScript(int scriptIndex) {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (scriptIndex < 0 || scriptIndex >= (int)g_scripts.size()) return false;
    RemoveScriptState(scriptIndex);
    g_status = "Lua unloaded: " + g_scripts[scriptIndex].name;
    PushNotification(g_status, 0.9f, 0.75f, 0.25f, 2500);
    return true;
}

void UnloadAllScripts() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    ClearScriptState();
    g_status = "Lua scripts unloaded";
    PushNotification(g_status, 0.9f, 0.75f, 0.25f, 2500);
}

void RunTick() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!g_available || !lua.state) return;
    ForEachCallback(g_tickCallbacks, [](lua_State*) { return 0; });
}

void RunRender() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!g_available || !lua.state) return;
    g_drawList = ImGui::GetBackgroundDrawList();
    ForEachCallback(g_renderCallbacks, [](lua_State*) { return 0; });
    g_drawList = nullptr;
}

void RunPlayerEsp(const LuaPlayerRenderInfo& info) {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!g_available || !lua.state) return;
    g_drawList = ImGui::GetBackgroundDrawList();
    ForEachCallback(g_playerEspCallbacks, [&](lua_State* L) {
        lua.createtable(L, 0, 9);
        lua.pushnumber(L, info.x); lua.setfield(L, -2, "x");
        lua.pushnumber(L, info.y); lua.setfield(L, -2, "y");
        lua.pushnumber(L, info.w); lua.setfield(L, -2, "w");
        lua.pushnumber(L, info.h); lua.setfield(L, -2, "h");
        lua.pushinteger(L, info.health); lua.setfield(L, -2, "health");
        lua.pushinteger(L, info.team); lua.setfield(L, -2, "team");
        lua.pushboolean(L, info.enemy ? 1 : 0); lua.setfield(L, -2, "enemy");
        lua.pushstring(L, info.name.c_str()); lua.setfield(L, -2, "name");
        lua.pushstring(L, info.weapon.c_str()); lua.setfield(L, -2, "weapon");
        return 1;
    });
    g_drawList = nullptr;
}

void RunMenuRender(float x, float y, float w, float h) {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!g_available || !lua.state) return;
    g_drawList = ImGui::GetBackgroundDrawList();
    ForEachCallback(g_menuRenderCallbacks, [&](lua_State* L) {
        lua.pushnumber(L, x);
        lua.pushnumber(L, y);
        lua.pushnumber(L, w);
        lua.pushnumber(L, h);
        return 4;
    });
    g_drawList = nullptr;
}

LuaMenuGlowStyle GetMenuGlowStyle() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_menuGlow;
}

void UpdateGameSnapshot(bool inGame, const LuaGamePlayerInfo& localPlayer, const std::vector<LuaGamePlayerInfo>& playerList) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_gameSnapshotInGame = inGame;
    g_localPlayer = localPlayer;
    g_gamePlayers = playerList;
}

void NotifyHit(const std::string& targetName, int damage, int remainingHp) {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!g_available || !lua.state) return;
    ForEachCallback(g_hitCallbacks, [&](lua_State* L) {
        lua.pushstring(L, targetName.c_str());
        lua.pushinteger(L, damage);
        lua.pushinteger(L, remainingHp);
        return 3;
    });
}

void NotifyRoundStart(int roundNumber) {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    if (!g_available || !lua.state) return;
    ForEachCallback(g_roundStartCallbacks, [&](lua_State* L) {
        lua.pushinteger(L, roundNumber);
        return 1;
    });
}

void RenderManager() {
    std::vector<LuaScript> scripts;
    int selected = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        scripts = g_scripts;
        selected = g_selectedScriptIndex;
    }

    gui::BeginSection("Lua Manager");
    ImGui::TextWrapped("%s", GetStatus());
    if (!IsAvailable()) {
        ImGui::TextWrapped("Put lua54.dll near Zitrox.exe to enable scripts.");
    }

    if (ImGui::Button("Refresh", ImVec2(ImGui::GetContentRegionAvail().x / 3.0f - 4.0f, 24.0f))) {
        RefreshScripts();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload all", ImVec2(ImGui::GetContentRegionAvail().x / 2.0f - 4.0f, 24.0f))) {
        ReloadScripts();
    }
    ImGui::SameLine();
    if (ImGui::Button("Unload all", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) {
        UnloadAllScripts();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Scripts:");
    ImGui::BeginChild("##lua_script_list", ImVec2(0, 180.0f), true);
    if (scripts.empty()) {
        ImGui::TextWrapped("No .lua files found.");
    } else {
        for (int i = 0; i < (int)scripts.size(); i++) {
            ImGui::PushID(i);
            std::string label = scripts[i].loaded ? "[loaded] " + scripts[i].name : "[off] " + scripts[i].name;
            if (ImGui::Selectable(label.c_str(), selected == i)) {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_selectedScriptIndex = i;
                selected = i;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (!scripts.empty() && selected >= 0 && selected < (int)scripts.size()) {
        ImGui::TextWrapped("Selected: %s", scripts[selected].name.c_str());
        if (ImGui::Button("Load selected", ImVec2(ImGui::GetContentRegionAvail().x / 2.0f - 4.0f, 24.0f))) {
            LoadScript(selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Unload selected", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) {
            UnloadScript(selected);
        }
        ImGui::TextWrapped("%s", scripts[selected].path.string().c_str());
    }
    gui::EndSection();

    gui::BeginSection("Lua API");
    ImGui::TextWrapped("ui.*, callbacks.*, settings.*, cheat.*, render.*, game.*, menu.* are available.");
    ImGui::TextWrapped("Scripts can create their own tabs after this Lua manager tab.");
    gui::EndSection();
}

void RenderMenu(int tabIndex) {
    if (tabIndex < 0) return;
    LuaTab tab;
    std::vector<LuaSection> sections;
    std::vector<LuaWidget> widgets;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (tabIndex >= (int)g_tabs.size()) return;
        tab = g_tabs[tabIndex];
        sections = g_sections;
        widgets = g_widgets;
    }

    if (tab.sectionIds.empty()) {
        gui::BeginSection("Lua");
        ImGui::TextWrapped("No Lua sections registered for this tab.");
        gui::EndSection();
        return;
    }

    ImGui::Columns(2, "##lua_cols", false);
    int rendered = 0;
    for (int sectionId : tab.sectionIds) {
        LuaSection* section = ById(sections, sectionId);
        if (!section) continue;
        if (rendered == (int)((tab.sectionIds.size() + 1) / 2)) ImGui::NextColumn();
        gui::BeginSection(section->name.c_str());
        for (int widgetId : section->widgetIds) {
            LuaWidget* w = ById(widgets, widgetId);
            if (!w) continue;
            ImGui::PushID(w->id);
            bool changed = false;
            if (w->type == WidgetType::Text) {
                ImGui::TextWrapped("%s", w->label.c_str());
            } else if (w->type == WidgetType::Checkbox) {
                bool value = w->boolValue;
                changed = gui::Checkbox(w->label.c_str(), &value);
                if (changed) {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (LuaWidget* live = ById(g_widgets, w->id)) live->boolValue = value;
                    w->boolValue = value;
                }
            } else if (w->type == WidgetType::Slider) {
                float value = w->floatValue;
                gui::Slider(w->label.c_str(), &value, w->minValue, w->maxValue);
                changed = std::fabs(value - w->floatValue) > 0.0001f;
                if (changed) {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (LuaWidget* live = ById(g_widgets, w->id)) live->floatValue = value;
                    w->floatValue = value;
                }
            } else if (w->type == WidgetType::Combo) {
                std::vector<const char*> itemPtrs;
                for (const auto& item : w->items) itemPtrs.push_back(item.c_str());
                int value = w->intValue;
                gui::ComboBox(w->label.c_str(), &value, itemPtrs.data(), (int)itemPtrs.size());
                changed = value != w->intValue;
                if (changed) {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (LuaWidget* live = ById(g_widgets, w->id)) live->intValue = value;
                    w->intValue = value;
                }
            } else if (w->type == WidgetType::Button) {
                changed = ImGui::Button(w->label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
            }
            ImGui::PopID();
            if (changed) CallWidgetCallback(*w);
        }
        gui::EndSection();
        rendered++;
    }
    ImGui::Columns(1);
}

int GetTabCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return (int)g_tabs.size();
}

const char* GetTabName(int tabIndex) {
    static thread_local std::string name;
    std::lock_guard<std::mutex> lock(g_mutex);
    name = (tabIndex >= 0 && tabIndex < (int)g_tabs.size()) ? g_tabs[tabIndex].name : "Lua";
    return name.c_str();
}

bool IsAvailable() {
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    return g_available;
}

const char* GetStatus() {
    static thread_local std::string status;
    std::lock_guard<std::recursive_mutex> luaLock(g_luaMutex);
    status = g_status;
    return status.c_str();
}
}
