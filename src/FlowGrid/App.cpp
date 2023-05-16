#include "Store/Store.h"

#include <iostream>

#include "immer/map.hpp"
#include "immer/map_transient.hpp"

namespace Field {
template<IsPrimitive T> StorePath Vector<T>::PathAt(const Count i) const { return Path / to_string(i); }
template<IsPrimitive T> Count Vector<T>::Size() const { return Value.size(); }
template<IsPrimitive T> T Vector<T>::operator[](const Count i) const { return Value[i]; }

template<IsPrimitive T>
void Vector<T>::Set(const vector<T> &values, TransientStore &store) const {
    Count i = 0;
    while (i < values.size()) {
        store.set(PathAt(i), T(values[i])); // When T is a bool, an explicit cast seems to be needed?
        i++;
    }
    while (store.count(PathAt(i))) {
        store.erase(PathAt(i));
        i++;
    }
}

template<IsPrimitive T>
void Vector<T>::Set(const vector<std::pair<int, T>> &values, TransientStore &store) const {
    for (const auto &[i, value] : values) store.set(PathAt(i), value);
}

template<IsPrimitive T>
void Vector<T>::Update() {
    Count i = 0;
    while (AppStore.count(PathAt(i))) {
        const T value = std::get<T>(AppStore.at(PathAt(i)));
        if (Value.size() == i) Value.push_back(value);
        else Value[i] = value;
        i++;
    }
    Value.resize(i);
}

template<IsPrimitive T> StorePath Vector2D<T>::PathAt(const Count i, const Count j) const { return Path / to_string(i) / to_string(j); }
template<IsPrimitive T> Count Vector2D<T>::Size() const { return Value.size(); };
template<IsPrimitive T> Count Vector2D<T>::Size(Count i) const { return Value[i].size(); };
template<IsPrimitive T> T Vector2D<T>::operator()(Count i, Count j) const { return Value[i][j]; }

template<IsPrimitive T>
void Vector2D<T>::Set(const vector<vector<T>> &values, TransientStore &store) const {
    Count i = 0;
    while (i < values.size()) {
        Count j = 0;
        while (j < values[i].size()) {
            store.set(PathAt(i, j), T(values[i][j]));
            j++;
        }
        while (store.count(PathAt(i, j))) store.erase(PathAt(i, j++));
        i++;
    }

    while (store.count(PathAt(i, 0))) {
        Count j = 0;
        while (store.count(PathAt(i, j))) store.erase(PathAt(i, j++));
        i++;
    }
}

template<IsPrimitive T>
void Vector2D<T>::Update() {
    Count i = 0;
    while (AppStore.count(PathAt(i, 0))) {
        if (Value.size() == i) Value.push_back({});
        Count j = 0;
        while (AppStore.count(PathAt(i, j))) {
            const T value = std::get<T>(AppStore.at(PathAt(i, j)));
            if (Value[i].size() == j) Value[i].push_back(value);
            else Value[i][j] = value;
            j++;
        }
        Value[i].resize(j);
        i++;
    }
    Value.resize(i);
}
template<IsPrimitive T>
StorePath Matrix<T>::PathAt(const Count row, const Count col) const { return Path / to_string(row) / to_string(col); }
template<IsPrimitive T> Count Matrix<T>::Rows() const { return RowCount; }
template<IsPrimitive T> Count Matrix<T>::Cols() const { return ColCount; }
template<IsPrimitive T> T Matrix<T>::operator()(const Count row, const Count col) const { return Data[row * ColCount + col]; }

template<IsPrimitive T>
void Matrix<T>::Update() {
    Count row_count = 0, col_count = 0;
    while (AppStore.count(PathAt(row_count, 0))) { row_count++; }
    while (AppStore.count(PathAt(row_count - 1, col_count))) { col_count++; }
    RowCount = row_count;
    ColCount = col_count;
    Data.resize(RowCount * ColCount);

    for (Count row = 0; row < RowCount; row++) {
        for (Count col = 0; col < ColCount; col++) {
            Data[row * ColCount + col] = std::get<T>(AppStore.at(PathAt(row, col)));
        }
    }
}
} // namespace Field

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"

#include "App.h"
#include "FileDialog/FileDialogDataJson.h"
#include "Helper/File.h"
#include "ProjectConstants.h"

using namespace nlohmann;

