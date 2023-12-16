
#include "Project.h"

#include "imgui_internal.h"
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>
#include <set>

#include "Application/ApplicationPreferences.h"
#include "Core/Store/Store.h"
#include "Core/Store/StoreHistory.h"
#include "Helper/File.h"
#include "Helper/Time.h"

using std::vector;
using namespace FlowGrid;

static SavableActionMoments ActiveGestureActions{}; // uncompressed, uncommitted
static Patch LatestPatch;

// Project constants:
static const fs::path InternalPath = ".flowgrid";

// Order matters here, as the first extension is the default project extension.
static const std::map<ProjectFormat, std::string> ExtensionByProjectFormat{
    {ProjectFormat::ActionFormat, ".fla"},
    {ProjectFormat::StateFormat, ".fls"},
};
static const auto ProjectFormatByExtension = ExtensionByProjectFormat | std::views::transform([](const auto &p) { return std::pair(p.second, p.first); }) | ranges::to<std::map>();
static const auto AllProjectExtensions = ProjectFormatByExtension | std::views::keys;
static const std::string AllProjectExtensionsDelimited = AllProjectExtensions | ranges::views::join(',') | ranges::to<std::string>;

static const fs::path EmptyProjectPath = InternalPath / ("empty" + ExtensionByProjectFormat.at(ProjectFormat::StateFormat));
// The default project is a user-created project that loads on app start, instead of the empty project.
// As an action-formatted project, it builds on the empty project, replaying the actions present at the time the default project was saved.
static const fs::path DefaultProjectPath = InternalPath / ("default" + ExtensionByProjectFormat.at(ProjectFormat::ActionFormat));

static std::optional<fs::path> CurrentProjectPath;
static bool ProjectHasChanges{false};

std::optional<ProjectFormat> GetProjectFormat(const fs::path &path) {
    const string &ext = path.extension();
    if (!ProjectFormatByExtension.contains(ext)) return {};
    return ProjectFormatByExtension.at(ext);
}

static float GestureTimeRemainingSec(float gesture_duration_sec) {
    if (ActiveGestureActions.empty()) return 0;
    const auto ret = std::max(0.f, gesture_duration_sec - fsec(Clock::now() - ActiveGestureActions.back().QueueTime).count());
    return ret;
}

void CommitGesture() {
    Field::GestureChangedPaths.clear();
    if (ActiveGestureActions.empty()) return;

    const auto merged_actions = MergeActions(ActiveGestureActions);
    ActiveGestureActions.clear();
    if (merged_actions.empty()) return;

    History.AddGesture({merged_actions, Clock::now()});
}

void Project::SetHistoryIndex(u32 index) const {
    if (index == History.Index) return;

    Field::GestureChangedPaths.clear();
    // If we're mid-gesture, revert the current gesture before navigating to the new index.
    ActiveGestureActions.clear();
    History.SetIndex(index);
    LatestPatch = RootStore.CheckedSet(History.CurrentStore());
    Field::RefreshChanged(LatestPatch);
    // ImGui settings are cheched separately from style since we don't need to re-apply ImGui settings state to ImGui context
    // when it initially changes, since ImGui has already updated its own context.
    // We only need to update the ImGui context based on settings changes when the history index changes.
    // However, style changes need to be applied to the ImGui context in all cases, since these are issued from Field changes.
    // We don't make `ImGuiSettings` a field change listener for this because it would would end up being slower,
    // since it has many descendent fields, and we would wastefully check for changes during the forward action pass, as explained above.
    if (LatestPatch.IsPrefixOfAnyPath(ImGuiSettings.Path)) ImGuiSettings::IsChanged = true;
    ProjectHasChanges = true;
}

Project::Project(Store &store) : Component(store, Context) {
    Context.Windows.SetWindowComponents({
        Audio.Graph,
        Audio.Graph.Connections,
        Audio.Style,
        Settings,
        Audio.Faust.FaustDsps,
        Audio.Faust.Logs,
        Audio.Faust.Graphs,
        Audio.Faust.Paramss,
        Debug,
        Debug.ProjectPreview,
        Debug.StorePathUpdateFrequency,
        Debug.DebugLog,
        Debug.StackTool,
        Debug.Metrics,
        Context.Style,
        Demo,
        Info,
    });
}

