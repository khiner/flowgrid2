#pragma once

#include <string>

#include "imgui.h"

using std::string;

// Uses same argument ordering as CSS.
struct Padding {
    Padding(const float top, const float right, const float bottom, const float left) : Top(top), Right(right), Bottom(bottom), Left(left) {}
    Padding(const float top, const float x, const float bottom) : Padding(top, x, bottom, x) {}
    Padding(const float y, const float x) : Padding(y, x, y, x) {}
    Padding(const float all) : Padding(all, all, all, all) {}
    Padding() : Padding(0, 0, 0, 0) {}

    const float Top, Right, Bottom, Left;
};

enum HJustify_ { HJustify_Left, HJustify_Middle, HJustify_Right, };
enum VJustify_ { VJustify_Top, VJustify_Middle, VJustify_Bottom, };
using HJustify = int;
using VJustify = int;

struct Justify {
    HJustify h;
    VJustify v;
};

struct TextStyle {
    enum FontStyle { Normal, Bold, Italic, };

    const ImColor Color;
    const Justify Justify{HJustify_Middle, VJustify_Middle};
    const Padding Padding;
    const FontStyle FontStyle{Normal};
};

struct RectStyle {
    const ImColor FillColor, StrokeColor;
    const float StrokeWidth{0}, CornerRadius{0};
};

float CalcAlignedX(HJustify h_justify, float inner_w, float outer_w, bool is_label = false); // todo better name than `is_label`
float CalcAlignedY(VJustify v_justify, float inner_h, float outer_h);

ImVec2 TextSize(const string &text);

// There's `RenderTextEllipsis` in `imgui_internal`, but it's way too complex and scary.
string Ellipsify(const string &text, float max_width);