// todo should be in `Store`, but first need to separate out the core action stuff so `Store.h` can include `Action/Core.h` and define its own actions.
namespace store {
void Apply(const StoreAction &action, TransientStore &store) {
    Match(
        action,
        [&store](const SetValue &a) { Set(a.path, a.value, store); },
        [&store](const SetValues &a) { Set(a.values, store); },
        [&store](const SetVector &a) { Set(a.path, a.value, store); },
        [&store](const SetMatrix &a) { Set(a.path, a.data, a.row_count, store); },
        [&store](const ToggleValue &a) { Set(a.path, !std::get<bool>(store::Get(a.path)), store); },
        [&store](const Actions::ApplyPatch &a) { ApplyPatch(a.patch, store); },
    );
}
} // namespace store

void State::Apply(const StatefulAction &action, TransientStore &store) const {
    Match(
        action,
        [&store](const StoreAction &a) { store::Apply(a, store); },
        [&](const FileDialogAction &a) { FileDialog.Apply(a, store); },
        [&](const StyleAction &a) {
            Match(
                a,
                // todo enum types instead of raw integers
                [&](const SetImGuiColorStyle &a) {
                    switch (a.id) {
                        case 0: return Style.ImGui.ColorsDark(store);
                        case 1: return Style.ImGui.ColorsLight(store);
                        case 2: return Style.ImGui.ColorsClassic(store);
                    }
                },
                [&](const SetImPlotColorStyle &a) {
                    switch (a.id) {
                        case 0: return Style.ImPlot.ColorsAuto(store);
                        case 1: return Style.ImPlot.ColorsDark(store);
                        case 2: return Style.ImPlot.ColorsLight(store);
                        case 3: return Style.ImPlot.ColorsClassic(store);
                    }
                },
                [&](const SetFlowGridColorStyle &a) {
                    switch (a.id) {
                        case 0: return Style.FlowGrid.ColorsDark(store);
                        case 1: return Style.FlowGrid.ColorsLight(store);
                        case 2: return Style.FlowGrid.ColorsClassic(store);
                    }
                },
                [&](const SetGraphColorStyle &a) {
                    switch (a.id) {
                        case 0: return Audio.Faust.Graph.Style.ColorsDark(store);
                        case 1: return Audio.Faust.Graph.Style.ColorsLight(store);
                        case 2: return Audio.Faust.Graph.Style.ColorsClassic(store);
                        case 3: return Audio.Faust.Graph.Style.ColorsFaust(store);
                    }
                },
                [&](const SetGraphLayoutStyle &a) {
                    switch (a.id) {
                        case 0: return Audio.Faust.Graph.Style.LayoutFlowGrid(store);
                        case 1: return Audio.Faust.Graph.Style.LayoutFaust(store);
                    }
                },
            );
        },
        [&](const ShowOpenProjectDialog &) { FileDialog.Set({"Choose file", AllProjectExtensionsDelimited, ".", ""}, store); },
        [&](const ShowSaveProjectDialog &) { FileDialog.Set({"Choose file", AllProjectExtensionsDelimited, ".", "my_flowgrid_project", true, 1}, store); },
        [&](const ShowOpenFaustFileDialog &) { FileDialog.Set({"Choose file", FaustDspFileExtension, ".", ""}, store); },
        [&](const ShowSaveFaustFileDialog &) { FileDialog.Set({"Choose file", FaustDspFileExtension, ".", "my_dsp", true, 1}, store); },
        [&](const ShowSaveFaustSvgFileDialog &) { FileDialog.Set({"Choose directory", ".*", ".", "faust_graph", true, 1}, store); },

        [&](const OpenFaustFile &a) { store::Set(Audio.Faust.Code, FileIO::read(a.path), store); },
        [&](const CloseApplication &) { store::Set({{UiProcess.Running, false}, {Audio.Device.On, false}}, store); },
    );
}

using namespace ImGui;

void State::UIProcess::Render() const {}

