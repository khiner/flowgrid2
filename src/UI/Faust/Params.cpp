#include "FaustUI.h"

#include "imgui.h"
#include "../../State.h"

class FaustUI;

void on_ui_change(FaustUI *);

void save_box_svg(const string &path);
using namespace ImGui;
using ItemType = FaustUI::ItemType;
using
enum FaustUI::ItemType;

static constexpr ItemType GroupItems[]{
    ItemType_HGroup,
    ItemType_VGroup,
    ItemType_TGroup,
};
static constexpr ItemType ShortItems[]{
    ItemType_Button,
    ItemType_CheckButton,
    ItemType_HSlider,
    ItemType_NumEntry,
    ItemType_HBargraph,
};
static constexpr ItemType LabeledItems[]{
    ItemType_HSlider,
    ItemType_NumEntry,
    ItemType_HBargraph,
    ItemType_VBargraph,
};

FaustUI *interface;

bool header_titles = false; // todo style config
bool center_vertical = true; // todo style config

// Similar to `ImGui::ProgressBar`, but with horizontal/vertical switch.
// The value text doesn't follow the value like `ImGui::ProgressBar`.
// Here it's simply displayed in the middle of the bar.
// Horizontal labels are placed to the right of the rect.
// Vertical labels are placed below the rect.
// todo flag for value text to follow the value like `ImGui::ProgressBar`
void ProgressishBar(const float value, const char *label, const ImVec2 &size, const float min_value=0, const float max_value=1, const bool is_h=true) {
    const auto style = GetStyle();
    const auto &draw_list = GetWindowDrawList();
    const auto &cursor_pos = GetCursorScreenPos();
    const auto fraction = (value - min_value) / max_value;
    const float font_height = GetFontSize();
    const auto &rect_size = is_h ? ImVec2{CalcItemWidth(), font_height + style.FramePadding.y * 2} : ImVec2{GetFontSize() * 2, size.y - (font_height + style.FramePadding.y * 2)};

    draw_list->AddRectFilled(cursor_pos, cursor_pos + rect_size, GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    draw_list->AddRectFilled(
        cursor_pos + ImVec2{0, is_h ? 0 : fraction * rect_size.y},
        cursor_pos + ImVec2{is_h ? fraction * rect_size.x : rect_size.x, rect_size.y},
        GetColorU32(ImGuiCol_PlotHistogram),
        style.FrameRounding,
        is_h ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersBottom
    );
    const string value_text = is_h ? format("{:.2f}", value) : format("{:.1f}", value);
    const auto &text_offset = (rect_size - CalcTextSize(value_text.c_str())) / 2;
    draw_list->AddText(
        cursor_pos + (is_h ? text_offset : text_offset + ImVec2{0, 0}),
        GetColorU32(ImGuiCol_Text), value_text.c_str(), FindRenderedTextEnd(value_text.c_str())
    );
    if (label) {
        draw_list->AddText(
            cursor_pos + ImVec2{is_h ? rect_size.x + style.ItemInnerSpacing.x : (rect_size.x - CalcTextSize(label).x) / 2,
                                is_h ? style.FramePadding.y : rect_size.y + style.FramePadding.y},
            GetColorU32(ImGuiCol_Text), label, FindRenderedTextEnd(label)
        );
    }
}

void DrawUiItem(const FaustUI::Item &item, const ImVec2 &size, const ItemType parent_type = ItemType_None) {
    const static auto group_bg_color = GetColorU32(ImGuiCol_FrameBg, 0.2); // todo new FG style color

    const auto type = item.type;
    const char *label = item.label.c_str();
    const bool show_label = parent_type != ItemType_TGroup && !(parent_type == ItemType_HGroup && header_titles);
    const auto &inner_items = item.items;
    const bool is_group = std::find(std::begin(GroupItems), std::end(GroupItems), type) != std::end(GroupItems);
    const float font_height = GetFontSize();
    const auto style = GetStyle();

    if (is_group) {
        if (show_label) Text("%s", label);
        const float group_height = size.y - (show_label ? font_height + style.ItemSpacing.y : 0);
        if (type == ItemType_HGroup) {
            if (BeginTable(label, int(inner_items.size()), ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable)) {
                const ImVec2 item_size = {size.x / float(inner_items.size()), group_height};
                for (const auto &inner_item: inner_items) TableSetupColumn(inner_item.label.c_str());
                if (header_titles) TableHeadersRow();
                for (const auto &inner_item: inner_items) {
                    TableNextColumn();
                    TableSetBgColor(ImGuiTableBgTarget_RowBg0, group_bg_color);
                    DrawUiItem(inner_item, item_size, type);
                }
                EndTable();
            }
        } else if (type == ItemType_VGroup) {
            if (BeginTable(label, 1, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable)) {
                const ImVec2 item_size = {size.x, group_height / float(inner_items.size())};
                for (const auto &inner_item: inner_items) {
                    TableNextRow(ImGuiTableRowFlags_None, item_size.y);
                    TableSetColumnIndex(0);
                    TableSetBgColor(ImGuiTableBgTarget_RowBg0, group_bg_color);
                    DrawUiItem(inner_item, item_size, type);
                }
                EndTable();
            }
        } else if (type == ItemType_TGroup) {
            BeginTabBar(label);
            for (const auto &inner_item: inner_items) {
                if (BeginTabItem(inner_item.label.c_str())) {
                    const float tab_height = font_height + style.FramePadding.y;
                    DrawUiItem(inner_item, {size.x, group_height - tab_height}, type);
                    EndTabItem();
                }
            }
            EndTabBar();
        }
    } else {
        const bool labeled = show_label && std::find(std::begin(LabeledItems), std::end(LabeledItems), type) != std::end(LabeledItems);
        SetNextItemWidth(GetContentRegionAvail().x - (labeled ? (CalcTextSize(label).x + GetFontSize()) : 0));
        const float before_y = GetCursorPosY();
        const bool is_short = std::find(std::begin(ShortItems), std::end(ShortItems), type) != std::end(ShortItems);
        const bool should_center_vertical = is_short && center_vertical;
        if (should_center_vertical) SetCursorPosY(before_y + (size.y - (font_height + style.FramePadding.y)) / 2);

        const char *title = show_label ? label : "";
        if (type == ItemType_Button) {
            *item.zone = Real(Button(label));
        } else if (type == ItemType_CheckButton) {
            auto checked = bool(*item.zone);
            Checkbox(title, &checked);
            *item.zone = Real(checked);
        } else if (type == ItemType_HSlider) {
            auto value = float(*item.zone);
            SliderFloat(title, &value, float(item.min), float(item.max), "%.2f");
            *item.zone = Real(value);
        } else if (type == ItemType_VSlider) {
            auto value = float(*item.zone);
            VSliderFloat(title, {GetFontSize() * 2, size.y}, &value, float(item.min), float(item.max), "%.1f");
            *item.zone = Real(value);
        } else if (type == ItemType_NumEntry) {
            auto value = float(*item.zone);
            InputFloat(title, &value, float(item.step));
            *item.zone = Real(value);
        } else if (type == ItemType_HBargraph || type == ItemType_VBargraph) {
            const auto value = float(*item.zone);
            ProgressishBar(value, title, size, float(item.min), float(item.max), type == ItemType_HBargraph);
        }
        if (should_center_vertical) SetCursorPosY(before_y);
    }
}

void Audio::FaustState::FaustParams::draw() const {
    if (!interface) {
        // todo don't show empty menu bar in this case
        Text("Enter a valid Faust program into the 'Faust editor' window to view its params."); // todo link to window?
        return;
    }

    const auto &size = GetContentRegionAvail();
    for (const auto &item: interface->ui) DrawUiItem(item, {size.x, size.y / float(interface->ui.size())});

//    if (hovered_node) {
//        const string label = get_ui_label(hovered_node->tree);
//        if (!label.empty()) {
//            const auto *widget = interface->get_widget(label);
//            if (widget) {
//                cout << "Found widget: " << label << '\n';
//            }
//        }
//    }
}

void on_ui_change(FaustUI *ui) {
    interface = ui;
}