json Project::GetProjectJson(const ProjectFormat format) const {
    switch (format) {
        case StateFormat: return ToJson();
        case ActionFormat: return History.GetIndexedGestures();
    }
}

void Project::Apply(const ActionType &action) const {
    Visit(
        action,
        [this](const Action::Project::OpenEmpty &) { Open(EmptyProjectPath); },
        [this](const Action::Project::Open &a) { Open(a.file_path); },
        [this](const Action::Project::OpenDefault &) { Open(DefaultProjectPath); },

        [this](const Action::Project::Save &a) { Save(a.file_path); },
        [this](const Action::Project::SaveDefault &) { Save(DefaultProjectPath); },
        [this](const Action::Project::SaveCurrent &) {
            if (CurrentProjectPath) Save(*CurrentProjectPath);
        },
        // History-changing actions:
        [this](const Action::Project::Undo &) {
            if (History.Empty()) return;

            // `StoreHistory::SetIndex` reverts the current gesture before applying the new history index.
            // If we're at the end of the stack, we want to commit the active gesture and add it to the stack.
            // Otherwise, if we're already in the middle of the stack somewhere, we don't want an active gesture
            // to commit and cut off everything after the current history index, so an undo just ditches the active changes.
            // (This allows consistent behavior when e.g. being in the middle of a change and selecting a point in the undo history.)
            if (History.Index == History.Size() - 1) {
                if (!ActiveGestureActions.empty()) CommitGesture();
                SetHistoryIndex(History.Index - 1);
            } else {
                SetHistoryIndex(History.Index - (ActiveGestureActions.empty() ? 1 : 0));
            }
        },
        [this](const Action::Project::Redo &) { SetHistoryIndex(History.Index + 1); },
        [this](const Action::Project::SetHistoryIndex &a) { SetHistoryIndex(a.index); },

        [](const FieldActionHandler::ActionType &a) { Field::ActionHandler.Apply(a); },
        [this](const Store::ActionType &a) { RootStore.Apply(a); },
        [this](const Action::Project::ShowOpenDialog &) { FileDialog.Set({"Choose file", AllProjectExtensionsDelimited, ".", ""}); },
        [this](const Action::Project::ShowSaveDialog &) { FileDialog.Set({"Choose file", AllProjectExtensionsDelimited, ".", "my_flowgrid_project", true, 1}); },
        [this](const Audio::ActionType &a) { Audio.Apply(a); },
        [this](const FileDialog::ActionType &a) { FileDialog.Apply(a); },
        [this](const Windows::ActionType &a) { Context.Windows.Apply(a); },
        [this](const Style::ActionType &a) { Context.Style.Apply(a); },
    );
}

bool Project::CanApply(const ActionType &action) const {
    return Visit(
        action,
        [](const Action::Project::Undo &) { return !ActiveGestureActions.empty() || History.CanUndo(); },
        [](const Action::Project::Redo &) { return History.CanRedo(); },
        [](const Action::Project::SetHistoryIndex &a) { return a.index < History.Size(); },
        [](const Action::Project::Save &) { return !History.Empty(); },
        [](const Action::Project::SaveDefault &) { return !History.Empty(); },
        [](const Action::Project::ShowOpenDialog &) { return true; },
        [](const Action::Project::ShowSaveDialog &) { return ProjectHasChanges; },
        [](const Action::Project::SaveCurrent &) { return ProjectHasChanges; },
        [](const Action::Project::OpenDefault &) { return fs::exists(DefaultProjectPath); },
        [](const Action::Project::OpenEmpty &) { return true; },
        [](const Action::Project::Open &) { return true; },

        [](const FieldActionHandler::ActionType &a) { return Field::ActionHandler.CanApply(a); },
        [this](const Store::ActionType &a) { return RootStore.CanApply(a); },
        [this](const Audio::ActionType &a) { return Audio.CanApply(a); },
        [this](const FileDialog::ActionType &a) { return FileDialog.CanApply(a); },
        [this](const Windows::ActionType &a) { return Context.Windows.CanApply(a); },
        [this](const Style::ActionType &a) { return Context.Style.CanApply(a); },
    );
}

using namespace ImGui;