void State::Render() const {
    MainMenu.Draw();

    // Good initial layout setup example in this issue: https://github.com/ocornut/imgui/issues/3548
    auto dockspace_id = DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    int frame_count = GetCurrentContext()->FrameCount;
    if (frame_count == 1) {
        auto settings_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.25f, nullptr, &dockspace_id);
        auto utilities_node_id = DockBuilderSplitNode(settings_node_id, ImGuiDir_Down, 0.5f, nullptr, &settings_node_id);

        auto debug_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.3f, nullptr, &dockspace_id);
        auto metrics_node_id = DockBuilderSplitNode(debug_node_id, ImGuiDir_Right, 0.35f, nullptr, &debug_node_id);

        auto info_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.2f, nullptr, &dockspace_id);
        auto faust_tools_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.5f, nullptr, &dockspace_id);
        auto faust_editor_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.7f, nullptr, &dockspace_id);

        Audio.Dock(settings_node_id);
        ApplicationSettings.Dock(settings_node_id);

        Audio.Faust.Editor.Dock(faust_editor_node_id);
        Audio.Faust.Editor.Metrics.Dock(dockspace_id); // What's remaining of the main dockspace after splitting is used for the editor metrics.
        Audio.Faust.Log.Dock(faust_tools_node_id);
        Audio.Faust.Graph.Dock(faust_tools_node_id);
        Audio.Faust.Params.Dock(faust_tools_node_id);

        Debug.StateViewer.Dock(debug_node_id);
        Debug.ProjectPreview.Dock(debug_node_id);
        // Debug.StateMemoryEditor.Dock(debug_node_id);
        Debug.StorePathUpdateFrequency.Dock(debug_node_id);
        Debug.DebugLog.Dock(debug_node_id);
        Debug.StackTool.Dock(debug_node_id);
        Debug.Metrics.Dock(metrics_node_id);

        Style.Dock(utilities_node_id);
        Demo.Dock(utilities_node_id);

        Info.Dock(info_node_id);
    } else if (frame_count == 2) {
        // Doesn't work on the first draw: https://github.com/ocornut/imgui/issues/2304
        Style.SelectTab();
        Audio.SelectTab();
        Audio.Faust.Graph.SelectTab();
        Debug.StateViewer.SelectTab(); // not visible by default anymore
    }

    // Draw non-window children.
    for (const auto *child : Children) {
        if (const auto *ui_child = dynamic_cast<const UIStateMember *>(child)) {
            if (!dynamic_cast<const Window *>(child)) {
                ui_child->Draw();
            }
        }
    }
    // Recursively draw all windows.
    DrawWindows();
}

constexpr U32 PackImVec2ih(const ImVec2ih &unpacked) { return (U32(unpacked.x) << 16) + U32(unpacked.y); }
constexpr ImVec2ih UnpackImVec2ih(const U32 packed) { return {S16(U32(packed) >> 16), S16(U32(packed) & 0xffff)}; }

// Copy of ImGui version, which is not defined publicly
struct ImGuiDockNodeSettings { // NOLINT(cppcoreguidelines-pro-type-member-init)
    ID NodeId;
    ID ParentNodeId;
    ID ParentWindowId;
    ID SelectedTabId;
    S8 SplitAxis;
    char Depth;
    ImGuiDockNodeFlags Flags;
    ImVec2ih Pos;
    ImVec2ih Size;
    ImVec2ih SizeRef;
};

void DockNodeSettings::Set(const ImVector<ImGuiDockNodeSettings> &dss, TransientStore &store) const {
    const Count size = dss.Size;
    vector<ID> node_id(size), parent_node_id(size), parent_window_id(size), selected_tab_id(size);
    vector<int> split_axis(size), depth(size), flags(size);
    vector<U32> pos(size), sz(size), sz_ref(size);
    for (Count i = 0; i < size; i++) {
        const auto &ds = dss[int(i)];
        node_id[i] = ds.NodeId;
        parent_node_id[i] = ds.ParentNodeId;
        parent_window_id[i] = ds.ParentWindowId;
        selected_tab_id[i] = ds.SelectedTabId;
        split_axis[i] = ds.SplitAxis;
        depth[i] = ds.Depth;
        flags[i] = ds.Flags;
        pos[i] = PackImVec2ih(ds.Pos);
        sz[i] = PackImVec2ih(ds.Size);
        sz_ref[i] = PackImVec2ih(ds.SizeRef);
    }
    NodeId.Set(node_id, store);
    ParentNodeId.Set(parent_node_id, store);
    ParentWindowId.Set(parent_window_id, store);
    SelectedTabId.Set(selected_tab_id, store);
    SplitAxis.Set(split_axis, store);
    Depth.Set(depth, store);
    Flags.Set(flags, store);
    Pos.Set(pos, store);
    Size.Set(sz, store);
    SizeRef.Set(sz_ref, store);
}
void DockNodeSettings::Apply(ImGuiContext *ctx) const {
    // Assumes `DockSettingsHandler_ClearAll` has already been called.
    const auto size = NodeId.Size();
    for (Count i = 0; i < size; i++) {
        ctx->DockContext.NodesSettings.push_back({
            NodeId[i],
            ParentNodeId[i],
            ParentWindowId[i],
            SelectedTabId[i],
            S8(SplitAxis[i]),
            char(Depth[i]),
            Flags[i],
            UnpackImVec2ih(Pos[i]),
            UnpackImVec2ih(Size[i]),
            UnpackImVec2ih(SizeRef[i]),
        });
    }
}

