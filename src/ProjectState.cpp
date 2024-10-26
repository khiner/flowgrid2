#include "ProjectState.h"

#include "imgui_internal.h"
#include "implot.h"

#include "Core/TextEditor/TextEditor.h"
#include "Core/UI/JsonTree.h"

#include "Project/ProjectContext.h"

using namespace flowgrid;

ProjectState::ProjectState(Store &store, ActionableProducer::Enqueue q, const ::ProjectContext &project_context)
    : Component(store, "ProjectState", PrimitiveQ, project_context), ActionableProducer(std::move(q)) {
    Windows.SetWindowComponents({
        Audio.Graph,
        Audio.Graph.Connections,
        Audio.Style,
        Settings,
        Audio.Faust.FaustDsps,
        Audio.Faust.Logs,
        Audio.Faust.Graphs,
        Audio.Faust.Paramss,
        Debug,
        Debug.StatePreview,
        Debug.StorePathUpdateFrequency,
        Debug.DebugLog,
        Debug.StackTool,
        Debug.Metrics,
        Style,
        Demo,
        Info,
    });
}

ProjectState::~ProjectState() = default;

void ProjectState::Apply(const ActionType &action) const {
    std::visit(
        Match{
            [this](Action::Audio::Any &&a) { Audio.Apply(std::move(a)); },
        },
        action
    );
}

bool ProjectState::CanApply(const ActionType &action) const {
    return std::visit(
        Match{
            [this](Action::Audio::Any &&a) { return Audio.CanApply(std::move(a)); },
        },
        action
    );
}

using namespace ImGui;

void ProjectState::Render() const {
    // Good initial layout setup example in this issue: https://github.com/ocornut/imgui/issues/3548
    auto dockspace_id = DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    int frame_count = GetCurrentContext()->FrameCount;
    if (frame_count == 1) {
        auto debug_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.3f, nullptr, &dockspace_id);
        auto metrics_node_id = DockBuilderSplitNode(debug_node_id, ImGuiDir_Right, 0.3f, nullptr, &debug_node_id);
        auto utilities_node_id = DockBuilderSplitNode(debug_node_id, ImGuiDir_Left, 0.3f, nullptr, &debug_node_id);

        auto info_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.2f, nullptr, &dockspace_id);
        auto settings_node_id = DockBuilderSplitNode(info_node_id, ImGuiDir_Down, 0.25f, nullptr, &info_node_id);

        auto audio_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.25f, nullptr, &dockspace_id);
        auto faust_tools_node_id = DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.5f, nullptr, &dockspace_id);
        auto faust_graph_node_id = DockBuilderSplitNode(faust_tools_node_id, ImGuiDir_Left, 0.5f, nullptr, &faust_tools_node_id);
        DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.5f, nullptr, &dockspace_id); // text editor

        Audio.Graph.Dock(audio_node_id);
        Audio.Graph.Connections.Dock(audio_node_id);
        Audio.Style.Dock(audio_node_id);

        Audio.Faust.FaustDsps.Dock(dockspace_id);
        Audio.Faust.Graphs.Dock(faust_graph_node_id);
        Audio.Faust.Paramss.Dock(faust_tools_node_id);
        Audio.Faust.Logs.Dock(faust_tools_node_id);

        Debug.Dock(debug_node_id);
        Debug.StatePreview.Dock(debug_node_id);
        Debug.StorePathUpdateFrequency.Dock(debug_node_id);
        Debug.DebugLog.Dock(debug_node_id);
        Debug.StackTool.Dock(debug_node_id);
        Debug.Metrics.Dock(metrics_node_id);

        Style.Dock(utilities_node_id);
        Demo.Dock(utilities_node_id);

        Info.Dock(info_node_id);
        Settings.Dock(settings_node_id);
    }

    // Draw non-window children.
    for (const auto *child : Children) {
        if (!Windows.IsWindow(child->Id) && child != &Windows) child->Draw();
    }

    Windows.Draw();

    if (frame_count == 1) {
        // Default focused windows.
        Style.Focus();
        Audio.Graph.Focus();
        Audio.Faust.Graphs.Focus();
        Audio.Faust.Paramss.Focus();
        Debug.Focus(); // not visible by default anymore
    }
}

void ProjectState::Debug::StorePathUpdateFrequency::Render() const {
    ProjectContext.RenderStorePathChangeFrequency();
}

void ProjectState::Debug::DebugLog::Render() const {
    ShowDebugLogWindow();
}
void ProjectState::Debug::StackTool::Render() const {
    ShowIDStackToolWindow();
}

void ProjectState::Debug::Metrics::ImGuiMetrics::Render() const { ImGui::ShowMetricsWindow(); }
void ProjectState::Debug::Metrics::ImPlotMetrics::Render() const { ImPlot::ShowMetricsWindow(); }

void ProjectState::Debug::OnComponentChanged() {
    if (AutoSelect.IsChanged()) {
        WindowFlags = AutoSelect ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None;
    }
}

void ProjectState::RenderDebug() const {
    const bool auto_select = Debug.AutoSelect;
    if (auto_select) BeginDisabled();
    RenderValueTree(Debug::LabelModeType(int(Debug.LabelMode)) == Debug::LabelModeType::Annotated, auto_select);
    if (auto_select) EndDisabled();
}

void ProjectState::Debug::StatePreview::Render() const {
    Format.Draw();
    Raw.Draw();

    Separator();

    json project_json = ProjectContext.GetProjectJson(ProjectFormat(int(Format)));
    if (Raw) {
        TextUnformatted(project_json.dump(4));
    } else {
        SetNextItemOpen(true);
        fg::JsonTree("", std::move(project_json));
    }
}

void ProjectState::Debug::Metrics::Render() const {
    RenderTabs();
}

void ProjectState::Debug::Metrics::ProjectMetrics::Render() const {
    ProjectContext.RenderMetrics();
}