void Project::Render() const {
    MainMenu.Draw();
    // Good initial layout setup example in this issue: https://github.com/ocornut/imgui/issues/3548
    auto dockspace_id = DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    int frame_count = GetCurrentContext()->FrameCount;
    if (frame_count == 1) {
        auto audio_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.25f, nullptr, &dockspace_id);
        auto utilities_node_id = DockBuilderSplitNode(audio_node_id, ImGuiDir_Down, 0.5f, nullptr, &audio_node_id);

        auto debug_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.3f, nullptr, &dockspace_id);
        auto metrics_node_id = DockBuilderSplitNode(debug_node_id, ImGuiDir_Right, 0.35f, nullptr, &debug_node_id);

        auto info_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.2f, nullptr, &dockspace_id);
        auto settings_node_id = DockBuilderSplitNode(info_node_id, ImGuiDir_Down, 0.25f, nullptr, &info_node_id);
        auto faust_tools_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.5f, nullptr, &dockspace_id);
        auto faust_graph_node_id = DockBuilderSplitNode(faust_tools_node_id, ImGuiDir_Left, 0.5f, nullptr, &faust_tools_node_id);

        Audio.Graph.Dock(audio_node_id);
        Audio.Graph.Connections.Dock(audio_node_id);
        Audio.Style.Dock(audio_node_id);

        Audio.Faust.FaustDsps.Dock(dockspace_id);
        Audio.Faust.Graphs.Dock(faust_graph_node_id);
        Audio.Faust.Paramss.Dock(faust_tools_node_id);
        Audio.Faust.Logs.Dock(faust_tools_node_id);

        Debug.Dock(debug_node_id);
        Debug.ProjectPreview.Dock(debug_node_id);
        // Debug.StateMemoryEditor.Dock(debug_node_id);
        Debug.StorePathUpdateFrequency.Dock(debug_node_id);
        Debug.DebugLog.Dock(debug_node_id);
        Debug.StackTool.Dock(debug_node_id);
        Debug.Metrics.Dock(metrics_node_id);

        Context.Style.Dock(utilities_node_id);
        Demo.Dock(utilities_node_id);

        Info.Dock(info_node_id);
        Settings.Dock(settings_node_id);
    }

    // Draw non-window children.
    for (const auto *child : Children) {
        if (!Context.Windows.IsWindow(child->Id)) child->Draw();
    }

    Context.Windows.Draw();

    if (frame_count == 1) {
        // Default focused windows.
        Context.Style.Focus();
        Audio.Graph.Focus();
        Audio.Faust.Graphs.Focus();
        Audio.Faust.Paramss.Focus();
        Debug.Focus(); // not visible by default anymore
    }

    // Handle file dialog.
    static string PrevSelectedPath = "";
    if (PrevSelectedPath != FileDialog.SelectedFilePath) {
        const fs::path selected_path = FileDialog.SelectedFilePath;
        const string &extension = selected_path.extension();
        if (std::ranges::find(AllProjectExtensions, extension) != AllProjectExtensions.end()) {
            if (FileDialog.SaveMode) Action::Project::Save{selected_path}.q();
            else Action::Project::Open{selected_path}.q();
        }
        PrevSelectedPath = selected_path;
    }

    static const auto Shortcuts = Action::Any::CreateShortcuts();
    const auto &io = GetIO();
    for (const auto &[action_id, shortcut] : Shortcuts) {
        const auto &[mod, key] = shortcut.Parsed;
        if (mod == io.KeyMods && IsKeyPressed(GetKeyIndex(ImGuiKey(key)), ImGuiKeyOwner_None)) {
            const auto action = Action::Any::Create(action_id);
            if (CanApply(action)) action.q();
        }
    }
}

void Project::OpenRecentProjectMenuItem() {
    if (BeginMenu("Open recent project", !Preferences.RecentlyOpenedPaths.empty())) {
        for (const auto &recently_opened_path : Preferences.RecentlyOpenedPaths) {
            if (MenuItem(recently_opened_path.filename().c_str())) Action::Project::Open{recently_opened_path}.q();
        }
        EndMenu();
    }
}

bool IsUserProjectPath(const fs::path &path) {
    return fs::relative(path).string() != fs::relative(EmptyProjectPath).string() &&
        fs::relative(path).string() != fs::relative(DefaultProjectPath).string();
}

