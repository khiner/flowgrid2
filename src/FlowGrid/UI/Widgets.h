#pragma once

#include "nlohmann/json_fwd.hpp"

#include "NamesAndValues.h"
#include "Styling.h"

using namespace nlohmann;

struct ImVec2;

namespace FlowGrid {}
namespace fg = FlowGrid;

enum InteractionFlags_ {
    InteractionFlags_None = 0,
    InteractionFlags_Hovered = 1 << 0,
    InteractionFlags_Held = 1 << 1,
    InteractionFlags_Clicked = 1 << 2,
};
enum KnobFlags_ {
    KnobFlags_None = 0,
    KnobFlags_NoTitle = 1 << 0,
    KnobFlags_NoInput = 1 << 1,
    KnobFlags_ValueTooltip = 1 << 2,
    KnobFlags_DragHorizontal = 1 << 3,
};
enum KnobVariant_ {
    KnobVariant_Tick = 1 << 0,
    KnobVariant_Dot = 1 << 1,
    KnobVariant_Wiper = 1 << 2,
    KnobVariant_WiperOnly = 1 << 3,
    KnobVariant_WiperDot = 1 << 4,
    KnobVariant_Stepped = 1 << 5,
    KnobVariant_Space = 1 << 6,
};
enum ValueBarFlags_ {
    // todo flag for value text to follow the value like `ImGui::ProgressBar`
    ValueBarFlags_None = 0,
    ValueBarFlags_Vertical = 1 << 0,
    ValueBarFlags_ReadOnly = 1 << 1,
    ValueBarFlags_NoTitle = 1 << 2,
};
enum RadioButtonsFlags_ {
    RadioButtonsFlags_None = 0,
    RadioButtonsFlags_Vertical = 1 << 0,
    RadioButtonsFlags_NoTitle = 1 << 1,
};
enum JsonTreeNodeFlags_ {
    JsonTreeNodeFlags_None = 0,
    JsonTreeNodeFlags_Highlighted = 1 << 0,
    JsonTreeNodeFlags_Disabled = 1 << 1,
    JsonTreeNodeFlags_DefaultOpen = 1 << 2,
};

using InteractionFlags = int;
using KnobFlags = int;
using KnobVariant = int;
using ValueBarFlags = int;
using RadioButtonsFlags = int;
using JsonTreeNodeFlags = int;

namespace FlowGrid {
struct ColorSet {
    ColorSet(const U32 base, const U32 hovered, const U32 active) : base(base), hovered(hovered), active(active) {}
    ColorSet(const U32 color) : ColorSet(color, color, color) {}

    U32 base, hovered, active;
};

// Similar to `imgui_demo.cpp`'s `HelpMarker`.
void HelpMarker(const char *help);
// Basically `ImGui::InvisibleButton`, but supports hover/held testing.
InteractionFlags InvisibleButton(const ImVec2 &size_arg, const char *id);

bool Knob(const char *label, float *p_value, float v_min, float v_max, float speed = 0, const char *format = nullptr, HJustify h_justify = HJustify_Middle, KnobVariant variant = KnobVariant_Tick, KnobFlags flags = KnobFlags_None, int steps = 10);
bool KnobInt(const char *label, int *p_value, int v_min, int v_max, float speed = 0, const char *format = nullptr, HJustify h_justify = HJustify_Middle, KnobVariant variant = KnobVariant_Tick, KnobFlags flags = KnobFlags_None, int steps = 10);

// When `ReadOnly` is set, this is similar to `ImGui::ProgressBar`, but it has a horizontal/vertical switch,
// and the value text doesn't follow the value position (it stays in the middle).
// If `ReadOnly` is not set, this delegates to `SliderFloat`/`VSliderFloat`, but renders the value & label independently.
// Horizontal labels are placed to the right of the rect.
// Vertical labels are placed below the rect, respecting the passed in alignment.
// `size` is the rectangle size.
// Assumes the current cursor position is at the desired top-left of the rectangle.
// Assumes the current item width has been set to the desired rectangle width (not including label width).
bool ValueBar(const char *label, float *value, const float rect_height, const float min_value = 0, const float max_value = 1, const ValueBarFlags flags = ValueBarFlags_None, const HJustify h_justify = HJustify_Middle);

// When `ReadOnly` is set, this is similar to `ImGui::ProgressBar`, but it has a horizontal/vertical switch,
// and the value text doesn't follow the value position (it stays in the middle).
// If `ReadOnly` is not set, this delegates to `SliderFloat`/`VSliderFloat`, but renders the value & label independently.
// Horizontal labels are placed to the right of the rect.
// Vertical labels are placed below the rect, respecting the passed in alignment.
// `size` is the rectangle size.
// Assumes the current cursor position is either the desired top-left of the rectangle (or the beginning of the label for a vertical bar with a title).
// Assumes the current item width has been set to the desired rectangle width (not including label width).
bool RadioButtons(const char *label, float *value, const NamesAndValues &names_and_values, const RadioButtonsFlags flags = RadioButtonsFlags_None, const Justify justify = {HJustify_Middle, VJustify_Middle});
float CalcRadioChoiceWidth(const string &choice_name);

// If `label` is empty, `JsonTree` will simply show the provided json `value` (object/array/raw value), with no nesting.
// For a non-empty `label`:
//   * If the provided `value` is an array or object, it will show as a nested `JsonTreeNode` with `label` as its parent.
//   * If the provided `value` is a raw value (or null), it will show as as '{label}: {value}'.
void JsonTree(std::string_view label, const json &value, JsonTreeNodeFlags node_flags = JsonTreeNodeFlags_None, const char *id = nullptr);
bool JsonTreeNode(std::string_view label, JsonTreeNodeFlags flags = JsonTreeNodeFlags_None, const char *id = nullptr, const char *value = nullptr);
} // namespace FlowGrid
