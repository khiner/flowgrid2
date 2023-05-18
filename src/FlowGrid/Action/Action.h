// next up:
// - improve action IDs
//   - actions all get a path in addition to their name (start with all at root, but will be heirarchical soon)
//   - `ID` type generated from path, like `StateMember`:
//     `Id(ImHashStr(ImGuiLabel.c_str(), 0, Parent ? Parent->Id : 0))`
// - Add `Help` string (also like StateMember)
// Move all action declaration & `Apply` handling to domain files.
#pragma once

#include "../Helper/String.h"
#include "../Store/StoreTypesJson.h"

/**
An action is an immutable representation of a user interaction event.
Each action stores all information needed to apply the action to a `Store` instance.
An `ActionMoment` is a combination of any action (`Action::Any`) and the `TimePoint` at which the action happened.

Actions are grouped into `std::variant`s, and thus the byte size of `Action::Any` is large enough to hold its biggest type.
- For actions holding very large structured data, using a JSON string is a good approach to keep the size low
  (at the expense of losing type safety and storing the string contents in heap memory).
- Note that adding static members does not increase the size of the variant(s) it belongs to.
  (You can verify this by looking at the 'Action variant size' in the Metrics->FlowGrid window.)
*/
namespace Action {
#define Define(ActionName, ...)                                                                \
    struct ActionName {                                                                        \
        inline const static std::string Name{StringHelper::PascalToSentenceCase(#ActionName)}; \
        __VA_ARGS__;                                                                           \
    };

template<typename T>
concept Actionable = requires() {
    { T::Name } -> std::same_as<const std::string &>;
    // { T::Id } -> std::same_as<const std::string>;
};

// E.g. `Action::GetName<MyAction>()`
template<Actionable T> std::string GetName() { return T::Name; }

// template<Actionable T> std::string GetId() { return T::Id; }

Define(Undo);
Define(Redo);
Define(SetHistoryIndex, int index;);
Define(OpenProject, std::string path;);
Define(OpenEmptyProject);
Define(OpenDefaultProject);
Define(ShowOpenProjectDialog);
Define(SaveProject, std::string path;);
Define(SaveCurrentProject);
Define(SaveDefaultProject);
Define(ShowSaveProjectDialog);
Define(CloseApplication);
Define(SetValue, StorePath path; Primitive value;);
Define(SetValues, StoreEntries values;);
Define(SetVector, StorePath path; std::vector<Primitive> value;);
Define(SetMatrix, StorePath path; std::vector<Primitive> data; Count row_count;);
Define(ToggleValue, StorePath path;);
Define(ApplyPatch, Patch patch;);
Define(SetImGuiColorStyle, int id;);
Define(SetImPlotColorStyle, int id;);
Define(SetFlowGridColorStyle, int id;);
Define(SetGraphColorStyle, int id;);
Define(SetGraphLayoutStyle, int id;);
Define(ShowOpenFaustFileDialog);
Define(ShowSaveFaustFileDialog);
Define(ShowSaveFaustSvgFileDialog);
Define(SaveFaustFile, std::string path;);
Define(OpenFaustFile, std::string path;);
Define(SaveFaustSvgFile, std::string path;);
Define(OpenFileDialog, std::string dialog_json;);
Define(CloseFileDialog);

// Define json converters for stateful actions (ones that can be saved to a project)
Json(ShowOpenProjectDialog);
Json(CloseFileDialog);
Json(ShowSaveProjectDialog);
Json(CloseApplication);
Json(ShowOpenFaustFileDialog);
Json(ShowSaveFaustFileDialog);
Json(ShowSaveFaustSvgFileDialog);
Json(OpenFileDialog, dialog_json);
Json(SetValue, path, value);
Json(SetValues, values);
Json(SetVector, path, value);
Json(SetMatrix, path, data, row_count);
Json(ToggleValue, path);
Json(ApplyPatch, patch);
Json(SetImGuiColorStyle, id);
Json(SetImPlotColorStyle, id);
Json(SetFlowGridColorStyle, id);
Json(SetGraphColorStyle, id);
Json(SetGraphLayoutStyle, id);
Json(OpenFaustFile, path);

using ProjectAction = std::variant<
    Undo, Redo, SetHistoryIndex,
    OpenProject, OpenEmptyProject, OpenDefaultProject,
    SaveProject, SaveDefaultProject, SaveCurrentProject, SaveFaustFile, SaveFaustSvgFile>;

// Actions that apply directly to the store.
using StoreAction = std::variant<SetValue, SetValues, SetVector, SetMatrix, ToggleValue, ApplyPatch>;

// Domain actions (todo move to their respective domain files).
using FileDialogAction = std::variant<OpenFileDialog, CloseFileDialog>;
using StyleAction = std::variant<SetImGuiColorStyle, SetImPlotColorStyle, SetFlowGridColorStyle, SetGraphColorStyle, SetGraphLayoutStyle>;

using OtherAction = std::variant<
    ShowOpenProjectDialog, ShowSaveProjectDialog, ShowOpenFaustFileDialog, ShowSaveFaustFileDialog, ShowSaveFaustSvgFileDialog, OpenFaustFile,
    CloseApplication>;

// Actions that update state (as opposed to actions that only have non-state-updating side effects, like saving a file).
// These get added to the gesture history, and are saved in a `.fga` (FlowGridAction) project.
using StatefulAction = Combine<StoreAction, FileDialogAction, StyleAction, OtherAction>::type;

// All actions.
using Any = Combine<ProjectAction, StoreAction, FileDialogAction, StyleAction, OtherAction>::type;

// Composite action types.
using ActionMoment = std::pair<Any, TimePoint>;
using StatefulActionMoment = std::pair<StatefulAction, TimePoint>;
using Gesture = std::vector<StatefulActionMoment>;
using Gestures = std::vector<Gesture>;

// Default-construct an action by its variant index (which is also its `ID`).
// Adapted from: https://stackoverflow.com/a/60567091/780425
template<ID I = 0> Any Create(ID index) {
    if constexpr (I >= std::variant_size_v<Any>) throw std::runtime_error{"Action index " + to_string(I + index) + " out of bounds"};
    else return index == 0 ? Any{std::in_place_index<I>} : Create<I + 1>(index - 1);
}

#include "../../Boost/mp11/mp_find.h"

// E.g. `ID action_id = id<action_type>`
// An action's ID is its index in the `Action::Any` variant.
// Down the road, this means `Action::Any` would need to be append-only (no order changes) for backwards compatibility.
// Not worried about that right now, since it should be easy enough to replace with some UUID system later.
// Index is simplest.
// Mp11 approach from: https://stackoverflow.com/a/66386518/780425
template<typename T> constexpr ID id = mp_find<Any, T>::value;

inline static const std::unordered_map<ID, string> ShortcutForId = {
    {id<Undo>, "cmd+z"},
    {id<Redo>, "shift+cmd+z"},
    {id<OpenEmptyProject>, "cmd+n"},
    {id<ShowOpenProjectDialog>, "cmd+o"},
    {id<OpenDefaultProject>, "shift+cmd+o"},
    {id<SaveCurrentProject>, "cmd+s"},
    {id<ShowSaveProjectDialog>, "shift+cmd+s"},
};

constexpr ID GetId(const Any &action) { return action.index(); }
constexpr ID GetId(const StatefulAction &action) { return action.index(); }

string GetName(const ProjectAction &action);
string GetName(const StatefulAction &action);
string GetShortcut(const Any &);
string GetMenuLabel(const Any &);
Gesture MergeGesture(const Gesture &);
} // namespace Action

/**
 This is the main action-queue method.
 Providing `flush = true` will run all enqueued actions (including this one) and finalize any open gesture.
 This is useful for running multiple actions in a single frame, without grouping them into a single gesture.
 Defined in `Project.cpp`.
*/
bool q(Action::Any &&a, bool flush = false);

// These are also defined in `Project.cpp`.
bool ActionAllowed(ID);
bool ActionAllowed(const Action::Any &);

namespace nlohmann {
DeclareJson(Action::StatefulAction);
} // namespace nlohmann