void SetCurrentProjectPath(const fs::path &path) {
    ProjectHasChanges = false;
    if (IsUserProjectPath(path)) {
        CurrentProjectPath = path;
        Preferences.OnProjectOpened(path);
    } else {
        CurrentProjectPath = {};
    }
}

bool Project::Save(const fs::path &path) const {
    const bool is_current_project = CurrentProjectPath && fs::equivalent(path, *CurrentProjectPath);
    if (is_current_project && !ProjectHasChanges) return false;

    const auto format = GetProjectFormat(path);
    if (!format) return false; // TODO log

    CommitGesture(); // Make sure any pending actions/diffs are committed.
    if (!FileIO::write(path, GetProjectJson(*format).dump())) {
        throw std::runtime_error(std::format("Failed to write project file: {}", path.string()));
    }

    SetCurrentProjectPath(path);
    return true;
}

void Project::OnApplicationLaunch() const {
    Field::IsGesturing = false;
    History.Clear();
    Field::ClearChanged();
    Field::LatestChangedPaths.clear();

    // When loading a new project, we always refresh all UI contexts.
    Context.Style.ImGui.IsChanged = true;
    Context.Style.ImPlot.IsChanged = true;
    ImGuiSettings::IsChanged = true;

    // Keep the canonical "empty" project up-to-date.
    if (!fs::exists(InternalPath)) fs::create_directory(InternalPath);
    Save(EmptyProjectPath);
}

json ReadFileJson(const fs::path &file_path) { return json::parse(FileIO::read(file_path)); }

// Helper function used in `Project::Open`.
// Modifies the active transient store.
void Project::OpenStateFormatProject(const fs::path &file_path) const {
    auto j = ReadFileJson(file_path);
    // First, refresh all component container fields to ensure the dynamically managed component instances match the JSON.
    for (const ID auxiliary_field_id : Field::ComponentContainerAuxiliaryFields) {
        auto *auxiliary_field = Field::ById.at(auxiliary_field_id);
        if (j.contains(auxiliary_field->JsonPointer())) {
            auxiliary_field->SetJson(std::move(j.at(auxiliary_field->JsonPointer())));
            auxiliary_field->Refresh();
            auxiliary_field->Parent->Refresh();
        }
    }

    // Now, every flattened JSON pointer is 1:1 with a field instance path.
    SetJson(std::move(j));

    // We could do `Field::RefreshChanged(RootStore.CheckedCommit())`, and only refresh the changed fields,
    // but this gets tricky with component container fields, since the store patch will contain added/removed paths
    // that have already been accounted for above.
    RootStore.Commit();
    Field::ClearChanged();
    Field::LatestChangedPaths.clear();
    Field::RefreshAll();

    // Always update the ImGui context, regardless of the patch, to avoid expensive sifting through paths and just to be safe.
    ImGuiSettings.IsChanged = true;
    History.Clear();
}

void Project::Open(const fs::path &file_path) const {
    const auto format = GetProjectFormat(file_path);
    if (!format) return; // TODO log

    Field::IsGesturing = false;

    if (format == StateFormat) {
        OpenStateFormatProject(file_path);
    } else if (format == ActionFormat) {
        OpenStateFormatProject(EmptyProjectPath);

        StoreHistory::IndexedGestures indexed_gestures = ReadFileJson(file_path);
        for (auto &gesture : indexed_gestures.Gestures) {
            for (const auto &action_moment : gesture.Actions) {
                Visit(
                    action_moment.Action,
                    [this](const Project::ActionType &a) { Apply(a); },
                );
                LatestPatch = RootStore.CheckedCommit();
                Field::RefreshChanged(LatestPatch);
            }
            History.AddGesture(std::move(gesture));
        }
        SetHistoryIndex(indexed_gestures.Index);
        Field::LatestChangedPaths.clear();
    }

    SetCurrentProjectPath(file_path);
}