void WindowSettings::Set(ImChunkStream<ImGuiWindowSettings> &wss, TransientStore &store) const {
    vector<ID> id, class_id, viewport_id, dock_id;
    vector<int> dock_order;
    vector<U32> pos, sz, viewport_pos;
    vector<bool> collapsed;
    for (auto *ws = wss.begin(); ws != nullptr; ws = wss.next_chunk(ws)) {
        id.push_back(ws->ID);
        class_id.push_back(ws->ClassId);
        viewport_id.push_back(ws->ViewportId);
        dock_id.push_back(ws->DockId);
        dock_order.push_back(ws->DockOrder);
        pos.push_back(PackImVec2ih(ws->Pos));
        sz.push_back(PackImVec2ih(ws->Size));
        viewport_pos.push_back(PackImVec2ih(ws->ViewportPos));
        collapsed.push_back(ws->Collapsed);
    }
    Id.Set(id, store);
    ClassId.Set(class_id, store);
    ViewportId.Set(viewport_id, store);
    DockId.Set(dock_id, store);
    DockOrder.Set(dock_order, store);
    Pos.Set(pos, store);
    Size.Set(sz, store);
    ViewportPos.Set(viewport_pos, store);
    Collapsed.Set(collapsed, store);
}

// See `imgui.cpp::ApplyWindowSettings`
void WindowSettings::Apply(ImGuiContext *) const {
    const auto *main_viewport = GetMainViewport();
    const auto size = Id.Size();
    for (Count i = 0; i < size; i++) {
        const auto id = Id[i];
        auto *window = FindWindowByID(id);
        if (!window) {
            std::cerr << "Unable to apply settings for window with ID " << std::format("{:#08X}", id) << ": Window not found.\n";
            continue;
        }

        window->ViewportPos = main_viewport->Pos;
        if (ViewportId[i]) {
            window->ViewportId = ViewportId[i];
            const auto viewport_pos = UnpackImVec2ih(ViewportPos[i]);
            window->ViewportPos = ImVec2(viewport_pos.x, viewport_pos.y);
        }
        const auto pos = UnpackImVec2ih(Pos[i]);
        window->Pos = ImVec2(pos.x, pos.y) + ImFloor(window->ViewportPos);

        const auto size = UnpackImVec2ih(Size[i]);
        if (size.x > 0 && size.y > 0) window->Size = window->SizeFull = ImVec2(size.x, size.y);
        window->Collapsed = Collapsed[i];
        window->DockId = DockId[i];
        window->DockOrder = short(DockOrder[i]);
    }
}

void TableSettings::Set(ImChunkStream<ImGuiTableSettings> &tss, TransientStore &store) const {
    // Table settings
    vector<ImGuiID> id;
    vector<int> save_flags;
    vector<float> ref_scale;
    vector<Count> columns_counts, columns_count_max;
    vector<bool> want_apply;

    // Column settings
    vector<vector<float>> width_or_weight;
    vector<vector<::ID>> user_id;
    vector<vector<int>> index, display_order, sort_order, sort_direction;
    vector<vector<bool>> is_enabled, is_stretch;

    for (auto *ts_it = tss.begin(); ts_it != nullptr; ts_it = tss.next_chunk(ts_it)) {
        auto &ts = *ts_it;
        const auto columns_count = ts.ColumnsCount;

        id.push_back(ts.ID);
        save_flags.push_back(ts.SaveFlags);
        ref_scale.push_back(ts.RefScale);
        columns_counts.push_back(columns_count);
        columns_count_max.push_back(ts.ColumnsCountMax);
        want_apply.push_back(ts.WantApply);

        width_or_weight.push_back(vector<float>(columns_count));
        user_id.push_back(vector<::ID>(columns_count));
        index.push_back(vector<int>(columns_count));
        display_order.push_back(vector<int>(columns_count));
        sort_order.push_back(vector<int>(columns_count));
        sort_direction.push_back(vector<int>(columns_count));
        is_enabled.push_back(vector<bool>(columns_count));
        is_stretch.push_back(vector<bool>(columns_count));

        for (int column_index = 0; column_index < columns_count; column_index++) {
            const auto &cs = ts.GetColumnSettings()[column_index];
            width_or_weight.back()[column_index] = cs.WidthOrWeight;
            user_id.back()[column_index] = cs.UserID;
            index.back()[column_index] = cs.Index;
            display_order.back()[column_index] = cs.DisplayOrder;
            sort_order.back()[column_index] = cs.SortOrder;
            sort_direction.back()[column_index] = cs.SortDirection;
            is_enabled.back()[column_index] = cs.IsEnabled;
            is_stretch.back()[column_index] = cs.IsStretch;
        }
    }

    ID.Set(id, store);
    SaveFlags.Set(save_flags, store);
    RefScale.Set(ref_scale, store);
    ColumnsCount.Set(columns_counts, store);
    ColumnsCountMax.Set(columns_count_max, store);
    WantApply.Set(want_apply, store);

    Columns.WidthOrWeight.Set(width_or_weight, store);
    Columns.UserID.Set(user_id, store);
    Columns.Index.Set(index, store);
    Columns.DisplayOrder.Set(display_order, store);
    Columns.SortOrder.Set(sort_order, store);
    Columns.SortDirection.Set(sort_direction, store);
    Columns.IsEnabled.Set(is_enabled, store);
    Columns.IsStretch.Set(is_stretch, store);
}

