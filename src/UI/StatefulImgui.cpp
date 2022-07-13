#include "StatefulImGui.h"
#include "../Context.h"

void StatefulImGui::DrawWindow(const Window &window, ImGuiWindowFlags flags) {
    if (!window.visible) return;

    bool &visible = ds.window_visible[window.name];
    if (ImGui::Begin(window.name.c_str(), &visible, flags)) {
        if (visible) window.draw();
        else q(close_window{window.name});
    }
    ImGui::End();
}

void dock_window(const Window &window, ImGuiID node_id) {
    ImGui::DockBuilderDockWindow(window.name.c_str(), node_id);
}

void gestured() {
    if (ImGui::IsItemActivated()) c.gesturing = true;
    if (ImGui::IsItemDeactivated()) c.gesturing = false;
}

bool StatefulImGui::WindowToggleMenuItem(const Window &window) {
    const bool edited = ImGui::MenuItem(window.name.c_str(), nullptr, window.visible);
    if (edited) q(toggle_window{window.name});
    return edited;
}

bool StatefulImGui::Checkbox(const char *label, bool v) {
    return ImGui::Checkbox(label, &v);
}

bool StatefulImGui::SliderFloat(const char *label, float *v, float v_min, float v_max, const char *format, ImGuiSliderFlags flags) {
    const bool edited = ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
    gestured();
    return edited;
}

bool StatefulImGui::SliderFloat2(const char *label, float *v, float v_min, float v_max, const char *format, ImGuiSliderFlags flags) {
    const bool edited = ImGui::SliderFloat2(label, v, v_min, v_max, format, flags);
    gestured();
    return edited;
}

bool StatefulImGui::SliderInt(const char *label, int *v, int v_min, int v_max, const char *format, ImGuiSliderFlags flags) {
    const bool edited = ImGui::SliderInt(label, v, v_min, v_max, format, flags);
    gestured();
    return edited;
}

bool StatefulImGui::SliderScalar(const char *label, ImGuiDataType data_type, void *p_data, const void *p_min, const void *p_max, const char *format, ImGuiSliderFlags flags) {
    const bool edited = ImGui::SliderScalar(label, data_type, p_data, p_min, p_max, format, flags);
    gestured();
    return edited;
}

bool StatefulImGui::DragFloat(const char *label, float *v, float v_speed, float v_min, float v_max, const char *format, ImGuiSliderFlags flags) {
    const bool edited = ImGui::DragFloat(label, v, v_speed, v_min, v_max, format, flags);
    gestured();
    return edited;
}

bool StatefulImGui::ColorEdit4(const char *label, float col[4], ImGuiColorEditFlags flags) {
    const bool edited = ImGui::ColorEdit4(label, col, flags);
    gestured();
    return edited;
}

void StatefulImGui::MenuItem(ActionID action_id) {
    const char *menu_label = action::get_menu_label(action_id);
    const char *shortcut = action::shortcut_for_id.contains(action_id) ? action::shortcut_for_id.at(action_id).c_str() : nullptr;
    if (ImGui::MenuItem(menu_label, shortcut, false, c.action_allowed(action_id))) q(action::create(action_id));
}