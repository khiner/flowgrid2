#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "Primitive.h"

using std::string_view;

struct StateMember {
    inline static std::unordered_map<ID, StateMember *> WithId; // Access any state member by its ID.

    StateMember(StateMember *parent = nullptr, string_view path_segment = "", string_view name_help = "");
    StateMember(StateMember *parent, string_view path_segment, std::pair<string_view, string_view> name_help);

    virtual ~StateMember();
    const StateMember *Child(Count i) const { return Children[i]; }
    inline Count ChildCount() const { return Children.size(); }

    const StateMember *Parent;
    std::vector<StateMember *> Children{};
    const string PathSegment;
    const StatePath Path;
    const string Name, Help, ImGuiLabel;
    const ID Id;

protected:
    // Helper to display a (?) mark which shows a tooltip when hovered. Similar to the one in `imgui_demo.cpp`.
    void HelpMarker(bool after = true) const;
};

/**
Convenience macros for compactly defining `StateMember` types and their properties.

todo These will very likely be defined in a separate language once the API settles down.
  If we could hot-reload and only recompile the needed bits without restarting the app, it would accelerate development A TON.
  (Long compile times, although they aren't nearly as bad as [my previous attempt](https://github.com/khiner/flowgrid_old),
  are still the biggest drain on this project.)

Macros:

All macros end in semicolons already, so there's no strict need to suffix their usage with a semicolon.
However, all macro calls in FlowGrid are treated like regular function calls, appending a semicolon.
todo If we stick with this, add a `static_assert(true, "")` to the end of all macro definitions.
https://stackoverflow.com/questions/35530850/how-to-require-a-semicolon-after-a-macro
todo Try out replacing semicolon separators by e.g. commas.

* Properties
  - `Prop` adds a new property `this`.
  Define a property of this type during construction at class scope to add a child member variable.
    - Assumes it's being called within a `PropType` class scope during construction.
    `PropType`, with variable name `PropName`, constructing the state member with `this` as a parent, and store path-segment `"{PropName}"`.
    (string with value the same as the variable name).
    - `Prop_` is the same as `Prop`, but supports overriding the displayed name & adding help text in the third arg.
    - Arguments
      1) `PropType`: Any type deriving from `StateMember`.
      2) `PropName` (use PascalCase) is used for:
        - The ID of the property, relative to its parent (`this` during the macro's execution).
        - The name of the instance variable added to `this` (again, defined like any other instance variable in a `StateMember`).
        - The default label displayed in the UI is a 'Sentense cased' label derived from the prop's 'PascalCase' `PropName` property-id/path-segment (the second arg).
      3) `NameHelp`
        - A string with format "Label string?Help string".
        - Optional, available with a `_` suffix.
        - Overrides the label displayed in the UI for this property.
        - Anything after a '?' is interpretted as a help string
          - E.g. `Prop(Bool, TestAThing, "Test-a-thing?A state member for testing things")` overrides the default "Test a thing" label with a hyphenation.
          - Or, provide nothing before the '?' to add a help string without overriding the default `PropName`-derived label.
            - E.g. "?A state member for testing things."
* Member types
  - `Member` defines a plain old state type.
  - `UIMember` defines a drawable state type.
    - `UIMember_` is the same as `UIMember`, but adds a custom constructor implementation (with the same arguments).
  - `WindowMember` defines a drawable state type whose contents are rendered to a window.
    - `WindowMember_` is the same as `WindowMember`, but allows passing either:
      - a `bool` to override the default `true` visibility, or
      - a `Menu` to define the window's menu.
    - `TabsWindow` is a `WindowMember` that renders all its props as tabs. (except the `Visible` boolean member coming from `WindowMember`).
  - todo Refactor docking behavior out of `WindowMember` into a new `DockMember` type.

**/

#define Prop(PropType, PropName, ...) PropType PropName{this, (#PropName), "", __VA_ARGS__};
#define Prop_(PropType, PropName, NameHelp, ...) PropType PropName{this, (#PropName), (NameHelp), __VA_ARGS__};

#define Member(MemberName, ...)         \
    struct MemberName : StateMember {   \
        using StateMember::StateMember; \
        __VA_ARGS__;                    \
    };

struct Drawable {
    virtual void Draw() const; // Wraps around the internal `Render` function.

protected:
    virtual void Render() const = 0;
};

struct UIStateMember : StateMember, Drawable {
    using StateMember::StateMember;
};

#define UIMember(MemberName, ...)           \
    struct MemberName : UIStateMember {     \
        using UIStateMember::UIStateMember; \
        __VA_ARGS__;                        \
                                            \
    protected:                              \
        void Render() const override;       \
    };

#define UIMember_(MemberName, ...)                                                             \
    struct MemberName : UIStateMember {                                                        \
        MemberName(StateMember *parent, string_view path_segment, string_view name_help = ""); \
        __VA_ARGS__;                                                                           \
                                                                                               \
    protected:                                                                                 \
        void Render() const override;                                                          \
    };

#define WindowMember(MemberName, ...) \
    struct MemberName : Window {      \
        using Window::Window;         \
        __VA_ARGS__;                  \
                                      \
    protected:                        \
        void Render() const override; \
    };

#define WindowMember_(MemberName, VisibleOrMenu, ...)                                         \
    struct MemberName : Window {                                                              \
        MemberName(StateMember *parent, string_view path_segment, string_view name_help = "") \
            : Window(parent, path_segment, name_help, (VisibleOrMenu)) {}                     \
        __VA_ARGS__;                                                                          \
                                                                                              \
    protected:                                                                                \
        void Render() const override;                                                         \
    };