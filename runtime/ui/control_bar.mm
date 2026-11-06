// A persistent handle and a compact panel, rendered in the game's own swapchain.
// Position and hit bounds are shared without reading ImGui from the SDL thread.
#include "imgui.h"
#include "../native_amiibo_ui.h"
#include "../native_atmosphere_ui.h"
#include "control_bar.h"
#include "i18n.h"
#include <atomic>
#include <string.h>
#include <math.h>
#include <mutex>
extern "C" {
#include "../settings.h"
#include "../motion.h"
#include "../framepace.h"
#include "../native_cheats.h"
#include "../nvn/output_scale.h"
void input_reset(void);
void menu_request_window_size_apply(void);
}

static std::atomic<bool> expanded{false};
static std::mutex bounds_lock;
static ImVec2 lower, upper;
static bool moving_panel;
static const ImVec2 anchors[] = {{0,0}, {.5f,0}, {1,0}, {1,.5f}, {1,1}, {.5f,1}, {0,1}, {0,.5f}};

bool control_bar_is_open(void) { return expanded.load(); }
bool control_bar_hit(float x, float y) {
    std::lock_guard<std::mutex> lock(bounds_lock);
    return x >= lower.x && y >= lower.y && x < upper.x && y < upper.y;
}
void control_bar_toggle(void) {
    menu_absorb_pad_buttons();
    expanded = !expanded.load();
    input_reset();
}

