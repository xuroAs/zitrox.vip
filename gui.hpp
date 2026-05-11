#pragma once
#include <d3d11.h>
#include "../imgui/imgui.h"

namespace gui {
    void Render();
    void SetupStyles();
    
    // Custom UI Components
    bool Checkbox(const char* label, bool* v);
    void Slider(const char* label, float* v, float min, float max);
    void ComboBox(const char* label, int* current_item, const char* const items[], int items_count);
    bool Tab(ID3D11ShaderResourceView* iconTexture, const char* icon, const char* label, bool active);
    
    void BeginSection(const char* label);
    void EndSection();
}