// Adapted from `imgui_tables.cpp::TableLoadSettings`
void TableSettings::Apply(ImGuiContext *) const {
    const auto size = ID.Size();
    for (Count i = 0; i < size; i++) {
        const auto id = ID[i];
        const auto table = TableFindByID(id);
        if (!table) {
            std::cerr << "Unable to apply settings for table with ID " << std::format("{:#08X}", id) << ": Table not found.\n";
            continue;
        }

        table->IsSettingsRequestLoad = false; // todo remove this var/behavior?
        table->SettingsLoadedFlags = SaveFlags[i]; // todo remove this var/behavior?
        table->RefScale = RefScale[i];

        // Serialize ImGuiTableSettings/ImGuiTableColumnSettings into ImGuiTable/ImGuiTableColumn
        ImU64 display_order_mask = 0;
        for (Count j = 0; j < ColumnsCount[i]; j++) {
            int column_n = Columns.Index(i, j);
            if (column_n < 0 || column_n >= table->ColumnsCount) continue;

            ImGuiTableColumn *column = &table->Columns[column_n];
            if (ImGuiTableFlags(SaveFlags[i]) & ImGuiTableFlags_Resizable) {
                float width_or_weight = Columns.WidthOrWeight(i, j);
                if (Columns.IsStretch(i, j)) column->StretchWeight = width_or_weight;
                else column->WidthRequest = width_or_weight;
                column->AutoFitQueue = 0x00;
            }
            column->DisplayOrder = ImGuiTableFlags(SaveFlags[i]) & ImGuiTableFlags_Reorderable ? ImGuiTableColumnIdx(Columns.DisplayOrder(i, j)) : (ImGuiTableColumnIdx)column_n;
            display_order_mask |= (ImU64)1 << column->DisplayOrder;
            column->IsUserEnabled = column->IsUserEnabledNextFrame = Columns.IsEnabled(i, j);
            column->SortOrder = ImGuiTableColumnIdx(Columns.SortOrder(i, j));
            column->SortDirection = Columns.SortDirection(i, j);
        }

        // Validate and fix invalid display order data
        const ImU64 expected_display_order_mask = ColumnsCount[i] == 64 ? ~0 : ((ImU64)1 << ImU8(ColumnsCount[i])) - 1;
        if (display_order_mask != expected_display_order_mask) {
            for (int column_n = 0; column_n < table->ColumnsCount; column_n++) {
                table->Columns[column_n].DisplayOrder = (ImGuiTableColumnIdx)column_n;
            }
        }
        // Rebuild index
        for (int column_n = 0; column_n < table->ColumnsCount; column_n++) {
            table->DisplayOrderToIndex[table->Columns[column_n].DisplayOrder] = (ImGuiTableColumnIdx)column_n;
        }
    }
}

Patch ImGuiSettings::CreatePatch(ImGuiContext *ctx) const {
    SaveIniSettingsToMemory(); // Populate the `Settings` context members.

    auto store = AppStore.transient();
    Nodes.Set(ctx->DockContext.NodesSettings, store);
    Windows.Set(ctx->SettingsWindows, store);
    Tables.Set(ctx->SettingsTables, store);

    return store::CreatePatch(AppStore, store.persistent(), Path);
}

void ImGuiSettings::Apply(ImGuiContext *ctx) const {
    DockSettingsHandler_ClearAll(ctx, nullptr);
    Windows.Apply(ctx);
    Tables.Apply(ctx);
    Nodes.Apply(ctx);
    DockSettingsHandler_ApplyAll(ctx, nullptr);

    // Other housekeeping to emulate `LoadIniSettingsFromMemory`
    ctx->SettingsLoaded = true;
    ctx->SettingsDirty = false;
}

//-----------------------------------------------------------------------------
// [SECTION] State windows
//-----------------------------------------------------------------------------

#include "Helper/String.h"
#include "Store/StoreHistory.h"
#include "UI/Widgets.h"