void Project::WindowMenuItem() const {
    const auto &item = [this](const Component &c) { return Context.Windows.ToggleMenuItem(c); };
    if (BeginMenu("Windows")) {
        if (BeginMenu("Audio")) {
            item(Audio.Graph);
            item(Audio.Graph.Connections);
            item(Audio.Style);
            EndMenu();
        }
        if (BeginMenu("Faust")) {
            item(Audio.Faust.FaustDsps);
            item(Audio.Faust.Graphs);
            item(Audio.Faust.Paramss);
            item(Audio.Faust.Logs);
            EndMenu();
        }
        if (BeginMenu("Debug")) {
            item(Debug);
            item(Debug.ProjectPreview);
            item(Debug.StorePathUpdateFrequency);
            item(Debug.DebugLog);
            item(Debug.StackTool);
            item(Debug.Metrics);
            EndMenu();
        }
        item(Context.Style);
        item(Demo);
        item(Info);
        item(Settings);
        EndMenu();
    }
}

//-----------------------------------------------------------------------------
// [SECTION] Debug
//-----------------------------------------------------------------------------

#include "date.h"
#include "implot.h"

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/map.hpp>

#include "Helper/String.h"
#include "UI/HelpMarker.h"
#include "UI/JsonTree.h"

struct Plottable {
    std::vector<std::string> Labels;
    std::vector<ImU64> Values;
};

Plottable StorePathChangeFrequencyPlottable() {
    if (History.GetChangedPathsCount() == 0 && Field::GestureChangedPaths.empty()) return {};

    std::map<StorePath, u32> gesture_change_counts;
    for (const auto &[field_id, changed_paths] : Field::GestureChangedPaths) {
        const auto &field = Field::ById[field_id];
        for (const PathsMoment &paths_moment : changed_paths) {
            for (const auto &path : paths_moment.second) {
                gesture_change_counts[path == "" ? field->Path : field->Path / path]++;
            }
        }
    }

    const auto history_change_counts = History.GetChangeCountByPath();
    const std::set<StorePath> paths = ranges::views::concat(ranges::views::keys(history_change_counts), ranges::views::keys(gesture_change_counts)) | ranges::to<std::set>;

    u32 i = 0;
    std::vector<ImU64> values(!gesture_change_counts.empty() ? paths.size() * 2 : paths.size());
    for (const auto &path : paths) {
        values[i++] = history_change_counts.contains(path) ? history_change_counts.at(path) : 0;
    }
    if (!gesture_change_counts.empty()) {
        // Optionally add a second plot item for gesturing update times.
        // See `ImPlot::PlotBarGroups` for value ordering explanation.
        for (const auto &path : paths) {
            values[i++] = gesture_change_counts.contains(path) ? gesture_change_counts.at(path) : 0;
        }
    }

    // Remove leading '/' from paths to create labels.
    return {
        paths | std::views::transform([](const string &path) { return path.substr(1); }) | ranges::to<std::vector>,
        values,
    };
}