void control_bar_draw(float scale) {
    ImGuiIO &io = ImGui::GetIO();
    bool open = expanded.load();
    int anchor = g_settings.control_bar_anchor;
    if (anchor < 0 || anchor >= 8) anchor = 4;
    static int previous_anchor = -1;
    static bool previous_open;
    if (!moving_panel || anchor != previous_anchor || open != previous_open) {
        ImVec2 p = anchors[anchor];
        ImGui::SetNextWindowPos(ImVec2(12*scale + p.x*(io.DisplaySize.x-24*scale),
                                      12*scale + p.y*(io.DisplaySize.y-24*scale)), ImGuiCond_Always, p);
        previous_anchor = anchor; previous_open = open;
    }
    ImGui::SetNextWindowBgAlpha(.96f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(open ? 340*scale : 0, 0), ImVec2(io.DisplaySize.x-12, io.DisplaySize.y-12));
    ImGui::Begin("##native-control-bar", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
    bool dirty = false;
    if (ImGui::Button(open ? tr("Collapse  [-]") : tr("Settings  [+]"), ImVec2(130*scale, 40*scale))) {
        control_bar_toggle();
        if (open) settings_save();
    }
    if (open) {
        ImGui::SameLine(); ImGui::TextUnformatted(tr("NATIVE / DECK"));
        ImGui::Separator();
        ImGui::TextUnformatted(tr("A: confirm    B: back"));
        if(ImGui::BeginTabBar("##feature-tabs")) {
            if(ImGui::BeginTabItem(tr("Display"))) {
                static const int rates[] = {30, 40, 45, 60, 90, 120, 0};
                const char *names[] = {"30", "40", "45", "60", "90", "120", tr("Unlimited")};
                int rate = 3;
                for (int i=0; i<7; ++i) if (rates[i] == framepace_configured_limit()) rate=i;
                if (ImGui::Combo(tr("Frame cap"), &rate, names, 7)) { framepace_set_user_limit(rates[rate]); dirty=true; }
                static const int widths[] = {960, 1280, 1280, 1920};
                static const int heights[] = {540, 720, 800, 1080};
                const char *sizes[] = {"960 x 540", "1280 x 720", "1280 x 800", "1920 x 1080"};
                int size=2;
                for(int i=0;i<4;++i) if(g_settings.window_w==widths[i] && g_settings.window_h==heights[i]) size=i;
                if(ImGui::Combo(tr("Window resolution"), &size, sizes, 4)) {
                    g_settings.window_w=widths[size]; g_settings.window_h=heights[size];
                    menu_request_window_size_apply(); dirty=true;
                }
                const char *aa[] = {"Off", "SMAA", "TAA"};
                dirty |= ImGui::Combo(tr("Anti-aliasing"), &g_settings.antialias, aa, 3);
                const int methods[] = {UPSCALE_OFF, UPSCALE_FSR1, UPSCALE_FSR3};
                const char *upscalers[] = {tr("Native"), "AMD FSR 1", tr("AMD FSR 3.1 (experimental)")};
                int current=0;
                for(int i=0;i<3;++i) if(g_settings.upscaler==methods[i]) current=i;
                if(ImGui::BeginCombo(tr("Upscaling"), upscalers[current])) {
                    for(int i=0;i<3;++i) {
                        bool enabled=upscaler_available(methods[i]);
                        ImGui::BeginDisabled(!enabled);
                        if(ImGui::Selectable(upscalers[i], current==i)) { g_settings.upscaler=methods[i]; dirty=true; }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                const char *quality[]={"Ultra Quality (1.3x)","Quality (1.5x)","Balanced (1.7x)","Performance (2x)","Ultra Performance (3x)"};
                ImGui::BeginDisabled(g_settings.upscaler==UPSCALE_OFF || !upscale_modes_available());
                dirty |= ImGui::Combo(tr("Upscaling quality"),&g_settings.upscale_quality,quality,5);
                ImGui::EndDisabled();
                dirty |= ImGui::SliderInt(tr("Sharpening"),&g_settings.sharpening,0,10);
                ImGui::TextDisabled("%s", tr("FSR 4: unavailable on this Deck backend"));
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(tr("Controls"))) {
                dirty |= ImGui::SliderFloat(tr("Motion sensitivity"), &g_settings.motion_sens, .1f, 3.f, "%.1fx");
                ImGui::TextDisabled("%s", motion_has_sensors() ? tr("Motion: native sensor connected") : tr("Motion: no sensor available"));
                if(ImGui::BeginCombo(tr("Dock"), tr("Choose an edge or corner"))) {
                    const char *positions[]={tr("Top left"),tr("Top"),tr("Top right"),tr("Right"),tr("Bottom right"),tr("Bottom"),tr("Bottom left"),tr("Left")};
                    for(int i=0;i<8;++i) if(ImGui::Selectable(positions[i], anchor==i)) { g_settings.control_bar_anchor=i; dirty=true; }
                    ImGui::EndCombo();
                }
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem(tr("Extensions"))) {
                nd_draw_amiibo_library("botw-native",!strncmp(ui_lang(),"zh",2));
                nd_draw_atmosphere("botw-native",!strncmp(ui_lang(),"zh",2));
                if(ImGui::TreeNode(tr("Advanced data patches"))) {
                    if(!nd_cheats_count()) ImGui::TextDisabled("%s", tr("No compatible presets imported"));
                    for(size_t i=0;i<nd_cheats_count();i++) {
                        bool on=nd_cheats_enabled(i)!=0;
                        if(ImGui::Checkbox(nd_cheats_name(i),&on)) nd_cheats_request(i,on);
                    }
                    if(*nd_cheats_error()) ImGui::TextWrapped("%s",nd_cheats_error());
                    ImGui::TreePop();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if(ImGui::Button(tr("All settings"))) { expanded=false; menu_control_open_full(); }
        ImGui::TextDisabled("%s", tr("Drag the panel; release to snap to an edge."));
        if(ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            menu_absorb_pad_buttons(); expanded=false; input_reset(); settings_save();
        }
    }
    ImVec2 position=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
    if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) moving_panel=true;
    if(moving_panel && ImGui::IsMouseReleased(0)) {
        moving_panel=false;
        ImVec2 center(position.x+size.x*.5f, position.y+size.y*.5f);
        float best=1e30f; int nearest=anchor;
        for(int i=0;i<8;++i) {
            float dx=(center.x-anchors[i].x*io.DisplaySize.x)/io.DisplaySize.x;
            float dy=(center.y-anchors[i].y*io.DisplaySize.y)/io.DisplaySize.y;
            float distance=dx*dx+dy*dy;
            if(distance<best) { best=distance; nearest=i; }
        }
        // Save docking only after a real drag, never after clicking a control.
        if(ImGui::GetMouseDragDelta(0).x*ImGui::GetMouseDragDelta(0).x +
           ImGui::GetMouseDragDelta(0).y*ImGui::GetMouseDragDelta(0).y > 100*scale*scale) {
            g_settings.control_bar_anchor=nearest; previous_anchor=-1; dirty=true;
        }
    }
    { std::lock_guard<std::mutex> lock(bounds_lock); lower=position; upper=ImVec2(position.x+size.x,position.y+size.y); }
    ImGui::End();
    if(dirty) settings_save();
}