ImRect RowItemRatioRect(float ratio) {
    const ImVec2 row_min = {GetWindowPos().x, GetCursorScreenPos().y};
    return {row_min, row_min + ImVec2{GetWindowWidth() * std::clamp(ratio, 0.f, 1.f), GetFontSize()}};
}

using namespace FlowGrid;

void FillRowItemBg(const U32 col = style.ImGui.Colors[ImGuiCol_FrameBgActive]) {
    const ImVec2 row_min = {GetWindowPos().x, GetCursorScreenPos().y};
    const ImRect &rect = {row_min, row_min + ImVec2{GetWindowWidth(), GetFontSize()}};
    GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max, col);
}

// TODO option to indicate relative update-recency
void Debug::StateViewer::StateJsonTree(string_view key, const json &value, const StorePath &path) const {
    const string leaf_name = path == RootPath ? path.string() : path.filename().string();
    const auto &parent_path = path == RootPath ? path : path.parent_path();
    const bool is_array_item = StringHelper::IsInteger(leaf_name);
    const int array_index = is_array_item ? std::stoi(leaf_name) : -1;
    const bool is_imgui_color = parent_path == style.ImGui.Colors.Path;
    const bool is_implot_color = parent_path == style.ImPlot.Colors.Path;
    const bool is_flowgrid_color = parent_path == style.FlowGrid.Colors.Path;
    const string label = LabelMode == Annotated ?
        (is_imgui_color        ? style.ImGui.Colors.Child(array_index)->Name :
             is_implot_color   ? style.ImPlot.Colors.Child(array_index)->Name :
             is_flowgrid_color ? style.FlowGrid.Colors.Child(array_index)->Name :
             is_array_item     ? leaf_name :
                                 string(key)) :
        string(key);

    if (AutoSelect) {
        const auto &updated_paths = History.LatestUpdatedPaths;
        const auto is_ancestor_path = [&path](const string &candidate_path) { return candidate_path.rfind(path.string(), 0) == 0; };
        const bool was_recently_updated = std::find_if(updated_paths.begin(), updated_paths.end(), is_ancestor_path) != updated_paths.end();
        SetNextItemOpen(was_recently_updated);
    }

    // Flash background color of nodes when its corresponding path updates.
    const auto &latest_update_time = History.LatestUpdateTime(path);
    if (latest_update_time) {
        const float flash_elapsed_ratio = fsec(Clock::now() - *latest_update_time).count() / style.FlowGrid.FlashDurationSec;
        ImColor flash_color = style.FlowGrid.Colors[FlowGridCol_GestureIndicator];
        flash_color.Value.w = std::max(0.f, 1 - flash_elapsed_ratio);
        FillRowItemBg(flash_color);
    }

    JsonTreeNodeFlags flags = JsonTreeNodeFlags_None;
    if (LabelMode == Annotated && (is_imgui_color || is_implot_color || is_flowgrid_color)) flags |= JsonTreeNodeFlags_Highlighted;
    if (AutoSelect) flags |= JsonTreeNodeFlags_Disabled;

    // The rest below is structurally identical to `fg::Widgets::JsonTree`.
    // Couldn't find an easy/clean way to inject the above into each recursive call.
    if (value.is_null()) {
        TextUnformatted(label.c_str());
    } else if (value.is_object()) {
        if (JsonTreeNode(label, flags)) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                StateJsonTree(it.key(), *it, path / it.key());
            }
            TreePop();
        }
    } else if (value.is_array()) {
        if (JsonTreeNode(label, flags)) {
            Count i = 0;
            for (const auto &it : value) {
                StateJsonTree(to_string(i), it, path / to_string(i));
                i++;
            }
            TreePop();
        }
    } else {
        JsonTreeNode(label, flags, nullptr, value.dump().c_str());
    }
}

#include "Store/StoreJson.h"

void Debug::StateViewer::Render() const {
    StateJsonTree("State", GetStoreJson());
}

void Debug::ProjectPreview::Render() const {
    Format.Draw();
    Raw.Draw();

    Separator();

    const json project_json = GetStoreJson(StoreJsonFormat(int(Format)));
    if (Raw) TextUnformatted(project_json.dump(4).c_str());
    else JsonTree("", project_json, JsonTreeNodeFlags_DefaultOpen);
}

