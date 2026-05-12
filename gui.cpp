#define IMGUI_DEFINE_MATH_OPERATORS
#include "gui.hpp"
#include "LuaEngine.hpp"
#include "../include/Overlay.hpp"
#include "../include/Globals.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include "../imgui/imgui_internal.h"

#include <filesystem>
#include <fstream>
#include <windows.h>

extern Overlay overlay;

extern std::vector<std::string> g_hitSounds;
extern std::string g_hitSoundsDir;
void RefreshHitSounds();

namespace gui {
    // Custom colors
    ImVec4 pink = ImVec4(0.58f, 0.74f, 0.22f, 1.0f); // Changed to Zitrox Green (accent color)
    ImVec4 black = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
    ImVec4 dark_grey = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    ImVec4 light_bg = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
    float* current_section_height = nullptr;
    std::string current_section_resize_id;
    ImVec2 current_section_max;

    static float SafeContentWidth(float padding = 10.0f) {
        float width = ImGui::GetContentRegionAvail().x - padding;
        return width > 1.0f ? width : 1.0f;
    }

    void ApplyTheme() {
        if (settings.theme == 0) {
            pink = ImVec4(1.0f, 0.25f, 0.65f, 1.0f);
            black = ImVec4(0.035f, 0.025f, 0.035f, 1.0f);
            dark_grey = ImVec4(0.09f, 0.06f, 0.09f, 1.0f);
            light_bg = ImVec4(0.08f, 0.04f, 0.07f, 1.0f);
            UI::accentColor = pink;
            UI::bgColor = black;
            UI::frameColor = dark_grey;
            UI::groupColor = ImVec4(0.075f, 0.045f, 0.07f, 1.0f);
            UI::borderOuter = ImVec4(0.35f, 0.12f, 0.25f, 1.0f);
            UI::borderInner = ImVec4(0.14f, 0.06f, 0.11f, 1.0f);
        } else if (settings.theme == 1) {
            pink = ImVec4(0.58f, 0.74f, 0.22f, 1.0f);
            black = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
            dark_grey = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);
            light_bg = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
            UI::accentColor = pink;
            UI::bgColor = ImVec4(0.06f, 0.06f, 0.06f, 1.0f);
            UI::frameColor = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
            UI::groupColor = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
            UI::borderOuter = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
            UI::borderInner = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
        } else {
            // Custom theme
            pink = ImVec4(settings.customAccent[0], settings.customAccent[1], settings.customAccent[2], settings.customAccent[3]);
            black = ImVec4(settings.customBg[0], settings.customBg[1], settings.customBg[2], settings.customBg[3]);
            dark_grey = ImVec4(settings.customFrame[0] * 0.8f, settings.customFrame[1] * 0.8f, settings.customFrame[2] * 0.8f, settings.customFrame[3]);
            light_bg = ImVec4(settings.customFrame[0], settings.customFrame[1], settings.customFrame[2], settings.customFrame[3]);
            UI::accentColor = pink;
            UI::bgColor = black;
            UI::frameColor = dark_grey;
            UI::groupColor = ImVec4(dark_grey.x * 0.85f, dark_grey.y * 0.85f, dark_grey.z * 0.85f, 1.0f);
            UI::borderOuter = ImVec4(settings.customBorder[0], settings.customBorder[1], settings.customBorder[2], settings.customBorder[3]);
            UI::borderInner = ImVec4(dark_grey.x * 0.7f, dark_grey.y * 0.7f, dark_grey.z * 0.7f, 1.0f);
        }

        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Header] = dark_grey;
        colors[ImGuiCol_Button] = pink;
        colors[ImGuiCol_HeaderHovered] = ImVec4(pink.x, pink.y, pink.z, 0.8f);
        colors[ImGuiCol_HeaderActive] = pink;
        colors[ImGuiCol_ButtonHovered] = pink;
        colors[ImGuiCol_ButtonActive] = pink;
        colors[ImGuiCol_CheckMark] = pink;
        colors[ImGuiCol_SliderGrab] = pink;
        colors[ImGuiCol_SliderGrabActive] = pink;
        colors[ImGuiCol_ScrollbarGrab] = pink;
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(pink.x, pink.y, pink.z, 0.8f);
        colors[ImGuiCol_ScrollbarGrabActive] = pink;
        colors[ImGuiCol_FrameBg] = dark_grey;
        colors[ImGuiCol_FrameBgHovered] = light_bg;
        colors[ImGuiCol_FrameBgActive] = light_bg;
        colors[ImGuiCol_PopupBg] = black;
        colors[ImGuiCol_ResizeGrip] = ImVec4(pink.x, pink.y, pink.z, 0.35f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(pink.x, pink.y, pink.z, 0.75f);
        colors[ImGuiCol_ResizeGripActive] = pink;
    }

    void SetupStyles() {
        ApplyTheme();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsResizeFromEdges = false;

        auto& style = ImGui::GetStyle();
        style.WindowRounding = 12.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 8.0f;
        style.ChildRounding = 8.0f;
        style.GrabRounding = 4.0f;
        style.ItemSpacing = ImVec2(10, 10);
        style.WindowPadding = ImVec2(10, 10);
        style.ScrollbarSize = 3.0f;
        style.ScrollbarRounding = 12.0f;
        
        auto& colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
        colors[ImGuiCol_Header] = ImVec4(pink.x, pink.y, pink.z, 0.2f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(pink.x, pink.y, pink.z, 0.4f);
        colors[ImGuiCol_HeaderActive] = ImVec4(pink.x, pink.y, pink.z, 0.6f);
        colors[ImGuiCol_Button] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.13f, 0.13f, 0.13f, 1.0f);
        colors[ImGuiCol_CheckMark] = pink;
        colors[ImGuiCol_SliderGrab] = pink;
        colors[ImGuiCol_SliderGrabActive] = pink;
        colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        colors[ImGuiCol_ScrollbarGrab] = pink;
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(pink.x, pink.y, pink.z, 0.8f);
        colors[ImGuiCol_ScrollbarGrabActive] = pink;
    }

    bool Tab(ID3D11ShaderResourceView* iconTexture, const char* icon, const char* label, bool active) {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = ImGui::GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 36.0f); // Slightly taller tabs
        
        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(size, 0.0f);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        static std::map<ImGuiID, float> anim_map;
        float& anim = anim_map[id];
        anim = ImLerp(anim, active ? 1.0f : (hovered ? 0.3f : 0.0f), g.IO.DeltaTime * 12.0f);

        if (anim > 0.01f) {
            // Background glow/fill
            ImGui::GetWindowDrawList()->AddRectFilled(
                bb.Min + ImVec2(5, 2), bb.Max - ImVec2(5, 2), 
                ImGui::GetColorU32(ImVec4(pink.x, pink.y, pink.z, 0.08f * anim)), 
                6.0f
            );
            
            if (active) {
                // Modern indicator
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(bb.Min.x + 2, bb.Min.y + 10),
                    ImVec2(bb.Min.x + 4, bb.Max.y - 10),
                    ImGui::GetColorU32(pink),
                    2.0f
                );
            }
        }

        // Icon rendering
        if (iconTexture) {
            ImVec2 icon_min = bb.Min + ImVec2(9.0f, 8.0f);
            ImVec2 icon_max = icon_min + ImVec2(20.0f, 20.0f);
            ImVec4 tint = active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.55f, 0.55f, 0.55f, 0.85f);
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)iconTexture, icon_min, icon_max, ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(tint));
        } else if (icon && icon[0]) {
            ImGui::PushFont(overlay.IconFont);
            ImVec2 icon_size = ImGui::CalcTextSize(icon);
            ImU32 icon_col = active ? ImGui::GetColorU32(pink) : ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            // Draw icon centered in the left 40px area
            ImGui::GetWindowDrawList()->AddText(
                bb.Min + ImVec2(20.0f - icon_size.x * 0.5f, (size.y - icon_size.y) * 0.5f),
                icon_col,
                icon
            );
            ImGui::PopFont();
        }

        ImVec4 text_col = ImLerp(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(1, 1, 1, 1), anim);
        ImGui::PushStyleColor(ImGuiCol_Text, text_col);
        // Push text further to the right (40px) to avoid overlap
        ImGui::RenderTextClipped(bb.Min + ImVec2(40, 0), bb.Max, label, NULL, &label_size, ImVec2(0.0f, 0.5f), &bb);
        ImGui::PopStyleColor();

        return pressed;
    }

    bool SubTab(const char* label, bool active) {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = ImGui::GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size = ImVec2(label_size.x + style.FramePadding.x * 5.0f, 24.0f);
        
        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(size, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        static std::map<ImGuiID, float> anim_map;
        float& anim = anim_map[id];
        anim = ImLerp(anim, active ? 1.0f : (hovered ? 0.45f : 0.0f), g.IO.DeltaTime * 12.0f);

        ImU32 bg_col = ImGui::GetColorU32(ImVec4(pink.x, pink.y, pink.z, 0.12f * anim));
        ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, bg_col, 5.0f);

        ImVec4 text_col = ImLerp(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), ImVec4(1, 1, 1, 1), anim);
        ImGui::PushStyleColor(ImGuiCol_Text, text_col);
        ImGui::RenderTextClipped(bb.Min, bb.Max, label, NULL, &label_size, ImVec2(0.5f, 0.5f), &bb);
        ImGui::PopStyleColor();

        return pressed;
    }

    bool Checkbox(const char* label, bool* v) {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = ImGui::GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

        const float w = 34.0f;
        const float h = 18.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float full_width = SafeContentWidth();
        
        const ImRect total_bb(pos, pos + ImVec2(full_width, h));
        ImGui::ItemSize(total_bb, 0.0f);
        if (!ImGui::ItemAdd(total_bb, id))
            return false;

        ImGui::SetItemAllowOverlap(); // Allow elements like ColorPicker to work on the same line

        // Separate hitboxes for label and toggle to let middle elements (color picker) work
        ImVec2 check_pos = ImVec2(pos.x + full_width - w, pos.y);
        ImRect label_bb(pos, pos + ImVec2(label_size.x + 10.0f, h));
        ImRect toggle_bb(check_pos, check_pos + ImVec2(w, h));

        bool item_hovered = ImGui::IsItemHovered();
        bool hovered = item_hovered && (ImGui::IsMouseHoveringRect(label_bb.Min, label_bb.Max) || ImGui::IsMouseHoveringRect(toggle_bb.Min, toggle_bb.Max));
        
        // Only toggle if we are NOT hovering something else on the same line (like a color picker)
        bool pressed = false;
        if (hovered && ImGui::IsMouseClicked(0)) {
            if (!ImGui::IsAnyItemHovered() || ImGui::IsItemHovered()) { // Simple overlap check
                *v = !(*v);
                pressed = true;
            }
        }

        static std::map<ImGuiID, float> check_anims;
        float& anim = check_anims[id];
        anim = ImLerp(anim, *v ? 1.0f : 0.0f, g.IO.DeltaTime * 12.0f);

        ImU32 bg_col = ImGui::GetColorU32(ImLerp(ImVec4(0.12f, 0.12f, 0.12f, 1.0f), pink, anim));
        ImGui::GetWindowDrawList()->AddRectFilled(check_pos, check_pos + ImVec2(w, h), bg_col, h / 2.0f);
        
        float circle_pos = ImLerp(4.0f, w - 14.0f, anim);
        ImGui::GetWindowDrawList()->AddCircleFilled(check_pos + ImVec2(circle_pos + 5.0f, h / 2.0f), 6.0f, ImColor(255, 255, 255, 255), 16);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        // Clip text to avoid overlap with color pickers/toggles (reserve ~80px on the right)
        float maxTextWidth = full_width - 85.0f;
        ImGui::RenderTextClipped(total_bb.Min, total_bb.Min + ImVec2(maxTextWidth, h), label, NULL, &label_size, ImVec2(0.0f, 0.5f), &total_bb);
        ImGui::PopStyleColor();

        return pressed;
    }

    bool YellowCheckbox(const char* label, bool* v) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.12f, 1.0f));
        bool pressed = Checkbox(label, v);
        ImGui::PopStyleColor();
        return pressed;
    }

    const char* KeyName(int key) {
        static char name[32];
        if (key == 0) return "";
        if (key == VK_LBUTTON) return "m1";
        if (key == VK_RBUTTON) return "m2";
        if (key == VK_MBUTTON) return "m3";
        if (key == VK_XBUTTON1) return "m4";
        if (key == VK_XBUTTON2) return "m5";
        if (key >= 'A' && key <= 'Z') {
            name[0] = (char)tolower(key);
            name[1] = '\0';
            return name;
        }
        if (key >= '0' && key <= '9') {
            name[0] = (char)key;
            name[1] = '\0';
            return name;
        }
        LONG scan = MapVirtualKeyA(key, MAPVK_VK_TO_VSC) << 16;
        if (GetKeyNameTextA(scan, name, sizeof(name)) > 0) {
            std::transform(name, name + strlen(name), name, [](unsigned char c) { return (char)tolower(c); });
            return name;
        }
        sprintf_s(name, "0x%X", key);
        return name;
    }

    bool BindButton(const char* id, KeyBind& bind, bool* value) {
        ImGuiContext& g = *GImGui;
        char text[40];
        if (bind.key == 0) sprintf_s(text, "none");
        else sprintf_s(text, "%s", KeyName(bind.key));

        ImGui::PushID(&bind);
        ImVec2 size(54.0f, 18.0f);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        
        const ImRect bb(pos, pos + size);
        ImGui::ItemSize(size, 0.0f);
        if (!ImGui::ItemAdd(bb, ImGui::GetID(id))) {
            ImGui::PopID();
            return false;
        }

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, ImGui::GetID(id), &hovered, &held);

        if (pressed) {
            settings.isListeningForKey = true;
            settings.pendingBindVar = &bind;
            settings.bindingLabel = id;
            settings.lastBindTime = std::chrono::steady_clock::now();
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("bind_mode_ctx");

        // Draw background
        ImU32 bg_col = ImGui::GetColorU32(hovered ? ImVec4(0.18f, 0.18f, 0.18f, 1.0f) : ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, bg_col, 4.0f);
        ImGui::GetWindowDrawList()->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.05f)), 4.0f);

        // Text
        ImGui::PushFont(overlay.MinecraftFontSmall);
        const char* display_text = settings.isListeningForKey && settings.pendingBindVar == &bind ? "..." : text;
        ImVec2 text_size = ImGui::CalcTextSize(display_text);
        ImGui::GetWindowDrawList()->AddText(
            bb.Min + ImVec2((size.x - text_size.x) * 0.5f, (size.y - text_size.y) * 0.5f),
            ImGui::GetColorU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f)),
            display_text
        );
        ImGui::PopFont();

        // Popup logic (keep existing)
        static std::unordered_map<uintptr_t, float> s_bindModePopAnim;
        uintptr_t bindKey = reinterpret_cast<uintptr_t>(&bind);
        if (ImGui::BeginPopup("bind_mode_ctx")) {
            float& a = s_bindModePopAnim[bindKey];
            a = ImLerp(a, 1.0f, ImGui::GetIO().DeltaTime * 18.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
            auto pickMode = [&](const char* label, BindMode m) {
                if (ImGui::Selectable(label, bind.mode == m)) {
                    bind.mode = m;
                    if (value && m == ALWAYS_ON) *value = true;
                    ImGui::CloseCurrentPopup();
                }
            };
            pickMode("Hold", HOLD);
            pickMode("Toggle", TOGGLE);
            pickMode("Always", ALWAYS_ON);
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        } else { s_bindModePopAnim[bindKey] = 0.0f; }

        if (settings.isListeningForKey && settings.pendingBindVar == &bind) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - settings.lastBindTime).count();
            if (elapsed > 180) {
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                    bind.key = 0; bind.active = false;
                    settings.isListeningForKey = false; settings.pendingBindVar = nullptr;
                } else {
                    for (int key = 1; key < 256; key++) {
                        if (key == VK_ESCAPE || key == VK_INSERT) continue;
                        if (GetAsyncKeyState(key) & 0x8000) {
                            bind.key = key; bind.active = value ? *value : false;
                            settings.isListeningForKey = false; settings.pendingBindVar = nullptr;
                            break;
                        }
                    }
                }
            }
        }
        ImGui::PopID();
        return pressed;
    }

    bool CheckboxBind(const char* label, bool* v, KeyBind& bind, bool yellowText = false) {
        float bindWidth = 52.0f;
        float toggleWidth = 40.0f;
        float startX = ImGui::GetCursorPosX();
        float avail = SafeContentWidth();
        
        if (yellowText) YellowCheckbox(label, v);
        else Checkbox(label, v);
        
        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + avail - bindWidth - toggleWidth); // Move bind further left
        ImGui::PushID(label);
        BindButton("##bind", bind, v);
        ImGui::PopID();
        if (!settings.isListeningForKey || settings.pendingBindVar != &bind) bind.active = *v;
        return true;
    }

    void ColorPicker(const char* id, float* col) {
        float avail = SafeContentWidth();
        float btnSize = 18.0f;
        float offsetFromRight = 68.0f; // Positioned before the checkbox toggle
        
        ImGui::SameLine();
        float startX = ImGui::GetCursorPosX();
        // Move cursor to the desired position relative to the content width
        ImGui::SetCursorPosX(startX + avail - offsetFromRight - btnSize);
        
        ImGuiColorEditFlags flags =
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_AlphaPreviewHalf |
            ImGuiColorEditFlags_AlphaBar;
        
        ImGui::PushID(id);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(18, 18);
        
        if (ImGui::ColorButton("##colbtn", ImVec4(col[0], col[1], col[2], col[3]),
            ImGuiColorEditFlags_AlphaPreviewHalf, size))
        {
            ImGui::OpenPopup("##colpicker_popup");
        }

        // Circular styling over the button
        ImGui::GetWindowDrawList()->AddCircleFilled(pos + ImVec2(9, 9), 9.5f, ImGui::GetColorU32(UI::bgColor), 16);
        ImGui::GetWindowDrawList()->AddCircleFilled(pos + ImVec2(9, 9), 8.0f, ImGui::GetColorU32(ImVec4(col[0], col[1], col[2], col[3])), 16);
        ImGui::GetWindowDrawList()->AddCircle(pos + ImVec2(9, 9), 8.5f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.1f)), 16, 1.0f);
        
        if (ImGui::BeginPopup("##colpicker_popup")) {
            ImGui::ColorPicker4("##picker", col, flags);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    static void GrenadePerTypeColorsPopup() {
        ImGuiColorEditFlags flags =
            ImGuiColorEditFlags_NoInputs |
            ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_AlphaPreviewHalf |
            ImGuiColorEditFlags_AlphaBar;
        static const uint16_t kGrenadeWeaponIds[6] = { 44, 43, 45, 46, 47, 48 };
        static const char* kGrenadeNames[6] = { "HE", "Flashbang", "Smoke", "Molotov", "Decoy", "Incendiary" };

        if (ImGui::BeginPopup("grenade_type_colors")) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
            ImFont* weaponIconFont = overlay.IconFont ? overlay.IconFont : ImGui::GetFont();
            for (int i = 0; i < 6; i++) {
                ImGui::PushID(i);
                ImGui::PushFont(weaponIconFont);
                const char* icon = GetWeaponIcon(kGrenadeWeaponIds[i]);
                ImGui::Text("%s", icon && icon[0] ? icon : "?");
                ImGui::PopFont();
                ImGui::SameLine(32.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", kGrenadeNames[i]);
                ImGui::SameLine(150.0f);
                float* col = settings.grenadeTypeColors[i];
                if (ImGui::ColorButton("##grnd_type_btn", ImVec4(col[0], col[1], col[2], col[3]),
                        ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(20, 20)))
                    ImGui::OpenPopup("##grnd_type_pick");
                if (ImGui::BeginPopup("##grnd_type_pick")) {
                    ImGui::ColorPicker4("##grnd_type_picker", col, flags);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopStyleVar();
            ImGui::EndPopup();
        }
    }

    void KeyBindLine(const char* label, KeyBind& bind) {
        float bindWidth = 52.0f;
        float startX = ImGui::GetCursorPosX();
        float avail = SafeContentWidth();
        ImGui::Text("%s", label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(startX + avail - bindWidth);
        ImGui::PushID(label);
        BindButton("##bind", bind, nullptr);
        ImGui::PopID();
    }

    void Slider(const char* label, float* v, float min, float max) {
        ImGuiContext& g = *GImGui;
        const ImGuiID id = ImGui::GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float width = SafeContentWidth();
        float height = 28.0f; 
        const ImRect bb(pos, pos + ImVec2(width, height));
        
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
            return;

        bool hovered, held;
        ImGui::ButtonBehavior(bb, id, &hovered, &held);

        const float track_padding = 5.0f;
        float track_min = bb.Min.x + track_padding;
        float track_max = bb.Max.x - track_padding;
        float track_width = track_max - track_min;
        if (track_width < 1.0f) track_width = 1.0f;

        if (held) {
            *v = min + (ImGui::GetIO().MousePos.x - track_min) / track_width * (max - min);
            if (*v < min) *v = min;
            if (*v > max) *v = max;
        }

        static std::map<ImGuiID, float> slider_anims;
        float& anim = slider_anims[id];
        anim = ImLerp(anim, (hovered || held) ? 1.0f : 0.0f, g.IO.DeltaTime * 10.0f);

        float line_y = bb.Min.y + 22.0f;
        float grab_pos = (*v - min) / (max - min) * track_width;
        
        // Background line
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(track_min, line_y), ImVec2(track_max, line_y + 2.0f), ImColor(30, 30, 30, 255), 1.0f);
        // Active line
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(track_min, line_y), ImVec2(track_min + grab_pos, line_y + 2.0f), ImGui::GetColorU32(pink), 1.0f);
        // Grab circle
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(track_min + grab_pos, line_y + 1.0f), 4.0f + anim * 2.0f, ImGui::GetColorU32(pink), 16);

        // Label and value
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::RenderText(ImVec2(bb.Min.x, bb.Min.y), label);
        
        char val_buf[16]; sprintf_s(val_buf, "%.1f", *v);
        ImVec2 val_size = ImGui::CalcTextSize(val_buf);
        ImGui::RenderText(ImVec2(bb.Max.x - val_size.x, bb.Min.y), val_buf);
        ImGui::PopStyleColor();
    }

    void ComboBox(const char* label, int* current_item, const char* const items[], int items_count) {
        ImGuiContext& g = *GImGui;
        const ImGuiID id = ImGui::GetID(label);
        if (*current_item < 0 || *current_item >= items_count) *current_item = 0;
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::Text(label);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

        ImGui::SetNextItemWidth(SafeContentWidth());
        if (ImGui::BeginCombo(("##" + std::string(label)).c_str(), items[*current_item], ImGuiComboFlags_None)) {
            for (int i = 0; i < items_count; i++) {
                bool is_selected = (*current_item == i);
                if (ImGui::Selectable(items[i], is_selected))
                    *current_item = i;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::Spacing();
    }

    // Глобальная карта высот секций (доступна для save/load)
    std::map<std::string, float> g_sectionHeights;

    void BeginSection(const char* label) {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float width = ImGui::GetColumnWidth() - ImGui::GetStyle().ItemSpacing.x * 2.0f;
        if (width < 120.0f) width = ImGui::GetContentRegionAvail().x;
        std::string section_id = std::to_string(settings.currentTab) + "_" + label;
        float& height = g_sectionHeights[section_id];
        current_section_height = &height;
        current_section_resize_id = "##section_resize_" + section_id;
        current_section_max = pos + ImVec2(width, height);
        
        if (height < 60.0f) {
            height = 320.0f; // Increased default height for better initial look
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Modern Card Style
        draw_list->AddRectFilled(pos, pos + ImVec2(width, height), ImColor(18, 18, 18, 255), 8.0f);
        draw_list->AddRect(pos, pos + ImVec2(width, height), ImColor(35, 35, 35, 255), 8.0f, 0, 1.0f);
        
        // Section Title (Modern)
        ImGui::SetCursorScreenPos(pos + ImVec2(12.0f, 10.0f));
        ImGui::PushFont(overlay.MenuFont);
        ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), label);
        ImGui::PopFont();

        ImGui::SetCursorScreenPos(pos + ImVec2(0, 35.0f));
        ImGui::BeginChild(label, ImVec2(width, height - 40.0f), false, ImGuiWindowFlags_NoBackground);
        ImGui::Indent(12.0f);
    }

    void EndSection() {
        ImGui::Unindent(12.0f);
        ImGui::EndChild();
        
        ImVec2 childMin = ImGui::GetItemRectMin();
        ImVec2 childMax = current_section_max;
        
        // Угловой grip 14x14 в правом нижнем углу
        const float gripSize = 14.0f;
        ImVec2 gripMin = ImVec2(childMax.x - gripSize, childMax.y - gripSize);
        ImVec2 gripMax = childMax;

        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImGui::SetCursorScreenPos(gripMin);
        ImGui::InvisibleButton(current_section_resize_id.c_str(), ImVec2(gripSize, gripSize));

        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        
        ImU32 gripCol = ImGui::GetColorU32(ImVec4(pink.x, pink.y, pink.z, (hovered || active) ? 1.0f : 0.45f));
        draw->AddLine(childMax - ImVec2(12, 4), childMax - ImVec2(4, 12), gripCol, 1.5f);
        draw->AddLine(childMax - ImVec2(8, 4), childMax - ImVec2(4, 8), gripCol, 1.5f);
        
        if (active && current_section_height) {
            *current_section_height += ImGui::GetIO().MouseDelta.y;
            if (*current_section_height < 90.0f) *current_section_height = 90.0f;
            if (*current_section_height > 600.0f) *current_section_height = 600.0f;
        }
        
        ImGui::SetCursorScreenPos(ImVec2(childMin.x, childMax.y + 8.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        
        current_section_height = nullptr;
        current_section_resize_id.clear();
        ImGui::PopStyleVar(2);
    }

    void RenderHitMarkerEditor() {
        if (!settings.misc.customHitMarkerEditorOpen) return;
        
        ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, UI::bgColor);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, UI::bgColor);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UI::accentColor);
        ImGui::PushStyleColor(ImGuiCol_Border, UI::borderOuter);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
        
        if (!ImGui::Begin("Hit Marker Editor", &settings.misc.customHitMarkerEditorOpen, ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            return;
        }
        
        ImGui::BeginChild("##hm_tools", ImVec2(170, 0), true);
        ImGui::TextColored(UI::accentColor, "Tools");
        ImGui::Separator();
        
        if (ImGui::Button("+ Line", ImVec2(-1, 0))) {
            HitMarkerShape s; s.type = HMS_LINE;
            s.p1[0] = -10; s.p1[1] = -10;
            s.p2[0] = -3; s.p2[1] = -3;
            g_hmShapes.push_back(s);
            g_hmSelected = (int)g_hmShapes.size() - 1;
        }
        if (ImGui::Button("+ Rectangle", ImVec2(-1, 0))) {
            HitMarkerShape s; s.type = HMS_RECT;
            s.p1[0] = -8; s.p1[1] = -8;
            s.p2[0] = 8; s.p2[1] = 8;
            g_hmShapes.push_back(s);
            g_hmSelected = (int)g_hmShapes.size() - 1;
        }
        if (ImGui::Button("+ Triangle", ImVec2(-1, 0))) {
            HitMarkerShape s; s.type = HMS_TRIANGLE;
            s.p1[0] = 0; s.p1[1] = -10;
            s.p2[0] = -8; s.p2[1] = 6;
            s.p3[0] = 8; s.p3[1] = 6;
            g_hmShapes.push_back(s);
            g_hmSelected = (int)g_hmShapes.size() - 1;
        }
        
        ImGui::Separator();
        
        bool hasSel = (g_hmSelected >= 0 && g_hmSelected < (int)g_hmShapes.size());
        if (ImGui::Button("Delete Selected", ImVec2(-1, 0)) && hasSel) {
            g_hmShapes.erase(g_hmShapes.begin() + g_hmSelected);
            g_hmSelected = -1;
            hasSel = false;
        }
        if (ImGui::Button("Clear All", ImVec2(-1, 0))) {
            g_hmShapes.clear();
            g_hmSelected = -1;
            hasSel = false;
        }
        if (ImGui::Button("Default Cross", ImVec2(-1, 0))) {
            g_hmShapes.clear();
            float gap = 4, len = 6;
            for (int i = 0; i < 4; i++) {
                HitMarkerShape s; s.type = HMS_LINE;
                s.color[0] = 1; s.color[1] = 1; s.color[2] = 1; s.color[3] = 1;
                s.thickness = 1.5f;
                if (i == 0) { s.p1[0] = -gap - len; s.p1[1] = -gap - len; s.p2[0] = -gap; s.p2[1] = -gap; }
                else if (i == 1) { s.p1[0] = gap; s.p1[1] = -gap; s.p2[0] = gap + len; s.p2[1] = -gap - len; }
                else if (i == 2) { s.p1[0] = -gap - len; s.p1[1] = gap + len; s.p2[0] = -gap; s.p2[1] = gap; }
                else { s.p1[0] = gap; s.p1[1] = gap; s.p2[0] = gap + len; s.p2[1] = gap + len; }
                g_hmShapes.push_back(s);
            }
        }
        
        ImGui::Separator();
        ImGui::TextColored(UI::accentColor, "Shapes (%d)", (int)g_hmShapes.size());
        ImGui::BeginChild("##hm_list", ImVec2(0, 120), true);
        for (int i = 0; i < (int)g_hmShapes.size(); i++) {
            const char* tname = g_hmShapes[i].type == HMS_LINE ? "Line" : g_hmShapes[i].type == HMS_RECT ? "Rect" : "Tri";
            char lbl[32]; sprintf_s(lbl, "%d. %s", i, tname);
            if (ImGui::Selectable(lbl, g_hmSelected == i)) g_hmSelected = i;
        }
        ImGui::EndChild();
        
        if (hasSel) {
            ImGui::Separator();
            ImGui::TextColored(UI::accentColor, "Properties");
            HitMarkerShape& s = g_hmShapes[g_hmSelected];
            ImGui::ColorEdit4("Color", s.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SliderFloat("Thick", &s.thickness, 0.5f, 6.0f, "%.1f");
            if (s.type != HMS_LINE) ImGui::Checkbox("Filled", &s.filled);
            ImGui::SliderFloat2("P1", s.p1, -40, 40);
            ImGui::SliderFloat2("P2", s.p2, -40, 40);
            if (s.type == HMS_TRIANGLE) ImGui::SliderFloat2("P3", s.p3, -40, 40);
        }
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Canvas
        ImGui::BeginChild("##hm_canvas", ImVec2(0, 0), true);
        ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
        ImVec2 center(canvasMin.x + canvasSize.x * 0.5f, canvasMin.y + canvasSize.y * 0.5f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        
        // фон под игру
        dl->AddRectFilled(canvasMin, canvasMax, ImColor(28, 28, 32, 255));
        // grid
        for (float x = canvasMin.x; x < canvasMax.x; x += 16) {
            dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), ImColor(50, 50, 55, 80));
        }
        for (float y = canvasMin.y; y < canvasMax.y; y += 16) {
            dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), ImColor(50, 50, 55, 80));
        }
        // оси через центр
        dl->AddLine(ImVec2(center.x, canvasMin.y), ImVec2(center.x, canvasMax.y), ImColor(80, 80, 90, 140));
        dl->AddLine(ImVec2(canvasMin.x, center.y), ImVec2(canvasMax.x, center.y), ImColor(80, 80, 90, 140));
        
        const float scale = 4.0f;
        
        // отрисовка фигур
        for (int i = 0; i < (int)g_hmShapes.size(); i++) {
            const auto& s = g_hmShapes[i];
            ImColor c = ImColor(s.color[0], s.color[1], s.color[2], s.color[3]);
            float th = s.thickness * scale * 0.5f;
            if (th < 1.0f) th = 1.0f;
            if (s.type == HMS_LINE) {
                dl->AddLine(ImVec2(center.x + s.p1[0] * scale, center.y + s.p1[1] * scale),
                            ImVec2(center.x + s.p2[0] * scale, center.y + s.p2[1] * scale), c, th);
            } else if (s.type == HMS_RECT) {
                ImVec2 a(center.x + s.p1[0] * scale, center.y + s.p1[1] * scale);
                ImVec2 b(center.x + s.p2[0] * scale, center.y + s.p2[1] * scale);
                if (s.filled) dl->AddRectFilled(a, b, c);
                else dl->AddRect(a, b, c, 0, 0, th);
            } else if (s.type == HMS_TRIANGLE) {
                ImVec2 a(center.x + s.p1[0] * scale, center.y + s.p1[1] * scale);
                ImVec2 b(center.x + s.p2[0] * scale, center.y + s.p2[1] * scale);
                ImVec2 d(center.x + s.p3[0] * scale, center.y + s.p3[1] * scale);
                if (s.filled) dl->AddTriangleFilled(a, b, d, c);
                else dl->AddTriangle(a, b, d, c, th);
            }
            
            if (i == g_hmSelected) {
                ImVec2 p1(center.x + s.p1[0] * scale, center.y + s.p1[1] * scale);
                ImVec2 p2(center.x + s.p2[0] * scale, center.y + s.p2[1] * scale);
                dl->AddCircleFilled(p1, 4.0f, ImColor(80, 220, 80, 255));
                dl->AddCircleFilled(p2, 4.0f, ImColor(220, 80, 80, 255));
                if (s.type == HMS_TRIANGLE) {
                    ImVec2 p3(center.x + s.p3[0] * scale, center.y + s.p3[1] * scale);
                    dl->AddCircleFilled(p3, 4.0f, ImColor(80, 80, 220, 255));
                }
            }
        }
        
        // Drag-логика
        ImGui::InvisibleButton("##canvas_btn", canvasSize);
        if (ImGui::IsItemActive() && hasSel) {
            ImVec2 mouse = ImGui::GetMousePos();
            float relX = (mouse.x - center.x) / scale;
            float relY = (mouse.y - center.y) / scale;
            
            HitMarkerShape& s = g_hmShapes[g_hmSelected];
            static int dragHandle = -1; // 0=p1, 1=p2, 2=p3, 3=center
            if (ImGui::IsItemActivated()) {
                ImVec2 p1(center.x + s.p1[0] * scale, center.y + s.p1[1] * scale);
                ImVec2 p2(center.x + s.p2[0] * scale, center.y + s.p2[1] * scale);
                float d1 = (mouse.x - p1.x) * (mouse.x - p1.x) + (mouse.y - p1.y) * (mouse.y - p1.y);
                float d2 = (mouse.x - p2.x) * (mouse.x - p2.x) + (mouse.y - p2.y) * (mouse.y - p2.y);
                float dMin = d1; dragHandle = 0;
                if (d2 < dMin) { dMin = d2; dragHandle = 1; }
                if (s.type == HMS_TRIANGLE) {
                    ImVec2 p3v(center.x + s.p3[0] * scale, center.y + s.p3[1] * scale);
                    float d3 = (mouse.x - p3v.x) * (mouse.x - p3v.x) + (mouse.y - p3v.y) * (mouse.y - p3v.y);
                    if (d3 < dMin) { dMin = d3; dragHandle = 2; }
                }
                if (dMin > 100.0f) dragHandle = 3;
            }
            
            if (dragHandle == 0) { s.p1[0] = relX; s.p1[1] = relY; }
            else if (dragHandle == 1) { s.p2[0] = relX; s.p2[1] = relY; }
            else if (dragHandle == 2) { s.p3[0] = relX; s.p3[1] = relY; }
            else if (dragHandle == 3) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                s.p1[0] += delta.x / scale; s.p1[1] += delta.y / scale;
                s.p2[0] += delta.x / scale; s.p2[1] += delta.y / scale;
                if (s.type == HMS_TRIANGLE) { s.p3[0] += delta.x / scale; s.p3[1] += delta.y / scale; }
            }
        }
        
        // подсказка
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + 8, canvasMin.y + 8));
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Canvas (4x scale). Drag handles to edit. Drag empty to move shape.");
        
        ImGui::EndChild();
        
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }
    
    void Render() {
        if (!settings.menuOpen) {
            static float alpha = 0.0f;
            alpha = 0.0f;
            return;
        }

        static float menuAlpha = 0.0f;
        menuAlpha = ImLerp(menuAlpha, 1.0f, ImGui::GetIO().DeltaTime * 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, menuAlpha);

        ApplyTheme();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 400), ImVec2(1920, 1080));
        
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, ImVec4(0, 0, 0, 0));

        const char* mainTitle = settings.theme == 0 ? "Femboyy.meow" : (settings.theme == 1 ? "Zitrox.vip" : settings.menuTitle);
        ImGui::Begin(mainTitle, &settings.menuOpen, ImGuiWindowFlags_NoTitleBar);

        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        LuaMenuGlowStyle luaGlow = LuaEngine::GetMenuGlowStyle();
        
        // Background & Border & Shadows
        // Fake drop shadow
        draw_list->AddRectFilled(window_pos - ImVec2(1, -5), window_pos + window_size + ImVec2(5, 5), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.35f)), 10.0f);
        if (luaGlow.enabled) {
            ImVec2 expand(luaGlow.thickness, luaGlow.thickness);
            draw_list->AddRect(
                window_pos - expand,
                window_pos + window_size + expand,
                ImGui::GetColorU32(ImVec4(luaGlow.r, luaGlow.g, luaGlow.b, luaGlow.a)),
                luaGlow.rounding,
                0,
                luaGlow.thickness
            );
        }
        
        draw_list->AddRectFilled(window_pos, window_pos + window_size, ImGui::GetColorU32(UI::bgColor), 10.0f);
        draw_list->AddRect(window_pos, window_pos + window_size, ImGui::GetColorU32(UI::borderOuter), 10.0f, 0, 1.5f);
        
        // Sidebar Background
        float sidebar_width = 160.0f;
        draw_list->AddRectFilled(window_pos, ImVec2(window_pos.x + sidebar_width, window_pos.y + window_size.y), ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.08f, 1.0f)), 10.0f, ImDrawFlags_RoundCornersLeft);
        draw_list->AddLine(ImVec2(window_pos.x + sidebar_width, window_pos.y), ImVec2(window_pos.x + sidebar_width, window_pos.y + window_size.y), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.05f)));

        ImGui::PopStyleColor(3);

        // Sidebar Content
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::BeginChild("##sidebar", ImVec2(sidebar_width, 0), false, ImGuiWindowFlags_NoBackground);
        
        // Logo/Title in Sidebar
        ImGui::SetCursorPosY(25.0f);
        ImGui::PushFont(overlay.MenuFont);
        if (settings.theme == 0) {
            float title_x = (sidebar_width - ImGui::CalcTextSize("Femboyy").x) * 0.5f;
            ImGui::SetCursorPosX(title_x);
            ImGui::TextColored(pink, "Femboyy");
        } else if (settings.theme == 1) {
            float title_x = (sidebar_width - ImGui::CalcTextSize("ZITROX").x) * 0.5f;
            ImGui::SetCursorPosX(title_x);
            ImGui::TextColored(pink, "ZITROX");
        } else {
            float title_x = (sidebar_width - ImGui::CalcTextSize(settings.menuTitle).x) * 0.5f;
            ImGui::SetCursorPosX(title_x);
            ImGui::TextColored(pink, "%s", settings.menuTitle);
        }
        ImGui::PopFont();

        ImGui::SetCursorPosY(80.0f);
        const char* tabs[] = { "Combat", "Visuals", "Misc", "Grenades", "Configs", "Lua" };
        ID3D11ShaderResourceView* tabIcons[] = { 
            overlay.TabCombatIcon, 
            overlay.TabVisualsIcon, 
            overlay.TabMiscIcon, 
            overlay.TabGrenadesIcon, 
            overlay.TabConfigsIcon,
            overlay.TabLuaIcon
        };
        const char* icons[] = { "A", "B", "C", "D", "E", "L" }; // Placeholder icons if IconFont supports them
        
        static int prevTab = settings.currentTab;
        static std::chrono::steady_clock::time_point tabChangeTime = std::chrono::steady_clock::now();
        
        for (int i = 0; i < 6; i++) {
            ImGui::SetCursorPosX(10.0f);
            
            bool clicked = Tab(tabIcons[i], icons[i], tabs[i], settings.currentTab == i);
            
            if (clicked && settings.currentTab != i) {
                prevTab = settings.currentTab;
                settings.currentTab = i;
                tabChangeTime = std::chrono::steady_clock::now();
            }
            ImGui::Spacing();
        }
        int luaTabCount = LuaEngine::GetTabCount();
        for (int i = 0; i < luaTabCount; i++) {
            ImGui::SetCursorPosX(10.0f);
            int tabId = 6 + i;
            bool clicked = Tab(overlay.TabLuaIcon, "L", LuaEngine::GetTabName(i), settings.currentTab == tabId);
            if (clicked && settings.currentTab != tabId) {
                prevTab = settings.currentTab;
                settings.currentTab = tabId;
                tabChangeTime = std::chrono::steady_clock::now();
            }
            ImGui::Spacing();
        }
        ImGui::EndChild();

        // Анимация табов: alpha fade + лёгкое смещение
        float tabAnim = 1.0f;
        float slideOffset = 0.0f;
        {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - tabChangeTime).count();
            const float animDuration = 0.25f;
            if (elapsed < animDuration) {
                float t = elapsed / animDuration;
                // ease out cubic
                float eased = 1.0f - std::pow(1.0f - t, 3.0f);
                tabAnim = eased;
                int dir = (settings.currentTab > prevTab) ? 1 : -1;
                slideOffset = (1.0f - eased) * 30.0f * dir;
            }
        }

        // Main Content Area
        ImGui::SetCursorPos(ImVec2(sidebar_width + 15 + slideOffset, 15));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tabAnim);
        ImGui::BeginChild("##content", ImVec2(window_size.x - sidebar_width - 30, window_size.y - 30), false, ImGuiWindowFlags_NoBackground);
        
        if (settings.currentTab == 0) { // Combat
            ImGui::Columns(2, "##combat_cols", false);
            BeginSection("General");
                CheckboxBind("Enable Aimbot", &settings.aim.enabled, settings.aim.enabledBind);
                Checkbox("Auto Pistol", &settings.aim.autoPistol);
                CheckboxBind("Chicken Aimbot", &settings.aim.chickenAimbot, settings.aim.chickenAimbotBind, true);
                Checkbox("Sniper Crosshair", &settings.misc.sniperCrosshair);
                Checkbox("Dynamic FOV (Scope)", &settings.aimExtra.dynamicFov);
                Checkbox("Auto Wall", &settings.aimExtra.autoWall);
                if (settings.aimExtra.autoWall) {
                    Slider("AW Min Dmg", &settings.aimExtra.autoWallMinDmg, 5.0f, 100.0f);
                }
            EndSection();
            BeginSection("Triggerbot");
                CheckboxBind("Enable Triggerbot", &settings.trigger.enabled, settings.trigger.hotkey);
                Slider("Delay (ms)", &settings.trigger.delay, 0.0f, 200.0f);
                Checkbox("Hitbox Filter", &settings.trigger.hitboxFilter);
                if (settings.trigger.hitboxFilter) {
                    if (ImGui::BeginCombo("##trigHB", "Hitboxes...")) {
                        ImGui::CheckboxFlags("Head", &settings.trigger.hbFlags, 1);
                        ImGui::CheckboxFlags("Neck", &settings.trigger.hbFlags, 2);
                        ImGui::CheckboxFlags("Chest", &settings.trigger.hbFlags, 4);
                        ImGui::CheckboxFlags("Arms", &settings.trigger.hbFlags, 8);
                        ImGui::CheckboxFlags("Legs", &settings.trigger.hbFlags, 16);
                        ImGui::EndCombo();
                    }
                }
                Checkbox("Burst Mode", &settings.trigger.burstMode);
                if (settings.trigger.burstMode) {
                    ImGui::SliderInt("Burst Count", &settings.trigger.burstCount, 2, 6);
                }
                Checkbox("Wallbang Mode", &settings.trigger.wallbang);
                if (settings.trigger.wallbang) {
                    Slider("Wallbang Tolerance", &settings.trigger.wallbangTolerance, 0.5f, 4.0f);
                }
            EndSection();
            ImGui::NextColumn();
            BeginSection("Weapon Settings");
                static int currentSpecificWeapon = 0;
                WeaponConfig* cfg = nullptr;

                if (currentSpecificWeapon > 0) {
                    cfg = &settings.aim.specific[currentSpecificWeapon];
                } else {
                    cfg = (settings.currentWeaponTab == 0) ? &settings.aim.rifle : (settings.currentWeaponTab == 1 ? &settings.aim.pistol : &settings.aim.sniper);
                }

                const char* wTabs[] = { "Rifle", "Pistol", "Sniper" };
                static std::unordered_map<int, float> s_specTabPopAnim;
                for (int i = 0; i < 3; i++) {
                    if (SubTab(wTabs[i], settings.currentWeaponTab == i && currentSpecificWeapon == 0)) {
                        settings.currentWeaponTab = i;
                        currentSpecificWeapon = 0;
                    }

                    if (ImGui::BeginPopupContextItem(wTabs[i])) {
                        float& popA = s_specTabPopAnim[i];
                        popA = ImLerp(popA, 1.0f, ImGui::GetIO().DeltaTime * 16.0f);
                        float p = 8.0f + (1.0f - popA) * 8.0f;
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popA);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(p, p));
                        ImGui::TextColored(pink, "Specific %s", wTabs[i]);
                        ImGui::Separator();
                        
                        std::vector<std::pair<int, const char*>> weps;
                        if (i == 0) {
                            weps = { {7, "AK-47"}, {16, "M4A4"}, {60, "M4A1-S"}, {10, "Famas"}, {13, "Galil"}, {8, "AUG"}, {39, "SG553"} };
                        } else if (i == 1) {
                            weps = { {4, "Glock"}, {61, "USP-S"}, {32, "P2000"}, {36, "P250"}, {3, "Five-SeveN"}, {30, "Tec-9"}, {63, "CZ75"}, {1, "Deagle"}, {64, "Revolver"}, {2, "Dualies"} };
                        } else if (i == 2) {
                            weps = { {9, "AWP"}, {40, "SSG08"}, {38, "SCAR-20"}, {11, "G3SG1"} };
                        }

                        ImFont* weaponIconFont = overlay.IconFont ? overlay.IconFont : ImGui::GetFont();
                        for (const auto& w : weps) {
                            ImGui::PushID(w.first);
                            bool isSelected = (currentSpecificWeapon == w.first);
                            ImGui::BeginGroup();

                            const char* icon = GetWeaponIcon(static_cast<uint16_t>(w.first));
                            if (!icon || !icon[0]) icon = "?";

                            ImGui::PushFont(weaponIconFont);
                            ImVec2 iconSz = ImGui::CalcTextSize(icon);
                            const float iconPad = 5.0f;
                            ImVec2 iconBtnSize(iconSz.x + iconPad * 2.0f, iconSz.y + iconPad * 2.0f);

                            bool iconPressed = ImGui::InvisibleButton("##wep_icon", iconBtnSize);
                            const ImVec2 iconMin = ImGui::GetItemRectMin();
                            if (isSelected) {
                                ImGui::GetWindowDrawList()->AddRect(
                                    iconMin - ImVec2(1, 1),
                                    ImGui::GetItemRectMax() + ImVec2(1, 1),
                                    ImGui::GetColorU32(ImVec4(pink.x, pink.y, pink.z, 1.0f)),
                                    2.0f, 0, 1.5f);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            ImGui::GetWindowDrawList()->AddText(
                                ImVec2(iconMin.x + iconPad, iconMin.y + iconPad),
                                ImGui::GetColorU32(ImGuiCol_Text),
                                icon);
                            ImGui::PopFont();

                            if (iconPressed) {
                                currentSpecificWeapon = w.first;
                                settings.currentWeaponTab = i;
                            }

                            ImGui::SameLine(0.0f, 10.0f);
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(w.second);

                            ImGui::EndGroup();
                            ImGui::PopID();
                        }
                        ImGui::PopStyleVar(2);
                        ImGui::EndPopup();
                    } else {
                        s_specTabPopAnim[i] = 0.0f;
                    }

                    if (i < 2) ImGui::SameLine();
                }
                ImGui::Spacing();

                if (currentSpecificWeapon > 0) {
                    const char* wName = "Unknown";
                    switch (currentSpecificWeapon) {
                        case 1: wName = "Deagle"; break; case 2: wName = "Dualies"; break; case 3: wName = "Five-Seven"; break; case 4: wName = "Glock"; break;
                        case 7: wName = "AK-47"; break; case 8: wName = "AUG"; break; case 9: wName = "AWP"; break; case 10: wName = "Famas"; break;
                        case 11: wName = "G3SG1"; break; case 13: wName = "Galil"; break; case 14: wName = "M249"; break; case 16: wName = "M4A4"; break;
                        case 17: wName = "Mac-10"; break; case 19: wName = "P90"; break; case 23: wName = "MP5-SD"; break; case 24: wName = "UMP-45"; break;
                        case 25: wName = "XM1014"; break; case 26: wName = "Bizon"; break; case 27: wName = "Mag-7"; break; case 28: wName = "Negev"; break;
                        case 29: wName = "Sawed-Off"; break; case 30: wName = "Tec-9"; break; case 32: wName = "P2000"; break; case 33: wName = "MP7"; break;
                        case 34: wName = "MP9"; break; case 35: wName = "Nova"; break; case 36: wName = "P250"; break; case 38: wName = "SCAR-20"; break;
                        case 39: wName = "SG553"; break; case 40: wName = "SSG08"; break; case 60: wName = "M4A1-S"; break; case 61: wName = "USP-S"; break;
                        case 63: wName = "CZ75"; break; case 64: wName = "Revolver"; break;
                    }
                    {
                        ImFont* weaponIconFont = overlay.IconFont ? overlay.IconFont : ImGui::GetFont();
                        ImGui::BeginGroup();
                        ImGui::PushFont(weaponIconFont);
                        const char* wIcon = GetWeaponIcon(static_cast<uint16_t>(currentSpecificWeapon));
                        ImGui::TextColored(pink, "%s", wIcon && wIcon[0] ? wIcon : "?");
                        ImGui::PopFont();
                        ImGui::SameLine(0.0f, 8.0f);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextColored(pink, "Editing: %s", wName);
                        ImGui::EndGroup();
                    }
                    {
                        bool prevOn = settings.aim.useSpecific[currentSpecificWeapon];
                        Checkbox("Enable Specific Config", &settings.aim.useSpecific[currentSpecificWeapon]);
                        if (settings.aim.useSpecific[currentSpecificWeapon] && !prevOn && cfg) {
                            WeaponConfig* base = GetBaseWeaponConfigForWeaponId(static_cast<uint16_t>(currentSpecificWeapon));
                            if (base) *cfg = *base;
                        }
                    }
                    ImGui::Separator();
                    if (!settings.aim.useSpecific[currentSpecificWeapon]) {
                        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                    }
                }

                KeyBindLine("Aim Key", cfg->aimKey);
                Slider("Field of View", &cfg->fov, 0.0f, 30.0f);
                Slider("Smooth Speed", &cfg->smooth, 1.0f, 20.0f);
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::TextUnformatted("Hitboxes");
                ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(SafeContentWidth());
                if (ImGui::BeginCombo("##hitboxes", "Select...")) {
                    ImGui::CheckboxFlags("Head", &cfg->hitboxes, 1);
                    ImGui::CheckboxFlags("Neck", &cfg->hitboxes, 2);
                    ImGui::CheckboxFlags("Chest/Stomach", &cfg->hitboxes, 4);
                    ImGui::CheckboxFlags("Arms", &cfg->hitboxes, 8);
                    ImGui::CheckboxFlags("Legs", &cfg->hitboxes, 16);
                    ImGui::EndCombo();
                }

                Checkbox("Check Wall", &cfg->checkWall);
                Checkbox("Check Smoke", &cfg->checkSmoke);
                Checkbox("Check Flash", &cfg->checkFlash);
                Checkbox("Check Jump", &cfg->checkJump);
                Checkbox("Team Check", &cfg->teamCheck);

                {
                    const bool sniperCtx =
                        (currentSpecificWeapon == 0 && settings.currentWeaponTab == 2) ||
                        (currentSpecificWeapon > 0 && IsSniperWeapon(static_cast<uint16_t>(currentSpecificWeapon)));
                    if (sniperCtx)
                        Checkbox("Auto scope (sniper)", &cfg->autoScope);
                }
                Checkbox("Auto stop", &cfg->autoStop);
                
                ImGui::Separator();
                Checkbox("Enable RCS", &cfg->rcs);
                if (!cfg->rcs) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                }
                Slider("RCS X", &cfg->rcsX, 0.0f, 2.0f);
                Slider("RCS Y", &cfg->rcsY, 0.0f, 2.0f);
                if (!cfg->rcs) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }

                if (currentSpecificWeapon > 0 && !settings.aim.useSpecific[currentSpecificWeapon]) {
                    ImGui::PopItemFlag();
                    ImGui::PopStyleVar();
                }
            EndSection();
            ImGui::Columns(1);
        }
        else if (settings.currentTab == 1) { // Visuals
            ImGui::Columns(2, "##visuals_cols", false);
            BeginSection("Players");
                Checkbox("Enable Visuals", &settings.visuals.enabled);
                Checkbox("Team Check", &settings.visuals.teamCheck);
                
                Checkbox("Box ESP", &settings.visuals.boxes);
                ColorPicker("##boxCol", settings.visuals.boxColor);
                
                const char* boxStyles[] = { "Full", "Corner", "Dynamic" };
                ComboBox("Box Style", &settings.visuals.boxStyle, boxStyles, 3);
                
                Checkbox("Name ESP", &settings.visuals.names);
                ColorPicker("##nameCol", settings.visuals.nameColor);
                
                Checkbox("Health Bar", &settings.visuals.health);
                ColorPicker("##healthCol", settings.visuals.healthColor);
                
                Checkbox("Ammo Bar", &settings.visuals.ammoBar);
                ColorPicker("##ammoCol", settings.visuals.ammoColor);
                
                Checkbox("Ammo Text", &settings.visuals.ammoText);
                
                Checkbox("Skeleton ESP", &settings.visuals.skeleton);
                ColorPicker("##skelCol", settings.visuals.skeletonColor);
                
                Checkbox("Glow ESP", &settings.visuals.glow);
                ColorPicker("##glowCol", settings.visuals.glowColor);
                if (settings.visuals.glow) Slider("Glow Intensity", &settings.visuals.glowIntensity, 0.2f, 2.5f);
                
                Checkbox("Active Weapon", &settings.visuals.activeWeapon);
                ColorPicker("##wepCol", settings.visuals.weaponColor);
                
                Checkbox("Weapon Icons", &settings.visuals.iconESP);
                
                Checkbox("Headshot Indicator", &settings.visuals.headshotIndicator);
                ColorPicker("##hsCol", settings.visuals.headshotColor);
                
                Checkbox("Defuse Kit ESP", &settings.visuals.defuseKitESP);
                Checkbox("Money Reveal", &settings.visuals.moneyReveal);
                Checkbox("Rank Reveal", &settings.visuals.rankReveal);
            EndSection();
            ImGui::NextColumn();
            BeginSection("World");
                Checkbox("Chicken ESP", &settings.visuals.chickenEsp);
                ColorPicker("##chickCol", settings.visuals.chickenColor);
                
                Checkbox("Dropped Weapons", &settings.visuals.droppedWeapons);
                ColorPicker("##dropCol", settings.visuals.droppedWeaponColor);
                Checkbox("Grenade Trajectory", &settings.visuals.grenadePrediction);
                Checkbox("Grenade World ESP", &settings.visuals.grenadeWorldEsp);
                
                {
                    float avail = SafeContentWidth();
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float h = 22.0f;
                    // Clip text to reserve space for buttons on the right
                    ImGui::RenderTextClipped(pos, pos + ImVec2(avail - 85.0f, h), "Grenade colors (trajectory + world)", NULL, NULL, ImVec2(0.0f, 0.5f));
                    ImGui::ItemSize(ImVec2(avail, h));
                    ImGui::SetCursorScreenPos(pos); // Reset for SameLine
                }
                
                ImGui::SameLine();
                {
                    ImGuiColorEditFlags gfl =
                        ImGuiColorEditFlags_NoInputs |
                        ImGuiColorEditFlags_NoLabel |
                        ImGuiColorEditFlags_AlphaPreviewHalf |
                        ImGuiColorEditFlags_AlphaBar;
                    float* leg = settings.visuals.grenadeColor;
                    float rowLeft = ImGui::GetCursorPosX();
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(rowLeft + avail - 46.0f);
                    if (ImGui::Button("...", ImVec2(22.0f, 22.0f)))
                        ImGui::OpenPopup("grenade_type_colors");
                    ImGui::SameLine(0.0f, 4.0f);
                    ImGui::PushID("grenade_legacy_col");
                    if (ImGui::ColorButton("##legbtn", ImVec4(leg[0], leg[1], leg[2], leg[3]),
                            ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(20, 20)))
                        ImGui::OpenPopup("##legpick");
                    if (ImGui::BeginPopup("##legpick")) {
                        if (ImGui::ColorPicker4("##legpicker", leg, gfl)) {
                            for (int gi = 0; gi < 6; gi++) {
                                settings.grenadeTypeColors[gi][0] = leg[0];
                                settings.grenadeTypeColors[gi][1] = leg[1];
                                settings.grenadeTypeColors[gi][2] = leg[2];
                                settings.grenadeTypeColors[gi][3] = leg[3];
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                GrenadePerTypeColorsPopup();
                Checkbox("Bomb ESP", &settings.visuals.bombESP);
                ColorPicker("##bombCol", settings.visuals.bombColor);
                Checkbox("No Flash", &settings.misc.noFlash);
                Checkbox("Radar Hack (In-game)", &settings.visuals.radarHack);
                Checkbox("Overlay Radar Window", &settings.visuals.overlayRadar);
                Checkbox("Spectator List", &settings.visuals.spectatorList);
                Slider("Radar Zoom", &settings.visuals.radarRange, 300.0f, 3000.0f);
                
                ImGui::Separator();
                Checkbox("Out of FOV Arrows", &settings.visuals.outOfFovArrows);
                ColorPicker("##arrCol", settings.visuals.arrowsColor);
                if (settings.visuals.outOfFovArrows) {
                    Slider("Arrows Radius", &settings.visuals.arrowsRadius, 60.0f, 400.0f);
                    Slider("Arrows Size", &settings.visuals.arrowsSize, 5.0f, 30.0f);
                }
            EndSection();
            ImGui::Columns(1);
        }
        else if (settings.currentTab == 2) { // Misc
            ImGui::Columns(2, "##misc_cols", false);
            BeginSection("Movement");
                Checkbox("Bunnyhop", &settings.misc.bhop);
            EndSection();
            BeginSection("Other");
                KeyBindLine("Menu Toggle", settings.misc.menuKey);
                KeyBindLine("Panic Key", settings.misc.panicKey);
                Checkbox("Auto Zeus", &settings.misc.zeusBot);
                Checkbox("Auto Knife", &settings.misc.knifeBot);
                Checkbox("Auto Accept", &settings.misc.autoAccept);
                Checkbox("Keybinds Overlay", &settings.misc.keybindsOverlay);
                Checkbox("Spread Circle", &settings.misc.spreadCircle);
                if (settings.misc.spreadCircle) {
                    ColorPicker("##spreadCol", settings.misc.spreadColor);
                    Slider("Spread Scale", &settings.misc.spreadScale, 100.0f, 1500.0f);
                }
            EndSection();
            ImGui::NextColumn();
            BeginSection("Hit Feedback");
                Checkbox("Hit Marker", &settings.misc.hitMarker);
                ColorPicker("##hmCol", settings.misc.hitMarkerColor);
                Checkbox("Hit Logger", &settings.misc.hitLogger);
                if (settings.misc.hitLogger) {
                    if (ImGui::Button("Reset position", ImVec2(ImGui::GetContentRegionAvail().x, 22))) {
                        settings.misc.hitLogAnchorX = -1.0f;
                        settings.misc.hitLogAnchorY = -1.0f;
                    }
                }
                
                Checkbox("Custom Hit Marker", &settings.misc.customHitMarker);
                if (settings.misc.customHitMarker) {
                    if (ImGui::Button("Open Editor", ImVec2(ImGui::GetContentRegionAvail().x, 22))) {
                        settings.misc.customHitMarkerEditorOpen = true;
                    }
                }
                
                ImGui::Separator();
                Checkbox("Hit Sound", &settings.misc.hitSound);
                
                if (ImGui::Button("Refresh Sounds", ImVec2(ImGui::GetContentRegionAvail().x, 22))) {
                    RefreshHitSounds();
                }
                
                if (g_hitSounds.empty()) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No .wav in C:\\ZitFem\\sounds");
                } else {
                    if (settings.misc.hitSoundIndex >= (int)g_hitSounds.size()) settings.misc.hitSoundIndex = 0;
                    if (ImGui::BeginCombo("##hitsndcombo", g_hitSounds[settings.misc.hitSoundIndex].c_str())) {
                        for (int i = 0; i < (int)g_hitSounds.size(); i++) {
                            bool sel = (settings.misc.hitSoundIndex == i);
                            if (ImGui::Selectable(g_hitSounds[i].c_str(), sel)) settings.misc.hitSoundIndex = i;
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                
                ImGui::TextWrapped("Drop .wav files into C:\\ZitFem\\sounds and click Refresh");
            EndSection();
            ImGui::Columns(1);
        }
        else if (settings.currentTab == 3) { // Grenades
            BeginSection("Grenade Helper");
                Checkbox("Enabled", &settings.grenadeHelper.enabled);
                Checkbox("Draw world overlay", &settings.grenadeHelper.drawOverlay);
                const char* ghMaps[] = { "de_dust2", "de_mirage" };
                ComboBox("Map", &settings.grenadeHelper.mapIndex, ghMaps, 2);
                const char* ghModes[] = { "Stand + throw", "Jump then throw", "Jump + throw (quick)", "Run + throw" };
                ComboBox("Throw mode (when recording)", &settings.grenadeHelper.recordMode, ghModes, 4);
                Slider("Position tolerance", &settings.grenadeHelper.posTolerance, 16.0f, 80.0f);
                ImGui::SliderInt("Jump-then-throw delay (ms)", &settings.grenadeHelper.jumpThrowDelayMs, 40, 280);
                ImGui::SliderInt("Run + throw hold W (ms)", &settings.grenadeHelper.runThrowPreMs, 40, 240);
                const char* ghStandRing[] = { "2D (flat ring)", "3D (cylinder)" };
                ComboBox("Stand circle", &settings.grenadeHelper.standCircleMode, ghStandRing, 2);
                Slider("Stand ring radius", &settings.grenadeHelper.standRingRadius, 12.0f, 120.0f);
                ImGui::TextUnformatted("Show on map:");
                const char* ghOvGt[] = { "HE", "Flash", "Smoke", "Molly", "Decoy", "Inc" };
                for (int gi = 0; gi < 6; gi++) {
                    ImGui::PushID(gi);
                    bool on = (settings.grenadeHelper.overlayGrenadeMask & (1u << gi)) != 0;
                    if (ImGui::Checkbox(ghOvGt[gi], &on)) {
                        if (on) settings.grenadeHelper.overlayGrenadeMask |= (1u << gi);
                        else settings.grenadeHelper.overlayGrenadeMask &= ~(1u << gi);
                    }
                    if ((gi + 1) % 3 != 0) ImGui::SameLine();
                    ImGui::PopID();
                }
                ImGui::InputText("Lineup name", settings.grenadeHelper.newLineupName, IM_ARRAYSIZE(settings.grenadeHelper.newLineupName));
                KeyBindLine("Record (hold: do lineup, LMB throw, release)", settings.grenadeHelper.recordBind);
                KeyBindLine("Play selected", settings.grenadeHelper.playBind);
                ImGui::Separator();
                if (ImGui::Button("Save lineups", ImVec2(ImGui::GetContentRegionAvail().x / 2 - 4, 24))) SaveGrenadeLineupsFile();
                ImGui::SameLine();
                if (ImGui::Button("Reload file", ImVec2(ImGui::GetContentRegionAvail().x, 24))) LoadGrenadeLineupsFile();
                ImGui::TextUnformatted("Lineups on this map:");
                ImGui::BeginChild("##ghspots", ImVec2(0, 140), true);
                for (size_t i = 0; i < g_grenadeLineups.size(); i++) {
                    const auto& sp = g_grenadeLineups[i];
                    if (sp.mapId != (uint32_t)settings.grenadeHelper.mapIndex) continue;
                    ImGui::PushID((int)i);
                    bool sel = settings.grenadeHelper.selectedGlobalIndex == (int)i;
                    char row[80];
                    if (sp.grenadeType < 6u)
                        sprintf_s(row, "%s  [%s]", sp.name, ghOvGt[sp.grenadeType]);
                    else
                        sprintf_s(row, "%s  [?]", sp.name);
                    if (ImGui::Selectable(row, sel)) settings.grenadeHelper.selectedGlobalIndex = (int)i;
                    ImGui::PopID();
                }
                ImGui::EndChild();
                if (ImGui::Button("Delete selected", ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
                    int idx = settings.grenadeHelper.selectedGlobalIndex;
                    if (idx >= 0 && (size_t)idx < g_grenadeLineups.size()) {
                        g_grenadeLineups.erase(g_grenadeLineups.begin() + idx);
                        settings.grenadeHelper.selectedGlobalIndex = -1;
                        SaveGrenadeLineupsFile();
                    }
                }
            EndSection();
        }
        else if (settings.currentTab == 4) { // Configs
            BeginSection("Config Management");
                static char configName[64] = "";
                ImGui::InputText("##configname", configName, IM_ARRAYSIZE(configName));
                
                if (ImGui::Button("Save", ImVec2(ImGui::GetContentRegionAvail().x / 2 - 4, 24))) {
                    if (strlen(configName) > 0) SaveConfig(configName);
                }
                ImGui::SameLine();
                if (ImGui::Button("Load", ImVec2(ImGui::GetContentRegionAvail().x, 24))) {
                    if (strlen(configName) > 0) LoadConfig(configName);
                }
                
                ImGui::Separator();
                ImGui::Text("Saved Configs:");
                ImGui::BeginChild("##configlist", ImVec2(0, 100), true);
                std::vector<std::string> configs = GetConfigs();
                for (const auto& cfg : configs) {
                    if (ImGui::Selectable(cfg.c_str(), strcmp(configName, cfg.c_str()) == 0)) {
                        strcpy_s(configName, sizeof(configName), cfg.c_str());
                    }
                }
                ImGui::EndChild();
            EndSection();

            BeginSection("Themes");
                const char* themes[] = { "Femboy.meow", "Zitrox.vip", "Custom" };
                ComboBox("Theme", &settings.theme, themes, 3);
                Checkbox("Watermark", &settings.misc.watermark);
                const char* watermarkStyles[] = { "Old", "New" };
                ComboBox("Watermark Style", &settings.misc.watermarkStyle, watermarkStyles, 2);
                
                if (settings.theme == 2) {
                    ImGui::Separator();
                    ImGui::TextColored(UI::accentColor, "Custom Theme");
                    ImGui::InputText("Menu Title", settings.menuTitle, sizeof(settings.menuTitle));
                    ImGui::ColorEdit4("Accent", settings.customAccent, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Background", settings.customBg, ImGuiColorEditFlags_NoInputs);
                    ImGui::ColorEdit4("Frame", settings.customFrame, ImGuiColorEditFlags_NoInputs);
                    ImGui::ColorEdit4("Border", settings.customBorder, ImGuiColorEditFlags_NoInputs);
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("Unload Cheat", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
                    settings.requestUnload = true;
                }
                ImGui::PopStyleColor(3);
            EndSection();
        }
        else if (settings.currentTab == 5) {
            LuaEngine::RenderManager();
        }
        else if (settings.currentTab >= 6) {
            LuaEngine::RenderMenu(settings.currentTab - 6);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(); // tab fade alpha

        if (settings.isListeningForKey) {
            ImVec2 overlayMin = window_pos;
            ImVec2 overlayMax = window_pos + window_size;
            draw_list->AddRectFilled(overlayMin, overlayMax, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.62f)), 8.0f);
            draw_list->AddRect(overlayMin + ImVec2(1, 1), overlayMax - ImVec2(1, 1), ImGui::GetColorU32(ImVec4(pink.x, pink.y, pink.z, 0.8f)), 8.0f, 0, 1.5f);
            const char* bindText = "press to bind";
            ImVec2 textSize = ImGui::CalcTextSize(bindText);
            ImVec2 center(window_pos.x + window_size.x * 0.5f, window_pos.y + window_size.y * 0.5f);
            draw_list->AddText(ImVec2(center.x - textSize.x * 0.5f + 1, center.y - textSize.y * 0.5f + 1), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.9f)), bindText);
            draw_list->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), bindText);
        }

        LuaEngine::RunMenuRender(window_pos.x, window_pos.y, window_size.x, window_size.y);

        ImGui::End();
        
        ImGui::PopStyleVar(); // Balanced for menu alpha animation
        
        RenderHitMarkerEditor();
    }
}
