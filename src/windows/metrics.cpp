#include "../context.h"
#include "../imgui_helpers.h"

using namespace ImGui;

namespace FlowGrid {

// Conforms to the [JSON patch](http://jsonpatch.com/) spec.
// TODO deserialize into a `Patch` struct
void ShowJsonPatchOpMetrics(const json &patch_op) {
    const std::string &path = patch_op["path"];
    const std::string &op = patch_op["op"];
    const std::string &value = patch_op["value"].dump();
    BulletText("Path: %s", path.c_str());
    BulletText("Op: %s", op.c_str());
    BulletText("Value:\n%s", value.c_str());
}

void ShowJsonPatchMetrics(const json &patch) {
    if (patch.size() == 1) {
        ShowJsonPatchOpMetrics(patch[0]);
    } else {
        for (size_t i = 0; i < patch.size(); i++) {
            if (TreeNodeEx(std::to_string(i).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ShowJsonPatchOpMetrics(patch[i]);
                TreePop();
            }
        }
    }
}

void ShowDiffMetrics(const BidirectionalStateDiff &diff) {
    if (diff.action_names.size() == 1) {
        BulletText("Action name: %s", (*diff.action_names.begin()).c_str());
    } else {
        if (TreeNode("Actions", "%lu actions", diff.action_names.size())) {
            for (const auto &action_name: diff.action_names) {
                BulletText("%s", action_name.c_str());
            }
            TreePop();
        }
    }
    if (TreeNode("Forward diff")) {
        ShowJsonPatchMetrics(diff.forward_patch);
        TreePop();
    }
    if (TreeNode("Reverse diff")) {
        ShowJsonPatchMetrics(diff.reverse_patch);
        TreePop();
    }

    // TODO add https://github.com/fmtlib/fmt and use e.g.
    //    std::format("{:%Y/%m/%d %T}", tSysTime);
    BulletText("Nanos: %llu", diff.system_time.time_since_epoch().count());
    TreePop();
}

void ShowMetrics() {
    if (c.diffs.empty()) BeginDisabled();
    if (TreeNode("Diffs", "Diffs (%lu)", c.diffs.size())) {
        for (size_t i = 0; i < c.diffs.size(); i++) {
            if (TreeNode(std::to_string(i).c_str())) {
                ShowDiffMetrics(c.diffs[i]);
            }
        }
        TreePop();
    }
    if (c.diffs.empty()) EndDisabled();

    if (TreeNode("Actions")) {
        Text("Action variant size: %lu bytes", sizeof(Action));
        SameLine();
        HelpMarker("All actions are internally stored in an `std::variant`, which must be large enough to hold its largest type. "
                   "Thus, it's important to keep action data small.");

        TreePop();
    }
}

}

void State::Metrics::draw() {
    if (BeginTabBar("##tabs")) {
        if (BeginTabItem("FlowGrid")) {
            FlowGrid::ShowMetrics();
            EndTabItem();
        }
        if (BeginTabItem("ImGui")) {
            ShowMetrics();
            EndTabItem();
        }
        if (BeginTabItem("ImPlot")) {
            ImPlot::ShowMetrics();
            EndTabItem();
        }
        EndTabBar();
    }
}