void Style::FlowGridStyle::Render() const {
    static int colors_idx = -1, graph_colors_idx = -1, graph_layout_idx = -1;
    if (Combo("Colors", &colors_idx, "Dark\0Light\0Classic\0")) q(SetFlowGridColorStyle{colors_idx});
    if (Combo("Graph colors", &graph_colors_idx, "Dark\0Light\0Classic\0Faust\0")) q(SetGraphColorStyle{graph_colors_idx});
    if (Combo("Graph layout", &graph_layout_idx, "FlowGrid\0Faust\0")) q(SetGraphLayoutStyle{graph_layout_idx});
    FlashDurationSec.Draw();

    if (BeginTabBar("")) {
        if (BeginTabItem("Matrix mixer", nullptr, ImGuiTabItemFlags_NoPushId)) {
            audio.Graph.Style.Matrix.Draw();
            EndTabItem();
        }
        if (BeginTabItem("Faust graph", nullptr, ImGuiTabItemFlags_NoPushId)) {
            audio.Faust.Graph.Style.Draw();
            EndTabItem();
        }
        if (BeginTabItem("Faust params", nullptr, ImGuiTabItemFlags_NoPushId)) {
            audio.Faust.Params.Style.Draw();
            EndTabItem();
        }
        if (BeginTabItem(Colors.ImGuiLabel.c_str(), nullptr, ImGuiTabItemFlags_NoPushId)) {
            Colors.Draw();
        }
        EndTabBar();
    }
}

#include "AppPreferences.h"

void OpenRecentProject::MenuItem() const {
    if (BeginMenu("Open recent project", !Preferences.RecentlyOpenedPaths.empty())) {
        for (const auto &recently_opened_path : Preferences.RecentlyOpenedPaths) {
            if (ImGui::MenuItem(recently_opened_path.filename().c_str())) q(OpenProject{recently_opened_path});
        }
        EndMenu();
    }
}

#include "date.h"

void ApplicationSettings::Render() const {
    int value = int(History.Index);
    if (SliderInt("History index", &value, 0, int(History.Size() - 1))) q(SetHistoryIndex{value});
    GestureDurationSec.Draw();
}

Demo::Demo(StateMember *parent, string_view path_segment, string_view name_help)
    : TabsWindow(parent, path_segment, name_help, ImGuiWindowFlags_MenuBar) {}

void Demo::ImGuiDemo::Render() const {
    ShowDemoWindow();
}
void Demo::ImPlotDemo::Render() const {
    ImPlot::ShowDemoWindow();
}

#include "Action/ActionJson.h"

void ShowGesture(const Gesture &gesture) {
    for (Count action_index = 0; action_index < gesture.size(); action_index++) {
        const auto &[action, time] = gesture[action_index];
        JsonTree(
            std::format("{}: {}", action::GetName(action), date::format("%Y-%m-%d %T", time).c_str()),
            json(action)[1],
            JsonTreeNodeFlags_None,
            to_string(action_index).c_str()
        );
    }
}

#include "PrimitiveJson.h"

