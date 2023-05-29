#pragma once

#include "Core/Stateful/Window.h"
#include "FileDialog/FileDialog.h"

struct Demo : TabsWindow {
    Demo(Stateful::Base *parent, string_view path_segment, string_view name_help);

    DefineUI(ImGuiDemo);
    DefineUI(ImPlotDemo);

    Prop(ImGuiDemo, ImGui);
    Prop(ImPlotDemo, ImPlot);
    Prop(FileDialog::Demo, FileDialog);
};