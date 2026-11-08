// Touch-first application sidebar. Gameplay mappings and native sensors are unchanged.
#include "imgui.h"
#include "../native_sidebar_ui.h"
#include "../native_amiibo_ui.h"
#include "../native_atmosphere_ui.h"
#include "control_bar.h"
#include "i18n.h"
#include <atomic>
#include <mutex>
#include <string.h>
extern "C" {
#include "../settings.h"
#include "../motion.h"
#include "../framepace.h"
#include "../nvn/output_scale.h"
void input_reset(void);
void menu_request_window_size_apply(void);
}
static std::atomic<bool> expanded{false};
static std::mutex bounds_lock;
static ImVec2 lower,upper;
bool control_bar_is_open(void) {return expanded.load();}
bool control_bar_hit(float x,float y) {
    std::lock_guard<std::mutex> lock(bounds_lock);
    return x>=lower.x && y>=lower.y && x<upper.x && y<upper.y;
}
void control_bar_toggle(void) {menu_absorb_pad_buttons();expanded=!expanded.load();input_reset();}
void control_bar_draw(float) {
    bool zh=!strncmp(ui_lang(),"zh",2);
    auto t=[zh](const char *en,const char *zhtext){return zh?zhtext:en;};
    static NdSidebar sidebar;bool open=expanded.load(),toggle=false,dirty=false;
    int anchor=g_settings.control_bar_anchor;
    bool content=nd_sidebar_begin(sidebar,open,anchor,t("Breath of the Wild","旷野之息"),zh,&toggle);
    if(content) {
        if(sidebar.page==0) {
            nd_ui_status(t("Live graphics settings","即时画面设置"));
            nd_ui_label(t("Frame cap","帧率上限"));
            static const int rates[]={30,40,45,60,90,120,0};
            const char *names[]={"30","40","45","60","90","120",t("Unlimited","不限帧")};int rate=3;
            for(int i=0;i<7;i++) if(rates[i]==framepace_configured_limit()) rate=i;
            if(nd_ui_choice("##frame-cap",&rate,names,7)) {framepace_set_user_limit(rates[rate]);dirty=true;}
            nd_ui_label(t("Anti-aliasing","抗锯齿"));
            const char *aa[]={t("Off","关闭"),"SMAA","TAA"};
            dirty|=nd_ui_choice("##antialias",&g_settings.antialias,aa,3);
            nd_ui_label(t("Upscaling","画面上采样"));
            const int methods[]={UPSCALE_OFF,UPSCALE_FSR1,UPSCALE_FSR3};
            const char *upscalers[]={t("Native","原始画面"),"AMD FSR 1",t("FSR 3.1 (experimental)","FSR 3.1（实验）")};int current=0;
            for(int i=0;i<3;i++) if(g_settings.upscaler==methods[i]) current=i;
            if(ImGui::BeginCombo("##upscaler",upscalers[current])) {
                for(int i=0;i<3;i++) {
                    bool enabled=upscaler_available(methods[i]);ImGui::BeginDisabled(!enabled);
                    if(ImGui::Selectable(upscalers[i],current==i,0,ImVec2(0,44))) {g_settings.upscaler=methods[i];dirty=true;}
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            nd_ui_label(t("Upscaling quality","上采样质量"));
            const char *quality[]={t("Ultra quality (1.3x)","极致质量（1.3 倍）"),t("Quality (1.5x)","质量（1.5 倍）"),t("Balanced (1.7x)","均衡（1.7 倍）"),t("Performance (2x)","性能（2 倍）"),t("Ultra performance (3x)","极致性能（3 倍）")};
            ImGui::BeginDisabled(g_settings.upscaler==UPSCALE_OFF || !upscale_modes_available());
            dirty|=nd_ui_choice("##quality",&g_settings.upscale_quality,quality,5);ImGui::EndDisabled();
            nd_ui_label(t("Sharpening","锐化强度"));
            ImGui::SliderInt("##sharpness",&g_settings.sharpening,0,10);if(ImGui::IsItemDeactivatedAfterEdit()) dirty=true;
            ImGui::TextWrapped(t("Backend anti-aliasing: %s\nRender scale: %.2fx","后端抗锯齿：%s\n当前渲染比例：%.2f 倍"),aa[std::clamp(rt_antialias(),0,2)],upscale_factor());
            if(ImGui::TreeNode(t("Window size / capabilities","窗口尺寸与能力说明"))) {
                nd_ui_label(t("Window size","窗口尺寸"));
                const int widths[]={960,1280,1280,1920},heights[]={540,720,800,1080};
                const char *sizes[]={"960 x 540","1280 x 720","1280 x 800","1920 x 1080"};int size=2;
                for(int i=0;i<4;i++) if(g_settings.window_w==widths[i] && g_settings.window_h==heights[i]) size=i;
                if(nd_ui_choice("##window-size",&size,sizes,4)) {g_settings.window_w=widths[size];g_settings.window_h=heights[size];menu_request_window_size_apply();dirty=true;}
                ImGui::TextWrapped("%s",t("Fullscreen output follows the display. FSR 4 is unavailable. FSR 3.1 remains behind its experimental backend gate.","全屏输出跟随显示器。FSR 4 不可用，FSR 3.1 仍遵循实验后端门禁。"));ImGui::TreePop();
            }
        } else if(sidebar.page==1) {
            nd_ui_status(motion_has_sensors()?t("Native sensor connected","原生传感器已连接"):t("No motion sensor detected","尚未检测到陀螺仪"),motion_has_sensors()?1:-1);
            nd_ui_label(t("Motion sensitivity","陀螺仪灵敏度"));
            ImGui::SliderFloat("##motion",&g_settings.motion_sens,.1f,3.f,"%.1fx");if(ImGui::IsItemDeactivatedAfterEdit()) dirty=true;
            ImGui::TextWrapped("%s",t("Game bindings stay unchanged. App menus use A to confirm and B to go back.","游戏操作保持原绑定。应用菜单使用 A 确认、B 返回。"));
            nd_ui_label(t("Edge / corner","边框与角落"));
            const char *docks[]={t("Top left","左上"),t("Top","上边"),t("Top right","右上"),t("Right","右边"),t("Bottom right","右下"),t("Bottom","下边"),t("Bottom left","左下"),t("Left","左边")};
            dirty|=nd_ui_choice("##dock",&anchor,docks,8);
            ImGui::TextWrapped("%s",t("Tap the edge handle. Drag the short bar below the title to move.","点击边缘入口展开；拖动标题下方短条移动位置。"));
            if(ImGui::Button(t("More settings","更多设置"),ImVec2(-1,50))) {expanded=false;menu_control_open_full();}
        } else if(sidebar.page==2) nd_draw_amiibo_library("botw-native",zh);
        else nd_draw_atmosphere("botw-native",zh);
    }
    ImVec2 lo,hi;dirty|=nd_sidebar_end(sidebar,open,&anchor,zh,&lo,&hi);
    {std::lock_guard<std::mutex> guard(bounds_lock);lower=lo;upper=hi;}
    g_settings.control_bar_anchor=anchor;
    if(toggle) {control_bar_toggle();dirty=true;}
    if(dirty) settings_save();
}