void Metrics::FlowGridMetrics::Render() const {
    {
        // Active (uncompressed) gesture
        const bool widget_gesturing = UiContext.IsWidgetGesturing;
        const bool ActiveGesturePresent = !History.ActiveGesture.empty();
        if (ActiveGesturePresent || widget_gesturing) {
            // Gesture completion progress bar
            const auto row_item_ratio_rect = RowItemRatioRect(1 - History.GestureTimeRemainingSec(s.ApplicationSettings.GestureDurationSec) / s.ApplicationSettings.GestureDurationSec);
            GetWindowDrawList()->AddRectFilled(row_item_ratio_rect.Min, row_item_ratio_rect.Max, style.FlowGrid.Colors[FlowGridCol_GestureIndicator]);

            const auto &ActiveGesture_title = "Active gesture"s + (ActiveGesturePresent ? " (uncompressed)" : "");
            if (TreeNodeEx(ActiveGesture_title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (widget_gesturing) FillRowItemBg();
                else BeginDisabled();
                Text("Widget gesture: %s", widget_gesturing ? "true" : "false");
                if (!widget_gesturing) EndDisabled();

                if (ActiveGesturePresent) ShowGesture(History.ActiveGesture);
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
        if (TreeNodeEx("StoreHistory", ImGuiTreeNodeFlags_DefaultOpen, "Store event records (Count: %d, Current index: %d)", History.Size() - 1, History.Index)) {
            for (Count i = 1; i < History.Size(); i++) {
                if (TreeNodeEx(to_string(i).c_str(), i == History.Index ? (ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_DefaultOpen) : ImGuiTreeNodeFlags_None)) {
                    const auto &[committed, store_record, gesture] = History.RecordAt(i);
                    BulletText("Committed: %s\n", date::format("%Y-%m-%d %T", committed).c_str());
                    if (TreeNode("Patch")) {
                        // We compute patches as we need them rather than memoizing them.
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
                    if (TreeNode("Gesture")) {
                        ShowGesture(gesture);
                        TreePop();
                    }
                    if (TreeNode("State")) {
                        JsonTree("", store_record);
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
        Text("Action variant size: %lu bytes", sizeof(Action));
        Text("Primitive variant size: %lu bytes", sizeof(Primitive));
        SameLine();
        HelpMarker("All actions are internally stored in an `std::variant`, which must be large enough to hold its largest type. "
                   "Thus, it's important to keep action data small.");
    }
}

#include "Audio/Faust/FaustGraph.h"

void Info::Render() const {
    const auto hovered_id = GetHoveredID();
    if (!hovered_id) return;

    PushTextWrapPos(0);
    if (StateMember::WithId.contains(hovered_id)) {
        const auto *member = StateMember::WithId.at(hovered_id);
        const string help = member->Help.empty() ? std::format("No info available for \"{}\".", member->Name) : member->Help;
        TextUnformatted(help.c_str());
    } else if (IsBoxHovered(hovered_id)) {
        TextUnformatted(GetBoxInfo(hovered_id).c_str());
    }
    PopTextWrapPos();
}

void Audio::Graph::RenderConnections() const {
    const auto &style = audio.Graph.Style.Matrix;
    const float cell_size = style.CellSize * GetTextLineHeight();
    const float cell_gap = style.CellGap;
    const float label_size = style.LabelSize * GetTextLineHeight(); // Does not include padding.
    const float label_padding = ImGui::GetStyle().ItemInnerSpacing.x;
    const float max_label_w = label_size + 2 * label_padding;
    const ImVec2 grid_top_left = GetCursorScreenPos() + max_label_w;

    BeginGroup();
    // Draw the source channel labels.
    Count source_count = 0;
    for (const auto *source_node : Nodes) {
        if (!source_node->IsSource()) continue;

        const char *label = source_node->Name.c_str();
        const string ellipsified_label = Ellipsify(string(label), label_size);

        SetCursorScreenPos(grid_top_left + ImVec2{(cell_size + cell_gap) * source_count, -max_label_w});
        const auto label_interaction_flags = InvisibleButton({cell_size, max_label_w}, source_node->ImGuiLabel.c_str());
        ImPlot::AddTextVertical(
            GetWindowDrawList(),
            grid_top_left + ImVec2{(cell_size + cell_gap) * source_count + (cell_size - GetTextLineHeight()) / 2, -label_padding},
            GetColorU32(ImGuiCol_Text), ellipsified_label.c_str()
        );
        const bool text_clipped = ellipsified_label.find("...") != string::npos;
        if (text_clipped && (label_interaction_flags & InteractionFlags_Hovered)) SetTooltip("%s", label);
        source_count++;
    }

    // Draw the destination channel labels and mixer cells.
    Count dest_i = 0;
    for (const auto *dest_node : Nodes) {
        if (!dest_node->IsDestination()) continue;

        const char *label = dest_node->Name.c_str();
        const string ellipsified_label = Ellipsify(string(label), label_size);

        SetCursorScreenPos(grid_top_left + ImVec2{-max_label_w, (cell_size + cell_gap) * dest_i});
        const auto label_interaction_flags = InvisibleButton({max_label_w, cell_size}, dest_node->ImGuiLabel.c_str());
        const float label_w = CalcTextSize(ellipsified_label.c_str()).x;
        SetCursorPos(GetCursorPos() + ImVec2{max_label_w - label_w - label_padding, (cell_size - GetTextLineHeight()) / 2}); // Right-align & vertically center label.
        TextUnformatted(ellipsified_label.c_str());
        const bool text_clipped = ellipsified_label.find("...") != string::npos;
        if (text_clipped && (label_interaction_flags & InteractionFlags_Hovered)) SetTooltip("%s", label);

        for (Count source_i = 0; source_i < source_count; source_i++) {
            PushID(dest_i * source_count + source_i);
            SetCursorScreenPos(grid_top_left + ImVec2{(cell_size + cell_gap) * source_i, (cell_size + cell_gap) * dest_i});
            const auto flags = InvisibleButton({cell_size, cell_size}, "Cell");
            if (flags & InteractionFlags_Clicked) q(SetValue{Connections.PathAt(dest_i, source_i), !Connections(dest_i, source_i)});

            const auto fill_color = flags & InteractionFlags_Held ? ImGuiCol_ButtonActive : (flags & InteractionFlags_Hovered ? ImGuiCol_ButtonHovered : (Connections(dest_i, source_i) ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg));
            RenderFrame(GetItemRectMin(), GetItemRectMax(), GetColorU32(fill_color));
            PopID();
        }
        dest_i++;
    }
    EndGroup();
}
