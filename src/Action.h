#pragma once

#include <string>
#include <variant>

#include "State.h"
#include "Helper/String.h"

/**
 An `Action` is an immutable representation of a user interaction event.
 Each action stores all information needed for `update` to apply it to the global `State` instance.

 Conventions:
 * Static members
   - _Not relevant as there aren't any static members atm, but note to future-self._
   - Any static members should be prefixed with an underscore to avoid name collisions with data members.
   - All such static members are declared _after_ data members, to allow for default construction using only non-static members.
   - Note that adding static members does not increase the size of the parent `Action` variant.
     (You can verify this by looking at the 'Action variant size' in the FlowGrid metrics window.)
 * Large action structs
   - Use JSON types for actions that hold very large structured data.
     An `Action` is a `std::variant`, which can hold any type, and thus must be large enough to hold its largest type.
*/

// Utility to make a variant visitor out of lambdas, using the "overloaded pattern" described
// [here](https://en.cppreference.com/w/cpp/utility/variant/visit).
template<class... Ts>
struct visitor : Ts ... {
    using Ts::operator()...;
};

template<class... Ts> visitor(Ts...)->visitor<Ts...>;


namespace Actions {

struct undo {};
struct redo {};
struct set_diff_index { int diff_index; };

struct open_project { string path; };
struct open_empty_project {};
struct open_default_project {};
struct show_open_project_dialog {};

struct open_file_dialog { File::DialogData dialog; }; // todo store as json and check effect on action size
struct close_file_dialog {};

struct save_project { string path; };
struct save_current_project {};
struct save_default_project {};
struct show_save_project_dialog {};

struct close_application {};

struct set_value { JsonPath path; json value; };
struct set_values { std::map<JsonPath, json> values; };
//struct patch_value { JsonPatch patch; };
struct toggle_value { JsonPath path; };

struct set_imgui_color_style { int id; };
struct set_implot_color_style { int id; };
struct set_flowgrid_color_style { int id; };
struct set_flowgrid_diagram_color_style { int id; };
struct set_flowgrid_diagram_layout_style { int id; };

struct show_open_faust_file_dialog {};
struct show_save_faust_file_dialog {};
struct show_save_faust_svg_file_dialog {};
struct save_faust_file { string path; };
struct open_faust_file { string path; };
struct save_faust_svg_file { string path; };

EmptyJsonType(undo)
EmptyJsonType(redo)
EmptyJsonType(open_empty_project)
EmptyJsonType(open_default_project)
EmptyJsonType(show_open_project_dialog)
EmptyJsonType(close_file_dialog)
EmptyJsonType(save_current_project)
EmptyJsonType(save_default_project)
EmptyJsonType(show_save_project_dialog)
EmptyJsonType(close_application)
EmptyJsonType(show_open_faust_file_dialog)
EmptyJsonType(show_save_faust_file_dialog)
EmptyJsonType(show_save_faust_svg_file_dialog)

JsonType(set_diff_index, diff_index)
JsonType(open_project, path)
JsonType(open_file_dialog, dialog)
JsonType(save_project, path)
JsonType(set_value, path, value)
JsonType(set_values, values)
//JsonType(patch_value, patch)
JsonType(toggle_value, path)
JsonType(set_imgui_color_style, id)
JsonType(set_implot_color_style, id)
JsonType(set_flowgrid_color_style, id)
JsonType(set_flowgrid_diagram_color_style, id)
JsonType(set_flowgrid_diagram_layout_style, id)
JsonType(save_faust_file, path)
JsonType(open_faust_file, path)
JsonType(save_faust_svg_file, path)

}

using namespace Actions;

namespace action {

using ID = size_t;

using Action = std::variant<
    undo, redo, set_diff_index,
    open_project, open_empty_project, open_default_project, show_open_project_dialog,
    save_project, save_default_project, save_current_project, show_save_project_dialog,
    open_file_dialog, close_file_dialog,
    close_application,

    set_value, set_values, toggle_value,

    set_imgui_color_style, set_implot_color_style, set_flowgrid_color_style, set_flowgrid_diagram_color_style,
    set_flowgrid_diagram_layout_style,

