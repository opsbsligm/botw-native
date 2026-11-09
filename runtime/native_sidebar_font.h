/* SPDX-License-Identifier: MIT */
#pragma once
#include "imgui.h"
#include <algorithm>

// Keep touch chrome readable independently of the legacy menu's font and DPI.
// Restore IO before PopFont, which recalculates the previous font's frame size.
class NdSidebarFont {
    float previous_scale;
public:
    explicit NdSidebarFont(ImFont *font,bool chinese=false) : previous_scale(ImGui::GetIO().FontGlobalScale) {
        if(!font) font=ImGui::GetFont();
        ImGuiIO &io=ImGui::GetIO();
        // The system CJK face has a smaller ink area than its nominal line size.
        float pixels=(chinese?32.f:24.f)*std::min(1.f,io.DisplaySize.y/720.f);
        io.FontGlobalScale=pixels/(font->FontSize*font->Scale);
        ImGui::PushFont(font);
    }
    ~NdSidebarFont() {
        ImGui::GetIO().FontGlobalScale=previous_scale;
        ImGui::PopFont();
    }
    NdSidebarFont(const NdSidebarFont &)=delete;
    NdSidebarFont &operator=(const NdSidebarFont &)=delete;
};
