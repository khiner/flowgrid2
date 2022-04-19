#pragma once

#include "nlohmann/json.hpp"
#include "imgui.h"

// TODO Different modes, with different states (e.g. AudioTrackMode),
//  which control the default settings for
//    * Layout
//    * Node organization, move-rules
//    * Automatic connections-rules

struct Dimensions {
    ImVec2 position;
    ImVec2 size;
};

struct Window {
    std::string name;
    bool visible{true};
};

// `WindowsBase` contains only data members.
// Derived fields and convenience methods are in `Windows`
struct WindowsBase {
    struct ImGuiWindows {
        Window demo{"Dear ImGui Demo"};
        Window metrics{"Dear ImGui Metrics/Debugger"};
    };
    struct FaustWindows {
        Window editor{"Faust Editor"};
        Window log{"Faust Log"};
    };

    Window controls{"Controls"};
    Window style_editor{"Style editor"};
    ImGuiWindows imgui{};
    FaustWindows faust;
};

struct Windows : public WindowsBase {
    Windows() = default;
    // Don't copy/assign references!
    Windows(const Windows &other) : WindowsBase(other) {}
    Windows &operator=(const Windows &other) {
        WindowsBase::operator=(other);
        return *this;
    }

    Window &named(const std::string &name) {
        for (auto &window: all) {
            if (name == window.get().name) return window;
        }
        throw std::invalid_argument(name);
    }

    const Window &named(const std::string &name) const {
        for (auto &window: all_const) {
            if (name == window.get().name) return window;
        }
        throw std::invalid_argument(name);
    }

    std::vector<std::reference_wrapper<Window>> all{controls, style_editor, imgui.demo, imgui.metrics, faust.editor, faust.log};
    std::vector<std::reference_wrapper<const Window>> all_const{controls, style_editor, imgui.demo, imgui.metrics, faust.editor, faust.log};
};

struct UI {
    bool running = true;
    Windows windows;
    ImGuiStyle style;
};

enum AudioBackend {
    none, dummy, alsa, pulseaudio, jack, coreaudio, wasapi
};

struct Editor {
    std::string file_name;
};

struct Faust {
    std::string code{"import(\"stdfaust.lib\");\n\n"
                     "pitchshifter = vgroup(\"Pitch Shifter\", ef.transpose(\n"
                     "    hslider(\"window (samples)\", 1000, 50, 10000, 1),\n"
                     "    hslider(\"xfade (samples)\", 10, 1, 10000, 1),\n"
                     "    hslider(\"shift (semitones) \", 0, -24, +24, 0.1)\n"
                     "  )\n"
                     ");\n"
                     "\n"
                     "process = no.noise : pitchshifter;\n"};
//    std::string code{"import(\"stdfaust.lib\");\n\nprocess = ba.pulsen(1, 10000) : pm.djembe(60, 0.3, 0.4, 1);"}; // TODO pm not working
// same with process = dm.freeverb_demo; and all `dm`
    std::string error{};
    Editor editor{"default.dsp"};
};

struct Audio {
    AudioBackend backend = none;
    Faust faust;
    char *in_device_id = nullptr;
    char *out_device_id = nullptr;
    bool running = true;
    bool muted = true;
    bool out_raw = false;
    int sample_rate = 48000;
    double latency = 0.0;

};

struct ActionConsumer {
    bool running = true;
};

struct State {
    UI ui;
    Audio audio;
    ActionConsumer action_consumer;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Editor, file_name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Faust, code, editor, error)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Audio, running, muted, backend, latency, sample_rate, out_raw, faust)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ImVec2, x, y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ImVec4, w, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Dimensions, position, size)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Window, name, visible)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WindowsBase::ImGuiWindows, demo, metrics)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WindowsBase::FaustWindows, editor, log)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WindowsBase, controls, style_editor, imgui, faust)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ImGuiStyle, Alpha, DisabledAlpha, WindowPadding, WindowRounding, WindowBorderSize, WindowMinSize, WindowTitleAlign, WindowMenuButtonPosition, ChildRounding, ChildBorderSize,
    PopupRounding, PopupBorderSize, FramePadding, FrameRounding, FrameBorderSize, ItemSpacing, ItemInnerSpacing, CellPadding, TouchExtraPadding, IndentSpacing, ColumnsMinSpacing, ScrollbarSize, ScrollbarRounding,
    GrabMinSize, GrabRounding, LogSliderDeadzone, TabRounding, TabBorderSize, TabMinWidthForCloseButton, ColorButtonPosition, ButtonTextAlign, SelectableTextAlign, DisplayWindowPadding, DisplaySafeAreaPadding,
    MouseCursorScale, AntiAliasedLines, AntiAliasedLinesUseTex, AntiAliasedFill, CurveTessellationTol, CircleTessellationMaxError, Colors)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UI, running, windows, style)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ActionConsumer, running)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(State, ui, audio, action_consumer);