    show_open_faust_file_dialog, show_save_faust_file_dialog, show_save_faust_svg_file_dialog,
    open_faust_file, save_faust_file, save_faust_svg_file
>;

using Gesture = std::vector<Action>;
using Gestures = std::vector<Gesture>;

// Default-construct an action by its variant index (which is also its `ID`).
// From https://stackoverflow.com/a/60567091/780425
template<ID I = 0>
Action create(ID index) {
    if constexpr (I >= std::variant_size_v<Action>) throw std::runtime_error{"Action index " + std::to_string(I + index) + " out of bounds"};
    else return index == 0 ? Action{std::in_place_index<I>} : create<I + 1>(index - 1);
}

// Get the index for action variant type.
// From https://stackoverflow.com/a/66386518/780425

#include "Boost/mp11/mp_find.h"

// E.g. `const ActionId action_id = id<action>`
// An action's ID is its index in the `Action` variant.
// Down the road, this means `Action` would need to be append-only (no order changes) for backwards compatibility.
// Not worried about that right now, since that should be an easy piece to replace with some uuid system later.
// Index is simplest.
template<typename T>
constexpr size_t id = mp_find<Action, T>::value;

#define ActionName(action_var_name) snake_case_to_sentence_case(#action_var_name)

// todo find a performant way to not compile if not exhaustive.
//  Could use a visitor on the action...
static const std::map<ID, string> name_for_id{
    {id<undo>, ActionName(undo)},
    {id<redo>, ActionName(redo)},
    {id<set_diff_index>, ActionName(set_diff_index)},

    {id<open_project>, ActionName(open_project)},
    {id<open_empty_project>, ActionName(open_empty_project)},
    {id<open_default_project>, ActionName(open_default_project)},
    {id<show_open_project_dialog>, ActionName(show_open_project_dialog)},

    {id<open_file_dialog>, ActionName(open_file_dialog)},
    {id<close_file_dialog>, ActionName(close_file_dialog)},

    {id<save_project>, ActionName(save_project)},
    {id<save_default_project>, ActionName(save_default_project)},
    {id<save_current_project>, ActionName(save_current_project)},
    {id<show_save_project_dialog>, ActionName(show_save_project_dialog)},

    {id<close_application>, ActionName(close_application)},

    {id<set_value>, ActionName(set_value)},
    {id<set_values>, ActionName(set_values)},
    {id<toggle_value>, ActionName(toggle_value)},
//    {id<patch_value>,              ActionName(patch_value)},

    {id<set_imgui_color_style>, "Set ImGui color style"},
    {id<set_implot_color_style>, "Set ImPlot color style"},
    {id<set_flowgrid_color_style>, "Set FlowGrid color style"},
    {id<set_flowgrid_diagram_color_style>, "Set FlowGrid diagram color style"},
    {id<set_flowgrid_diagram_color_style>, "Set FlowGrid diagram layout style"},

    {id<show_open_faust_file_dialog>, "Show open Faust file dialog"},
    {id<show_save_faust_file_dialog>, "Show save Faust file dialog"},
    {id<show_save_faust_svg_file_dialog>, "Show save Faust SVG file dialog"},
    {id<open_faust_file>, "Open Faust file"},
    {id<save_faust_file>, "Save Faust file"},
    {id<save_faust_svg_file>, "Save Faust SVG file"},
};

// An action's menu label is its name, except for a few exceptions.
static const std::map<ID, string> menu_label_for_id{
    {id<show_open_project_dialog>, "Open project"},
    {id<open_empty_project>, "New project"},
    {id<save_current_project>, "Save project"},
    {id<show_save_project_dialog>, "Save project as..."},
    {id<show_open_faust_file_dialog>, "Open DSP file"},
    {id<show_save_faust_file_dialog>, "Save DSP as..."},
    {id<show_save_faust_svg_file_dialog>, "Export SVG"},
};

const std::map<ID, string> shortcut_for_id = {
    {id<undo>, "cmd+z"},
    {id<redo>, "shift+cmd+z"},
    {id<open_empty_project>, "cmd+n"},
    {id<show_open_project_dialog>, "cmd+o"},
    {id<save_current_project>, "cmd+s"},
    {id<open_default_project>, "shift+cmd+o"},
    {id<save_default_project>, "shift+cmd+s"},
};

static constexpr ID get_id(const Action &action) { return action.index(); }
static string get_name(const Action &action) { return name_for_id.at(get_id(action)); }

static const char *get_menu_label(ID action_id) {
    if (menu_label_for_id.contains(action_id)) return menu_label_for_id.at(action_id).c_str();
    return name_for_id.at(action_id).c_str();
}

Gesture merge_gesture(const Gesture &);

}

using ActionID = action::ID;
using action::Gesture;
using action::Gestures;
