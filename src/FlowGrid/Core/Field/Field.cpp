#include "Field.h"

#include "Core/Store/Patch/Patch.h"

#include "imgui.h"

// xxx only for gesture flash.
#include "App/Settings.h"
#include "App/Style/Style.h"

Field::Field(ComponentArgs &&args) : Component(std::move(args)) {
    FieldById.emplace(Id, this);
    FieldIdByPath.emplace(Path, Id);
}
Field::~Field() {
    FieldIdByPath.erase(Path);
    FieldById.erase(Id);
}

void Field::FindAndMarkChanged(const Patch &patch) {
    ClearChanged();
    for (const auto &path : patch.GetPaths()) {
        const auto *changed_field = FindByPath(path);
        if (changed_field == nullptr) throw std::runtime_error(std::format("Patch affects a path belonging to an unknown field: {}", path.string()));

        const auto relative_path = path == changed_field->Path ? fs::path("") : path.lexically_relative(changed_field->Path);
        if (ChangedPathsByFieldId.contains(changed_field->Id)) {
            ChangedPathsByFieldId[changed_field->Id].insert(relative_path);
        } else {
            ChangedPathsByFieldId.emplace(changed_field->Id, UniquePaths{relative_path});
        }

        // Mark all ancestor components as changed.
        const Component *ancestor = changed_field;
        while (ancestor != nullptr) {
            ChangedComponentIds.insert(ancestor->Id);
            ancestor = ancestor->Parent;
        }
    }
}

void Field::RefreshChanged(const Patch &patch) {
    FindAndMarkChanged(patch);
    static std::unordered_set<ChangeListener *> affected_listeners;
    for (const auto &[field_id, _] : ChangedPathsByFieldId) {
        auto *changed_field = FieldById[field_id];
        changed_field->RefreshValue();
        const auto &listeners = ChangeListenersByFieldId[field_id];
        affected_listeners.insert(listeners.begin(), listeners.end());
    }

    for (auto *listener : affected_listeners) listener->OnFieldChanged();
    affected_listeners.clear();
}

void Field::UpdateGesturing() {
    if (ImGui::IsItemActivated()) IsGesturing = true;
    if (ImGui::IsItemDeactivated()) IsGesturing = false;
}

std::optional<TimePoint> Field::LatestUpdateTime(const ID component_id) {
    if (!GestureChangedPathsByFieldId.contains(component_id)) return {};

    return GestureChangedPathsByFieldId.at(component_id).back().first;
}

void Field::RenderValueTree(ValueTreeLabelMode mode, bool auto_select) const {
    // Flash background color of nodes with alpha level corresponding to how much time is left in the gesture before it's committed.
    if (const auto latest_update_time = LatestUpdateTime(Id)) {
        const float flash_elapsed_ratio = fsec(Clock::now() - *latest_update_time).count() / app_settings.GestureDurationSec;
        ImColor flash_color = fg::style.FlowGrid.Colors[FlowGridCol_GestureIndicator];
        flash_color.Value.w = std::max(0.f, 1 - flash_elapsed_ratio);
        FillRowItemBg(flash_color);
    }
}
