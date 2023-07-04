#pragma once

#include "Core/Container/Vector.h"
#include "Core/Primitive/UInt.h"

struct ImVec4;

struct Colors : Vector<U32> {
    Colors(ComponentArgs &&, Count size, std::function<const char *(int)> get_name, const bool allow_auto = false);

    static U32 Float4ToU32(const ImVec4 &value);
    static ImVec4 U32ToFloat4(U32 value);
    constexpr static std::string U32ToHex(U32 value) noexcept { return std::format("#{:08X}", value); }

    // An arbitrary transparent color is used to mark colors as "auto".
    // Using a the unique bit pattern `010101` for the RGB components so as not to confuse it with black/white-transparent.
    // Similar to ImPlot's usage of [`IMPLOT_AUTO_COL = ImVec4(0,0,0,-1)`](https://github.com/epezent/implot/blob/master/implot.h#L67).
    static constexpr U32 AutoColor = 0X00010101;

    void Set(const std::vector<ImVec4> &) const;
    void Set(const std::vector<std::pair<int, ImVec4>> &) const;

    void RenderValueTree(bool annotate, bool auto_select) const override;

protected:
    void Render() const override;

private:
    std::function<const char *(int)> GetName;
    bool AllowAuto;
};