void Project::Debug::StorePathUpdateFrequency::Render() const {
    auto [labels, values] = StorePathChangeFrequencyPlottable();
    if (labels.empty()) {
        Text("No state updates yet.");
        return;
    }

    if (ImPlot::BeginPlot("Path update frequency", {-1, float(labels.size()) * 30 + 60}, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("Number of updates", nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_Invert);

        // Hack to allow `SetupAxisTicks` without breaking on assert `n_ticks > 1`: Just add an empty label and only plot one value.
        // todo fix in ImPlot
        if (labels.size() == 1) labels.emplace_back("");

        // todo add an axis flag to exclude non-integer ticks
        // todo add an axis flag to show last tick
        const auto c_labels = labels | std::views::transform([](const std::string &label) { return label.c_str(); }) | ranges::to<std::vector>;
        ImPlot::SetupAxisTicks(ImAxis_Y1, 0, double(labels.size() - 1), int(labels.size()), c_labels.data(), false);

        static const char *ItemLabels[] = {"Committed updates", "Active updates"};
        const int item_count = !ActiveGestureActions.empty() ? 2 : 1;
        const int group_count = values.size() / item_count;
        ImPlot::PlotBarGroups(ItemLabels, values.data(), item_count, group_count, 0.75, 0, ImPlotBarGroupsFlags_Horizontal | ImPlotBarGroupsFlags_Stacked);

        ImPlot::EndPlot();
    }
}

void Project::Debug::DebugLog::Render() const {
    ShowDebugLogWindow();
}
void Project::Debug::StackTool::Render() const {
    ShowIDStackToolWindow();
}

void Project::Debug::Metrics::ImGuiMetrics::Render() const { ImGui::ShowMetricsWindow(); }
void Project::Debug::Metrics::ImPlotMetrics::Render() const { ImPlot::ShowMetricsWindow(); }

using namespace FlowGrid;

void Project::Debug::OnFieldChanged() {
    if (AutoSelect.IsChanged()) {
        WindowFlags = AutoSelect ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None;
    }
}

void Project::RenderDebug() const {
    const bool auto_select = Debug.AutoSelect;
    if (auto_select) BeginDisabled();
    RenderValueTree(Debug::LabelModeType(int(Debug.LabelMode)) == Debug::LabelModeType::Annotated, auto_select);
    if (auto_select) EndDisabled();
}

void Project::Debug::ProjectPreview::Render() const {
    Format.Draw();
    Raw.Draw();

    Separator();

    json project_json = GetProject()->GetProjectJson(ProjectFormat(int(Format)));
    if (Raw) {
        TextUnformatted(project_json.dump(4).c_str());
    } else {
        SetNextItemOpen(true);
        fg::JsonTree("", std::move(project_json));
    }
}

void ShowActions(const SavableActionMoments &actions) {
    for (u32 action_index = 0; action_index < actions.size(); action_index++) {
        const auto &[action, queue_time] = actions[action_index];
        if (TreeNodeEx(to_string(action_index).c_str(), ImGuiTreeNodeFlags_None, "%s", action.GetPath().string().c_str())) {
            BulletText("Queue time: %s", date::format("%Y-%m-%d %T", queue_time).c_str());
            SameLine();
            fg::HelpMarker("The original queue time of the action. If this is a merged action, this is the queue time of the most recent action in the merge.");
            json data = json(action)[1];
            if (!data.is_null()) {
                SetNextItemOpen(true);
                JsonTree("Data", std::move(data));
            }
            TreePop();
        }
    }
}

ImRect RowItemRatioRect(float ratio) {
    const ImVec2 row_min = {GetWindowPos().x, GetCursorScreenPos().y};
    return {row_min, row_min + ImVec2{GetWindowWidth() * std::clamp(ratio, 0.f, 1.f), GetFontSize()}};
}

void Project::Debug::Metrics::FlowGridMetrics::Render() const {
    {
        // Active (uncompressed) gesture
        const bool is_gesturing = Field::IsGesturing;
        const bool any_gesture_actions = !ActiveGestureActions.empty();
        if (any_gesture_actions || is_gesturing) {
            // Gesture completion progress bar (full-width to empty).
            const float gesture_duration_sec = GetProject()->Settings.GestureDurationSec;
            const float time_remaining_sec = GestureTimeRemainingSec(gesture_duration_sec);
            const auto row_item_ratio_rect = RowItemRatioRect(time_remaining_sec / gesture_duration_sec);
            GetWindowDrawList()->AddRectFilled(row_item_ratio_rect.Min, row_item_ratio_rect.Max, RootContext.Style.FlowGrid.Colors[FlowGridCol_GestureIndicator]);

            const auto &ActiveGestureActions_title = "Active gesture"s + (any_gesture_actions ? " (uncompressed)" : "");
            if (TreeNodeEx(ActiveGestureActions_title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (is_gesturing) FillRowItemBg(RootContext.Style.ImGui.Colors[ImGuiCol_FrameBgActive]);
                else BeginDisabled();
                Text("Widget gesture: %s", is_gesturing ? "true" : "false");
                if (!is_gesturing) EndDisabled();

                if (any_gesture_actions) ShowActions(ActiveGestureActions);
                else Text("No actions yet");
                TreePop();
            }
        } else {
            BeginDisabled();
            Text("No active gesture");
            EndDisabled();
        }
    }
    Separator();
    {
        const bool no_history = History.Empty();
        if (no_history) BeginDisabled();
        if (TreeNodeEx("History", ImGuiTreeNodeFlags_DefaultOpen, "History (Records: %d, Current record index: %d)", History.Size() - 1, History.Index)) {
            if (!no_history) {
                int edited_history_index = int(History.Index);
                if (SliderInt("History index", &edited_history_index, 0, int(History.Size() - 1))) {
                    Action::Project::SetHistoryIndex{u32(edited_history_index)}.q();
                }
            }
            for (u32 i = 1; i < History.Size(); i++) {
                // todo button to navitate to this history index.
                if (TreeNodeEx(to_string(i).c_str(), i == History.Index ? (ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_DefaultOpen) : ImGuiTreeNodeFlags_None)) {
                    const auto &[store_record, gesture] = History.RecordAt(i);
                    BulletText("Gesture committed: %s\n", date::format("%Y-%m-%d %T", gesture.CommitTime).c_str());
                    if (TreeNode("Actions")) {
                        ShowActions(gesture.Actions);
                        TreePop();
                    }
                    if (TreeNode("Patch")) {
                        // We compute patches as we need them rather than memoizing.
                        const auto &patch = History.CreatePatch(i);
                        for (const auto &[partial_path, op] : patch.Ops) {
                            const auto &path = patch.BasePath / partial_path;
                            if (TreeNodeEx(path.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                                BulletText("Op: %s", to_string(op.Op).c_str());
                                if (op.Value) BulletText("Value: %s", to_string(*op.Value).c_str());
                                if (op.Old) BulletText("Old value: %s", to_string(*op.Old).c_str());
                                TreePop();
                            }
                        }
                        TreePop();
                    }
                    TreePop();
                }
            }
            TreePop();
        }
        if (no_history) EndDisabled();
    }
    Separator();
    {
        // Preferences
        const bool has_RecentlyOpenedPaths = !Preferences.RecentlyOpenedPaths.empty();
        if (TreeNodeEx("Preferences", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (SmallButton("Clear")) Preferences.Clear();
            SameLine();
            ShowRelativePaths.Draw();

            if (!has_RecentlyOpenedPaths) BeginDisabled();
            if (TreeNodeEx("Recently opened paths", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const auto &recently_opened_path : Preferences.RecentlyOpenedPaths) {
                    BulletText("%s", (ShowRelativePaths ? fs::relative(recently_opened_path) : recently_opened_path).c_str());
                }
                TreePop();
            }
            if (!has_RecentlyOpenedPaths) EndDisabled();

            TreePop();
        }
    }
    Separator();
    {
        // Various internals
        Text("Action variant size: %lu bytes", sizeof(Action::Savable));
        Text("Primitive variant size: %lu bytes", sizeof(Primitive));
        SameLine();
        fg::HelpMarker(
            "All actions are internally stored in a `std::variant`, which must be large enough to hold its largest type. "
            "Thus, it's important to keep action data minimal."
        );
    }
}

void Project::Debug::Metrics::Render() const {
    RenderTabs();
}

// #include "imgui_memory_editor.h"

// todo need to rethink this with the store system
// void Project::Debug::StateMemoryEditor::Render() const {
//     static MemoryEditor memory_editor;
//     static bool first_render{true};
//     if (first_render) {
//         memory_editor.OptShowDataPreview = true;
//         //        memory_editor.WriteFn = ...; todo write_state_bytes action
//         first_render = false;
//     }

//     const void *mem_data{&s};
//     memory_editor.DrawContents(mem_data, sizeof(s));
// }

//-----------------------------------------------------------------------------
// [SECTION] Action queueing
//-----------------------------------------------------------------------------

void Project::Queue(ActionMoment &&action_moment) const {
    ActionQueue.enqueue(std::move(action_moment));
}

void Project::RunQueuedActions(Store &store, bool force_commit_gesture, bool ignore_actions) const {
    static ActionMoment action_moment;

    if (ignore_actions) {
        while (ActionQueue.try_dequeue(action_moment)) {};
        return;
    }

    const bool gesture_actions_already_present = !ActiveGestureActions.empty();

    while (ActionQueue.try_dequeue(action_moment)) {
        auto &[action, queue_time] = action_moment;
        if (!CanApply(action)) continue;

        // Special cases:
        // * If saving the current project where there is none, open the save project dialog so the user can choose the save file:
        if (std::holds_alternative<Action::Project::SaveCurrent>(action) && !CurrentProjectPath) action = Action::Project::ShowSaveDialog{};
        // * Treat all toggles as immediate actions. Otherwise, performing two toggles in a row compresses into nothing:
        force_commit_gesture |=
            std::holds_alternative<Action::Primitive::Bool::Toggle>(action) ||
            std::holds_alternative<Action::Vec2::ToggleLinked>(action) ||
            std::holds_alternative<Action::AdjacencyList::ToggleConnection>(action) ||
            std::holds_alternative<Action::FileDialog::Select>(action);

        Apply(action);

        Visit(
            action,
            [&store, &queue_time](const Action::Savable &a) {
                LatestPatch = store.CheckedCommit();
                if (!LatestPatch.Empty()) {
                    Field::RefreshChanged(LatestPatch, true);
                    ActiveGestureActions.emplace_back(a, queue_time);
                    ProjectHasChanges = true;
                }
            },
            // Note: `const auto &` capture does not work when the other type is itself a variant group. Need to be exhaustive.
            [](const Action::NonSavable &) {},
        );
    }

    if (force_commit_gesture ||
        (!Field::IsGesturing && gesture_actions_already_present && GestureTimeRemainingSec(Settings.GestureDurationSec) <= 0)) {
        CommitGesture();
    }
}

#define DefineQ(ActionType)                                                                                      \
    void Action::ActionType::q() const { project.Queue({std::move(*this), Clock::now()}); }                      \
    void Action::ActionType::MenuItem() {                                                                        \
        const auto &instance = Action::ActionType{};                                                             \
        if (ImGui::MenuItem(GetMenuLabel().c_str(), GetShortcut().c_str(), false, project.CanApply(instance))) { \
            instance.q();                                                                                        \
        }                                                                                                        \
    }

DefineQ(Windows::ToggleVisible);
DefineQ(Windows::ToggleDebug);
DefineQ(Project::Undo);
DefineQ(Project::Redo);
DefineQ(Project::SetHistoryIndex);
DefineQ(Project::Open);
DefineQ(Project::OpenEmpty);
DefineQ(Project::OpenDefault);
DefineQ(Project::Save);
DefineQ(Project::SaveDefault);
DefineQ(Project::SaveCurrent);
DefineQ(Project::ShowOpenDialog);
DefineQ(Project::ShowSaveDialog);
DefineQ(Primitive::Bool::Toggle);
DefineQ(Primitive::Int::Set);
DefineQ(Primitive::UInt::Set);
DefineQ(Primitive::Float::Set);
DefineQ(Primitive::String::Set);
DefineQ(Primitive::Enum::Set);
DefineQ(Primitive::Flags::Set);
DefineQ(TextBuffer::Set);
DefineQ(PrimitiveVector<bool>::SetAt);
DefineQ(PrimitiveVector<int>::SetAt);
DefineQ(PrimitiveVector<u32>::SetAt);
DefineQ(PrimitiveVector<float>::SetAt);
DefineQ(PrimitiveVector<std::string>::SetAt);
DefineQ(PrimitiveVector2D<bool>::Set);
DefineQ(PrimitiveVector2D<int>::Set);
DefineQ(PrimitiveVector2D<u32>::Set);
DefineQ(PrimitiveVector2D<float>::Set);
DefineQ(Vec2::Set);
DefineQ(Vec2::SetX);
DefineQ(Vec2::SetY);
DefineQ(Vec2::SetAll);
DefineQ(Vec2::ToggleLinked);
DefineQ(AdjacencyList::ToggleConnection);
DefineQ(Navigable<u32>::Push);
DefineQ(Navigable<u32>::MoveTo);
DefineQ(Store::ApplyPatch);
DefineQ(Style::SetImGuiColorPreset);
DefineQ(Style::SetImPlotColorPreset);
DefineQ(Style::SetFlowGridColorPreset);
DefineQ(AudioGraph::CreateNode);
DefineQ(AudioGraph::CreateFaustNode);
DefineQ(AudioGraph::DeleteNode);
DefineQ(AudioGraph::SetDeviceDataFormat);
DefineQ(Faust::DSP::Create);
DefineQ(Faust::DSP::Delete);
DefineQ(Faust::File::ShowOpenDialog);
DefineQ(Faust::File::ShowSaveDialog);
DefineQ(Faust::File::Save);
DefineQ(Faust::File::Open);
DefineQ(Faust::GraphStyle::ApplyColorPreset);
DefineQ(Faust::GraphStyle::ApplyLayoutPreset);
DefineQ(Faust::Graph::ShowSaveSvgDialog);
DefineQ(Faust::Graph::SaveSvgFile);
DefineQ(FileDialog::Open);
DefineQ(FileDialog::Select);
