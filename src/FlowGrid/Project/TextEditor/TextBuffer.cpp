#include "TextBuffer.h"

#include <array>
#include <ranges>

#include "imgui_internal.h"
#include "immer/flex_vector_transient.hpp"
#include "immer/vector_transient.hpp"

#include "Application/ApplicationPreferences.h"
#include "Core/Windows.h"
#include "Helper/File.h"
#include "Helper/String.h"
#include "Helper/Time.h"
#include "Project/FileDialog/FileDialog.h"
#include "UI/Fonts.h"

#include "LanguageID.h"
#include "SyntaxTree.h"
#include "TextBufferPaletteId.h"

#include "Core/Store/Store.h"

namespace fs = std::filesystem;

using Buffer = TextBufferData;
using Cursor = LineCharRange;
using Line = TextBufferLine;
using Lines = TextBufferLines;
using Coords = TextBufferCoords;
using TransientLine = immer::flex_vector_transient<char>;
using TransientLines = immer::flex_vector_transient<Line>;

using std::views::filter, std::views::transform, std::views::join, std::ranges::any_of, std::ranges::subrange, std::ranges::to;

TextBufferStyle GTextBufferStyle{};

enum class PaletteIndex {
    TextDefault,
    Background,
    Cursor,
    Selection,
    Error,
    ControlCharacter,
    Breakpoint,
    LineNumber,
    CurrentLineFill,
    CurrentLineFillInactive,
    CurrentLineEdge,
    Max
};

const char *TSReadText(void *payload, u32 byte_index, TSPoint position, u32 *bytes_read);

struct TextBufferImpl {
    TextBufferImpl(TextBuffer *buffer, ID id) : Id(id), Syntax(std::make_unique<SyntaxTree>(TSInput{buffer, TSReadText, TSInputEncodingUTF8})) {}

    ~TextBufferImpl() = default;

    using PaletteT = std::array<u32, u32(PaletteIndex::Max)>;
    inline static const TextBufferPaletteId DefaultPaletteId{TextBufferPaletteId::Dark};

    inline static const PaletteT DarkPalette = {{
        0xffe4dfdc, // Default
        0xff342c28, // Background
        0xffe0e0e0, // Cursor
        0x80a06020, // Selection
        0x800020ff, // Error
        0x15ffffff, // ControlCharacter
        0x40f08000, // Breakpoint
        0xff94837a, // Line number
        0x40000000, // Current line fill
        0x40808080, // Current line fill (inactive)
        0x40a0a0a0, // Current line edge
    }};

    inline static const PaletteT MarianaPalette = {{
        0xffffffff, // Default
        0xff413830, // Background
        0xffe0e0e0, // Cursor
        0x80655a4e, // Selection
        0x80665fec, // Error
        0x30ffffff, // ControlCharacter
        0x40f08000, // Breakpoint
        0xb0ffffff, // Line number
        0x80655a4e, // Current line fill
        0x30655a4e, // Current line fill (inactive)
        0xb0655a4e, // Current line edge
    }};

    inline static const PaletteT LightPalette = {{
        0xff404040, // Default
        0xffffffff, // Background
        0xff000000, // Cursor
        0x40600000, // Selection
        0xa00010ff, // Error
        0x90909090, // ControlCharacter
        0x80f08000, // Breakpoint
        0xff505000, // Line number
        0x40000000, // Current line fill
        0x40808080, // Current line fill (inactive)
        0x40000000, // Current line edge
    }};

    inline static const PaletteT RetroBluePalette = {{
        0xff00ffff, // Default
        0xff800000, // Background
        0xff0080ff, // Cursor
        0x80ffff00, // Selection
        0xa00000ff, // Error
        0x80ff8000, // Breakpoint
        0xff808000, // Line number
        0x40000000, // Current line fill
        0x40808080, // Current line fill (inactive)
        0x40000000, // Current line edge
    }};

    // Returns the range of all edited cursor starts/ends since cursor edits were last cleared.
    // Used for updating the scroll range.
    std::optional<Cursor> GetEditedCursor(TextBufferData b) const {
        if (StartEdited.empty() && EndEdited.empty()) return {};

        Cursor edited;
        for (u32 i = 0; i < b.Cursors.size(); ++i) {
            if (StartEdited.contains(i) || EndEdited.contains(i)) {
                edited = b.Cursors[i];
                break; // todo create a sensible cursor representing the combined range when multiple cursors are edited.
            }
        }
        return edited;
    }

    // Cleared every frame. Used to keep recently edited cursors visible.
    std::unordered_set<u32> StartEdited{}, EndEdited{};

    std::string GetSyntaxTreeSExp() const { return Syntax->GetSExp(); }

    std::string_view GetLanguageName() const { return Languages.Get(LanguageId).Name; }
    u32 GetColor(PaletteIndex index) const { return GetPalette()[u32(index)]; }

    const PaletteT &GetPalette() const {
        switch (PaletteId) {
            case TextBufferPaletteId::Dark: return DarkPalette;
            case TextBufferPaletteId::Light: return LightPalette;
            case TextBufferPaletteId::Mariana: return MarianaPalette;
            case TextBufferPaletteId::RetroBlue: return RetroBluePalette;
        }
    }

    void SetFilePath(const fs::path &file_path) {
        const std::string extension = file_path.extension();
        SetLanguage(!extension.empty() && Languages.ByFileExtension.contains(extension) ? Languages.ByFileExtension.at(extension) : LanguageID::None);
    }

    void SetPalette(TextBufferPaletteId palette_id) { PaletteId = palette_id; }

    void SetLanguage(LanguageID language_id) {
        if (LanguageId == language_id) return;

        LanguageId = language_id;
        Syntax->SetLanguage(language_id);
        // Syntax->ApplyEdits(B.Edits);
        // B.Edits = {};
    }

    void ToggleOverwrite() { Overwrite ^= true; } // todo use Bool prop

    // todo store clipboard text manually in a `Lines` to avoid string conversion
    static Lines GetClipboardText() {
        TransientLines text{};
        const char *ptr = ImGui::GetClipboardText();
        while (*ptr != '\0') {
            u32 str_length = 0;
            while (ptr[str_length] != '\n' && ptr[str_length] != '\0') ++str_length;
            text.push_back({ptr, ptr + str_length});
            // Special case: Last char is a newline.
            if (*(ptr + str_length) == '\n' && *(ptr + str_length + 1) == '\0') text.push_back({});
            ptr += str_length + 1;
        }
        return text.persistent();
    }

    std::optional<TextBuffer::ActionType> Render(TextBufferData, bool is_focused);

    bool ReadOnly{false};
    bool Overwrite{false};
    bool AutoIndent{true};
    bool ShowWhitespaces{true};
    bool ShowLineNumbers{true};
    bool ShowStyleTransitionPoints{false}, ShowChangedCaptureRanges{false};
    bool ShortTabs{true};
    float LineSpacing{1};

    // private:

    Coords ScreenPosToCoords(TextBufferData b, const ImVec2 &screen_pos, ImVec2 char_advance, float text_start_x, bool *is_over_li = nullptr) const {
        static constexpr float PosToCoordsColumnOffset = 0.33;

        const auto local = screen_pos + ImVec2{3, 0} - ImGui::GetCursorScreenPos();
        if (is_over_li != nullptr) *is_over_li = local.x < text_start_x;

        Coords coords{
            std::min(u32(std::max(0.f, floor(local.y / char_advance.y))), u32(b.Text.size()) - 1),
            u32(std::max(0.f, floor((local.x - text_start_x + PosToCoordsColumnOffset * char_advance.x) / char_advance.x)))
        };
        // Check if the coord is in the middle of a tab character.
        const auto &line = b.Text[std::min(coords.L, u32(b.Text.size()) - 1)];
        const u32 ci = b.GetCharIndex(line, coords.C);
        if (ci < line.size() && line[ci] == '\t') coords.C = b.GetColumn(line, ci);

        return {coords.L, b.GetLineMaxColumn(line, coords.C)};
    }

    std::optional<TextBuffer::ActionType> HandleMouseInputs(TextBufferData b, ImVec2 char_advance, float text_start_x);

    void CreateHoveredNode(u32 byte_index) {
        DestroyHoveredNode();
        HoveredNode = std::make_unique<SyntaxNodeAncestry>(Syntax->GetNodeAncestryAtByte(byte_index));
        for (const auto &node : HoveredNode->Ancestry) {
            std::string name = !node.FieldName.empty() ? std::format("{}: {}", node.FieldName, node.Type) : node.Type;
            HelpInfo::ById.emplace(node.Id, HelpInfo{.Name = std::move(name), .Help = ""});
        }
    }

    void DestroyHoveredNode() {
        if (HoveredNode) {
            for (const auto &node : HoveredNode->Ancestry) HelpInfo::ById.erase(node.Id);
            HoveredNode.reset();
        }
    }

    ID Id;

    TextBufferPaletteId PaletteId{DefaultPaletteId};
    LanguageID LanguageId{LanguageID::None};

    ImVec2 ContentDims{0, 0}; // Pixel width/height of current content area.
    Coords ContentCoordDims{0, 0}; // Coords width/height of current content area.
    ImVec2 CurrentSpaceDims{20, 20}; // Pixel width/height given to `ImGui::Dummy`.
    ImVec2 LastClickPos{-1, -1};
    float LastClickTime{-1}; // ImGui time.
    std::unique_ptr<SyntaxNodeAncestry> HoveredNode{};
    std::unique_ptr<SyntaxTree> Syntax;
};

const char *TSReadText(void *payload, u32 byte_index, TSPoint position, u32 *bytes_read) {
    (void)byte_index; // Unused.
    static const char newline = '\n';

    const auto *buffer = static_cast<TextBuffer *>(payload);
    const auto buffer_data = buffer->GetBuffer();
    if (position.row >= buffer_data.LineCount()) {
        *bytes_read = 0;
        return nullptr;
    }
    const auto &line = buffer_data.GetLine(position.row);
    if (position.column > line.size()) { // Sanity check - shouldn't happen.
        *bytes_read = 0;
        return nullptr;
    }
    if (position.column == line.size()) {
        *bytes_read = 1;
        return &newline;
    }

    // Read until the end of the line.
    *bytes_read = line.size() - position.column;
    return &line.front() + position.column;
}

TextBuffer::TextBuffer(ArgsT &&args, const ::FileDialog &file_dialog, const fs::path &file_path)
    : ActionableComponent(std::move(args)), FileDialog(file_dialog), _LastOpenedFilePath(file_path),
      Impl(std::make_unique<TextBufferImpl>(this, Id)) {
    Impl->SetFilePath(file_path);
    FieldIds.insert(Id); // Acts as a `TextBufferData` field.
    // if (Exists()) Refresh();
    Commit(TextBufferData{}.SetText(std::string(FileIO::read(file_path))));
}

TextBuffer::~TextBuffer() {
    Erase();
    FieldIds.erase(Id);
}

bool TextBuffer::CanApply(const ActionType &action) const {
    using namespace Action::TextBuffer;

    return std::visit(
        Match{
            [](const ShowOpenDialog &) { return true; },
            [](const ShowSaveDialog &) { return true; },
            [](const Open &) { return true; },
            [](const Save &) { return true; },

            [](const SetCursor &) { return true; },
            [](const SetCursorRange &) { return true; },
            [](const MoveCursorsLines &) { return true; },
            [](const PageCursorsLines &) { return true; },
            [](const MoveCursorsChar &) { return true; },
            [](const MoveCursorsTop &) { return true; },
            [](const MoveCursorsBottom &) { return true; },
            [](const MoveCursorsStartLine &) { return true; },
            [](const MoveCursorsEndLine &) { return true; },
            [](const SelectAll &) { return true; },
            [](const SelectNextOccurrence &) { return true; },

            [](const Set &) { return true; },
            [](const ToggleOverwrite &) { return true; },

            [this](const Copy &) { return GetBuffer().AnyCursorsRanged(); },
            [this](const Cut &) { return !Impl->ReadOnly && GetBuffer().AnyCursorsRanged(); },
            [this](const Paste &) { return !Impl->ReadOnly && ImGui::GetClipboardText() != nullptr; },
            [this](const Delete &) { return !Impl->ReadOnly; },
            [this](const Backspace &) { return !Impl->ReadOnly; },
            [this](const DeleteCurrentLines &) { return !Impl->ReadOnly; },
            [this](const ChangeCurrentLinesIndentation &) { return !Impl->ReadOnly; },
            [this](const MoveCurrentLines &) { return !Impl->ReadOnly; },
            [this](const ToggleLineComment &) { return !Impl->ReadOnly && !Languages.Get(Impl->LanguageId).SingleLineComment.empty(); },
            [this](const EnterChar &) { return !Impl->ReadOnly; },
        },
        action
    );
}

void TextBuffer::Apply(const ActionType &action) const {
    using namespace Action::TextBuffer;

    std::visit(
        Match{
            // Buffer-affecting actions
            [this](const SetCursor &a) { Commit(GetBuffer().SetCursor(a.lc, a.add)); },
            [this](const SetCursorRange &a) { Commit(GetBuffer().SetCursor(a.lcr, a.add)); },
            [this](const MoveCursorsLines &a) { Commit(GetBuffer().MoveCursorsLines(a.amount, a.select)); },
            [this](const PageCursorsLines &a) { Commit(GetBuffer().MoveCursorsLines((Impl->ContentCoordDims.L - 2) * (a.up ? -1 : 1), a.select)); },
            [this](const MoveCursorsChar &a) { Commit(GetBuffer().MoveCursorsChar(a.right, a.select, a.word)); },
            [this](const MoveCursorsTop &a) { Commit(GetBuffer().MoveCursorsTop(a.select)); },
            [this](const MoveCursorsBottom &a) { Commit(GetBuffer().MoveCursorsBottom(a.select)); },
            [this](const MoveCursorsStartLine &a) { Commit(GetBuffer().MoveCursorsStartLine(a.select)); },
            [this](const MoveCursorsEndLine &a) { Commit(GetBuffer().MoveCursorsEndLine(a.select)); },
            [this](const SelectAll &) { Commit(GetBuffer().SelectAll()); },
            [this](const SelectNextOccurrence &) { Commit(GetBuffer().SelectNextOccurrence()); },
            [this](const Set &a) { Commit(GetBuffer().SetText(a.value)); },
            [this](const ToggleOverwrite &) { Impl->ToggleOverwrite(); },
            [this](const Copy &) {
                const auto str = GetBuffer().GetSelectedText();
                ImGui::SetClipboardText(str.c_str());
            },
            [this](const Cut &) {
                const auto str = GetBuffer().GetSelectedText();
                ImGui::SetClipboardText(str.c_str());
                Commit(GetBuffer().DeleteSelections());
            },
            [this](const Paste &) { Commit(GetBuffer().Paste(Impl->GetClipboardText())); },
            [this](const Delete &a) { Commit(GetBuffer().Delete(a.word)); },
            [this](const Backspace &a) { Commit(GetBuffer().Backspace(a.word)); },
            [this](const DeleteCurrentLines &) { Commit(GetBuffer().DeleteCurrentLines()); },
            [this](const ChangeCurrentLinesIndentation &a) { Commit(GetBuffer().ChangeCurrentLinesIndentation(a.increase)); },
            [this](const MoveCurrentLines &a) { Commit(GetBuffer().MoveCurrentLines(a.up)); },
            [this](const ToggleLineComment &) { Commit(GetBuffer().ToggleLineComment(Languages.Get(Impl->LanguageId).SingleLineComment)); },
            [this](const EnterChar &a) { Commit(GetBuffer().EnterChar(a.value, Impl->AutoIndent)); },
            [this](const Open &a) {
                LastOpenedFilePath.Set(a.file_path);
                Impl->SetFilePath(a.file_path);
                Commit(GetBuffer().SetText(FileIO::read(a.file_path)));
            },
            // Non-buffer actions
            [this](const ShowOpenDialog &) {
                FileDialog.Set({
                    .owner_id = Id,
                    .title = "Open file",
                    .filters = ".*", // No filter for opens. Go nuts :)
                    .save_mode = false,
                    .max_num_selections = 1, // todo open multiple files
                });
            },
            [this](const ShowSaveDialog &) {
                const std::string current_file_ext = fs::path(LastOpenedFilePath).extension();
                FileDialog.Set({
                    .owner_id = Id,
                    .title = std::format("Save {} file", Impl->GetLanguageName()),
                    .filters = current_file_ext,
                    .default_file_name = std::format("my_{}_program{}", Impl->GetLanguageName() | transform(::tolower) | to<std::string>(), current_file_ext),
                    .save_mode = true,
                });
            },
            [this](const Save &a) { FileIO::write(a.file_path, GetBuffer().GetText()); },
        },
        action
    );
}

// todo: Need a way to merge cursor-only edits, and skip over cursor-only buffer changes when undoing/redoing.
void TextBuffer::Commit(TextBufferData b) const {
    RootStore.Set(Id, b);
    Impl->Syntax->ApplyEdits(b.Edits);
    // b.Edits = {};
}

bool TextBuffer::Exists() const { return RootStore.Count<Buffer>(Id); }
Buffer TextBuffer::GetBuffer() const { return RootStore.Get<Buffer>(Id); }
std::string TextBuffer::GetText() const { return GetBuffer().GetText(); }
bool TextBuffer::Empty() const { return GetBuffer().Empty(); }

static bool IsPressed(ImGuiKeyChord chord) {
    const auto window_id = ImGui::GetCurrentWindowRead()->ID;
    ImGui::SetKeyOwnersForKeyChord(chord, window_id); // Prevent app from handling this key press.
    return ImGui::IsKeyChordPressed(chord, ImGuiInputFlags_Repeat, window_id);
}

std::optional<TextBuffer::ActionType> TextBuffer::ProduceKeyboardAction() const {
    using namespace Action::TextBuffer;

    // no-select moves
    if (IsPressed(ImGuiKey_UpArrow)) return MoveCursorsLines{.component_id = Id, .amount = -1, .select = false};
    if (IsPressed(ImGuiKey_DownArrow)) return MoveCursorsLines{.component_id = Id, .amount = 1, .select = false};
    if (IsPressed(ImGuiKey_LeftArrow)) return MoveCursorsChar{.component_id = Id, .right = false, .select = false, .word = false};
    if (IsPressed(ImGuiKey_RightArrow)) return MoveCursorsChar{.component_id = Id, .right = true, .select = false, .word = false};
    if (IsPressed(ImGuiMod_Alt | ImGuiKey_LeftArrow)) return MoveCursorsChar{.component_id = Id, .right = false, .select = false, .word = true};
    if (IsPressed(ImGuiMod_Alt | ImGuiKey_RightArrow)) return MoveCursorsChar{.component_id = Id, .right = true, .select = false, .word = true};
    if (IsPressed(ImGuiKey_PageUp)) return PageCursorsLines{.component_id = Id, .up = false, .select = false};
    if (IsPressed(ImGuiKey_PageDown)) return PageCursorsLines{.component_id = Id, .up = true, .select = false};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_Home)) return MoveCursorsTop{.component_id = Id, .select = false};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_End)) return MoveCursorsBottom{.component_id = Id, .select = false};
    if (IsPressed(ImGuiKey_Home)) return MoveCursorsStartLine{.component_id = Id, .select = false};
    if (IsPressed(ImGuiKey_End)) return MoveCursorsEndLine{.component_id = Id, .select = false};
    // select moves
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_UpArrow)) return MoveCursorsLines{.component_id = Id, .amount = -1, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_DownArrow)) return MoveCursorsLines{.component_id = Id, .amount = 1, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_LeftArrow)) return MoveCursorsChar{.component_id = Id, .right = false, .select = true, .word = false};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_RightArrow)) return MoveCursorsChar{.component_id = Id, .right = true, .select = true, .word = false};
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_LeftArrow)) return MoveCursorsChar{.component_id = Id, .right = false, .select = true, .word = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Alt | ImGuiKey_RightArrow)) return MoveCursorsChar{.component_id = Id, .right = true, .select = true, .word = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_PageUp)) return PageCursorsLines{.component_id = Id, .up = false, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_PageDown)) return PageCursorsLines{.component_id = Id, .up = true, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_Home)) return MoveCursorsTop{.component_id = Id, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_End)) return MoveCursorsBottom{.component_id = Id, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_Home)) return MoveCursorsStartLine{.component_id = Id, .select = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_End)) return MoveCursorsEndLine{.component_id = Id, .select = true};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_A)) return SelectAll{Id};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_D)) return SelectNextOccurrence{Id};
    // cut/copy/paste
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_Insert) || IsPressed(ImGuiMod_Ctrl | ImGuiKey_C)) return Copy{Id};
    if (IsPressed(ImGuiMod_Shift | ImGuiKey_Insert) || IsPressed(ImGuiMod_Ctrl | ImGuiKey_V)) return Paste{Id};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_X) || IsPressed(ImGuiMod_Shift | ImGuiKey_Delete)) {
        if (Impl->ReadOnly) return Copy{Id};
        return Cut{Id};
    }
    // todo readonly toggle
    if (IsPressed(ImGuiKey_Insert)) return ToggleOverwrite{Id};
    // edits
    if (IsPressed(ImGuiKey_Delete)) return Delete{.component_id = Id, .word = false};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_Delete)) return Delete{.component_id = Id, .word = true};
    if (IsPressed(ImGuiKey_Backspace)) return Backspace{.component_id = Id, .word = false};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_Backspace)) return Backspace{.component_id = Id, .word = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_K)) return DeleteCurrentLines{Id};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_LeftBracket) || IsPressed(ImGuiMod_Shift | ImGuiKey_Tab)) {
        return ChangeCurrentLinesIndentation{.component_id = Id, .increase = false};
    }
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_RightBracket) || (IsPressed(ImGuiKey_Tab) && GetBuffer().AnyCursorsMultiline())) {
        return ChangeCurrentLinesIndentation{.component_id = Id, .increase = true};
    }
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_UpArrow)) return MoveCurrentLines{.component_id = Id, .up = true};
    if (IsPressed(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_DownArrow)) return MoveCurrentLines{.component_id = Id, .up = false};
    if (IsPressed(ImGuiMod_Ctrl | ImGuiKey_Slash)) return ToggleLineComment{.component_id = Id};
    if (IsPressed(ImGuiKey_Tab)) return EnterChar{.component_id = Id, .value = '\t'};
    if (IsPressed(ImGuiKey_Enter) || IsPressed(ImGuiKey_KeypadEnter)) return EnterChar{.component_id = Id, .value = '\n'};

    return {};
}

using namespace ImGui;

constexpr float Distance(const ImVec2 &a, const ImVec2 &b) {
    const auto diff = a - b;
    return sqrt(diff.x * diff.x + diff.y * diff.y);
}

std::optional<TextBuffer::ActionType> TextBufferImpl::HandleMouseInputs(TextBufferData b, ImVec2 char_advance, float text_start_x) {
    using namespace Action::TextBuffer;
    constexpr static ImGuiMouseButton MouseLeft = ImGuiMouseButton_Left, MouseMiddle = ImGuiMouseButton_Middle;

    if (!IsWindowHovered()) {
        DestroyHoveredNode();
        return {};
    }

    SetMouseCursor(ImGuiMouseCursor_TextInput);

    if (IsMouseDown(MouseMiddle) && IsMouseDragging(MouseMiddle)) {
        const auto scroll = ImVec2{GetScrollX(), GetScrollY()} - GetMouseDragDelta(MouseMiddle);
        SetScrollX(scroll.x);
        SetScrollY(scroll.y);
    }

    bool is_over_line_number = false;
    const auto mouse_pos = GetMousePos();
    const auto mouse_lc = b.ToLineChar(ScreenPosToCoords(b, mouse_pos, char_advance, text_start_x, &is_over_line_number));
    const auto &io = GetIO();
    const auto is_click = IsMouseClicked(MouseLeft);
    if ((io.KeyShift && is_click) || IsMouseDragging(MouseLeft)) {
        return SetCursorRange{Id, Cursor{b.LastAddedCursor().Start, mouse_lc}, false};
    }
    if (io.KeyShift || io.KeyAlt) return {};

    if (is_over_line_number) DestroyHoveredNode();
    else if (Syntax) CreateHoveredNode(b.ToByteIndex(mouse_lc));

    const float time = GetTime();
    const bool is_double_click = IsMouseDoubleClicked(MouseLeft);
    const bool is_triple_click = is_click && !is_double_click && LastClickTime != -1.0f &&
        time - LastClickTime < io.MouseDoubleClickTime && Distance(io.MousePos, LastClickPos) < 0.01f;
    if (is_triple_click) {
        LastClickTime = -1.0f;
        return SetCursorRange{Id, b.Clamped({mouse_lc.L, 0}, b.CheckedNextLineBegin(mouse_lc.L)), io.KeyCtrl};
    } else if (is_double_click) {
        LastClickTime = time;
        LastClickPos = mouse_pos;
        return SetCursorRange{Id, b.Clamped(b.FindWordBoundary(mouse_lc, true), b.FindWordBoundary(mouse_lc, false)), io.KeyCtrl};
    } else if (is_click) {
        LastClickTime = time;
        LastClickPos = mouse_pos;
        auto lcr = is_over_line_number ? b.Clamped({mouse_lc.L, 0}, b.CheckedNextLineBegin(mouse_lc.L)) : b.Clamped(mouse_lc, mouse_lc);
        return SetCursorRange{Id, std::move(lcr), io.KeyCtrl};
    }

    return {};
}

std::optional<TextBuffer::ActionType> TextBufferImpl::Render(TextBufferData b, bool is_focused) {
    static constexpr float ScrollbarWidth = 14, LeftMargin = 10;

    const float font_size = GetFontSize();
    const float font_width = GetFont()->CalcTextSizeA(font_size, FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
    const float font_height = GetTextLineHeightWithSpacing();
    const ImVec2 char_advance = {font_width, font_height * LineSpacing};
    // Line-number column has room for the max line-num digits plus two spaces.
    const float text_start_x = LeftMargin + (ShowLineNumbers ? std::format("{}  ", std::max(0, int(b.Text.size()) - 1)).size() * font_width : 0);

    const ImVec2 scroll{GetScrollX(), GetScrollY()};
    const ImVec2 cursor_screen_pos = GetCursorScreenPos();
    ContentDims = {
        GetWindowWidth() - (CurrentSpaceDims.x > ContentDims.x ? ScrollbarWidth : 0.0f),
        GetWindowHeight() - (CurrentSpaceDims.y > ContentDims.y ? ScrollbarWidth : 0.0f)
    };
    const Coords first_visible_coords{u32(scroll.y / char_advance.y), u32(std::max(scroll.x - text_start_x, 0.0f) / char_advance.x)};
    const Coords last_visible_coords{u32((ContentDims.y + scroll.y) / char_advance.y), u32((ContentDims.x + scroll.x - text_start_x) / char_advance.x)};
    ContentCoordDims = last_visible_coords - first_visible_coords + Coords{1, 1};

    if (auto edited_cursor = GetEditedCursor(b); edited_cursor) {
        StartEdited.clear();
        EndEdited.clear();

        // Move scroll to keep the edited cursor visible.
        // Goal: Keep all edited cursor(s) visible at all times.
        // So, vars like `end_in_view` mean, "is the end of the edited cursor _fully_ in view?"
        // We assume at least the end has been edited, since it's the _interactive_ end.
        const Coords end{edited_cursor->End.L, b.GetColumn(edited_cursor->End)};

        const bool end_in_view = end.L > first_visible_coords.L && end.L < (std::min(last_visible_coords.L, 1u) - 1) &&
            end.C >= first_visible_coords.C && end.C < last_visible_coords.C;
        // const bool target_start = edited_cursor->StartEdited && end_in_view;
        const bool target_start = end_in_view;
        const auto target = target_start ? Coords{edited_cursor->Start.L, b.GetColumn(edited_cursor->Start)} : end;
        if (target.L <= first_visible_coords.L) {
            SetScrollY(std::max((target.L - 0.5f) * char_advance.y, 0.f));
        } else if (target.L >= last_visible_coords.L) {
            SetScrollY(std::max((target.L + 1.5f) * char_advance.y - ContentDims.y, 0.f));
        }
        if (target.C <= first_visible_coords.C) {
            SetScrollX(std::clamp(text_start_x + (target.C - 0.5f) * char_advance.x, 0.f, scroll.x));
        } else if (target.C >= last_visible_coords.C) {
            SetScrollX(std::max(text_start_x + (target.C + 1.5f) * char_advance.x - ContentDims.x, 0.f));
        }
    }

    const auto mouse_action = HandleMouseInputs(b, char_advance, text_start_x);

    u32 max_column = 0;
    auto dl = GetWindowDrawList();
    auto transition_it = Syntax->CaptureIdTransitions.begin();
    for (u32 li = first_visible_coords.L, byte_index = b.ToByteIndex({first_visible_coords.L, 0});
         li <= last_visible_coords.L && li < b.Text.size(); ++li) {
        const auto &line = b.Text[li];
        const u32 line_max_column = b.GetLineMaxColumn(line, last_visible_coords.C);
        max_column = std::max(line_max_column, max_column);

        const ImVec2 line_start_screen_pos{cursor_screen_pos.x, cursor_screen_pos.y + li * char_advance.y};
        const float text_screen_x = line_start_screen_pos.x + text_start_x;
        const Coords line_start_coord{li, 0}, line_end_coord{li, line_max_column};
        // Draw current line selection
        for (const auto &c : b.Cursors) {
            const auto selection_start = b.ToCoords(c.Min()), selection_end = b.ToCoords(c.Max());
            if (selection_start <= line_end_coord && selection_end > line_start_coord) {
                const u32 start_col = selection_start > line_start_coord ? selection_start.C : 0;
                const u32 end_col = selection_end < line_end_coord ?
                    selection_end.C :
                    line_end_coord.C + (selection_end.L > li || (selection_end.L == li && selection_end > line_end_coord) ? 1 : 0);
                if (start_col < end_col) {
                    const ImVec2 rect_start{text_screen_x + start_col * char_advance.x, line_start_screen_pos.y};
                    const ImVec2 rect_end = rect_start + ImVec2{(end_col - start_col) * char_advance.x, char_advance.y};
                    dl->AddRectFilled(rect_start, rect_end, GetColor(PaletteIndex::Selection));
                }
            }
        }

        if (ShowLineNumbers) {
            // Draw line number (right aligned).
            const std::string line_num_str = std::format("{}  ", li);
            dl->AddText({text_screen_x - line_num_str.size() * font_width, line_start_screen_pos.y}, GetColor(PaletteIndex::LineNumber), line_num_str.c_str());
        }

        // Render cursors
        if (is_focused) {
            {
                // (Copied from ImGui::InputTextEx)
                // Notify OS of text input position for advanced IME (-1 x offset so that Windows IME can cover our cursor. Bit of an extra nicety.)
                auto &g = *GImGui;
                g.PlatformImeData.WantVisible = true;
                g.PlatformImeData.InputPos = {cursor_screen_pos.x - 1.0f, cursor_screen_pos.y - g.FontSize};
                g.PlatformImeData.InputLineHeight = g.FontSize;
                g.PlatformImeViewport = ImGui::GetCurrentWindowRead()->Viewport->ID;
            }

            for (const auto &c : filter(b.Cursors, [li](const auto &c) { return c.Line() == li; })) {
                const u32 ci = c.CharIndex(), column = b.GetColumn(line, ci);
                const float width = !Overwrite || ci >= line.size() ? 1.f : (line[ci] == '\t' ? GTextBufferStyle.NumTabSpacesAtColumn(column) : 1) * char_advance.x;
                const ImVec2 pos{text_screen_x + column * char_advance.x, line_start_screen_pos.y};
                dl->AddRectFilled(pos, pos + ImVec2{width, char_advance.y}, GetColor(PaletteIndex::Cursor));
            }
        }

        // Render colorized text
        const u32 line_start_byte_index = byte_index, start_ci = b.GetFirstVisibleCharIndex(line, first_visible_coords.C);
        byte_index += start_ci;
        transition_it.MoveForwardTo(byte_index);
        for (u32 ci = start_ci, column = first_visible_coords.C; ci < line.size() && column <= last_visible_coords.C;) {
            const auto lc = LineChar{li, ci};
            const ImVec2 glyph_pos = line_start_screen_pos + ImVec2{text_start_x + column * char_advance.x, 0};
            const char ch = line[lc.C];
            const u32 seq_length = UTF8CharLength(ch);
            if (ch == '\t') {
                if (ShowWhitespaces) {
                    const float gap = font_size * (ShortTabs ? 0.16f : 0.2f);
                    const ImVec2 p1{glyph_pos + ImVec2{char_advance.x * 0.3f, font_height * 0.5f}};
                    const ImVec2 p2{glyph_pos.x + char_advance.x * (ShortTabs ? (GTextBufferStyle.NumTabSpacesAtColumn(column) - 0.3f) : 1.f), p1.y};
                    const u32 color = GetColor(PaletteIndex::ControlCharacter);
                    dl->AddLine(p1, p2, color);
                    dl->AddLine(p2, {p2.x - gap, p1.y - gap}, color);
                    dl->AddLine(p2, {p2.x - gap, p1.y + gap}, color);
                }
            } else if (ch == ' ') {
                if (ShowWhitespaces) {
                    dl->AddCircleFilled(glyph_pos + ImVec2{font_width, font_size} * 0.5f, 1.5f, GetColor(PaletteIndex::ControlCharacter), 4);
                }
            } else {
                if (seq_length == 1 && b.Cursors.size() == 1) {
                    if (const auto matching_brackets = b.FindMatchingBrackets(b.Cursors.front())) {
                        if (matching_brackets->Start == lc || matching_brackets->End == lc) {
                            const ImVec2 start{glyph_pos + ImVec2{0, font_height + 1.0f}};
                            dl->AddRectFilled(start, start + ImVec2{char_advance.x, 1.0f}, GetColor(PaletteIndex::Cursor));
                        }
                    }
                }
                // Render the current character.
                const auto &char_style = Syntax->StyleByCaptureId.at(*transition_it);
                const bool font_changed = Fonts::Push(FontFamily::Monospace, char_style.Font);
                const char *seq_begin = &line[ci];
                dl->AddText(glyph_pos, char_style.Color, seq_begin, seq_begin + seq_length);
                if (font_changed) Fonts::Pop();
            }
            if (ShowStyleTransitionPoints && !transition_it.IsEnd() && transition_it.ByteIndex == byte_index) {
                const auto color = SetAlpha(Syntax->StyleByCaptureId.at(*transition_it).Color, 40);
                dl->AddRectFilled(glyph_pos, glyph_pos + char_advance, color);
            }
            if (ShowChangedCaptureRanges) {
                for (const auto &range : Syntax->ChangedCaptureRanges) {
                    if (byte_index >= range.Start && byte_index < range.End) {
                        dl->AddRectFilled(glyph_pos, glyph_pos + char_advance, Col32(255, 255, 255, 20));
                    }
                }
            }
            std::tie(ci, column) = b.NextCharIndexAndColumn(line, ci, column);
            byte_index += seq_length;
            transition_it.MoveForwardTo(byte_index);
        }
        byte_index = line_start_byte_index + line.size() + 1; // + 1 for the newline character.
    }

    CurrentSpaceDims = {
        std::max((max_column + std::min(ContentCoordDims.C - 1, max_column)) * char_advance.x, CurrentSpaceDims.x),
        (b.Text.size() + std::min(ContentCoordDims.L - 1, u32(b.Text.size()))) * char_advance.y
    };

    ImGui::SetCursorPos({0, 0});

    // Stack invisible items to push node hierarchy to ImGui stack.
    if (Syntax && HoveredNode) {
        const auto before_cursor = ImGui::GetCursorScreenPos();
        for (const auto &node : HoveredNode->Ancestry) {
            PushOverrideID(node.Id);
            InvisibleButton("", CurrentSpaceDims, ImGuiButtonFlags_AllowOverlap);
            ImGui::SetCursorScreenPos(before_cursor);
        }
        for (u32 i = 0; i < HoveredNode->Ancestry.size(); ++i) PopID();
    }

    Dummy(CurrentSpaceDims);

    return mouse_action;
}

void TextBuffer::Refresh() {
    if (IsChanged()) {
        const auto b = GetBuffer();
        Impl->Syntax->ApplyEdits(b.Edits);
        // todo only mark changed cursors. need a way to compare with previous.
        for (u32 i = 0; i < b.Cursors.size(); ++i) {
            Impl->StartEdited.insert(i);
            Impl->EndEdited.insert(i);
        }
    }
}

void TextBuffer::Render() const {
    static std::string PrevSelectedPath = "";
    if (FileDialog.OwnerId == Id && PrevSelectedPath != FileDialog.SelectedFilePath) {
        const fs::path selected_path = FileDialog.SelectedFilePath;
        PrevSelectedPath = FileDialog.SelectedFilePath = "";
        if (FileDialog.SaveMode) Q(Action::TextBuffer::Save{Id, selected_path});
        else Q(Action::TextBuffer::Open{Id, selected_path});
    }

    const auto b = GetBuffer();
    const auto cursor_coords = b.GetCursorPosition();
    const std::string editing_file = LastOpenedFilePath ? string(fs::path(LastOpenedFilePath).filename()) : "No file";
    ImGui::Text(
        "%6d/%-6d %6d lines  | %s | %s | %s | %s", cursor_coords.L + 1, cursor_coords.C + 1, int(b.Text.size()),
        Impl->Overwrite ? "Ovr" : "Ins",
        IsChanged() ? "*" : " ", // todo show if buffer is dirty
        Impl->GetLanguageName().data(),
        editing_file.c_str()
    );

    const bool is_parent_focused = IsWindowFocused();
    PushStyleColor(ImGuiCol_ChildBg, Impl->GetColor(PaletteIndex::Background));
    PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    BeginChild("TextBuffer", {}, false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

    const bool font_changed = Fonts::Push(FontFamily::Monospace);
    const bool is_focused = IsWindowFocused() || is_parent_focused;
    if (is_focused) {
        auto &io = GetIO();
        io.WantCaptureKeyboard = io.WantTextInput = true;

        // (Copied from ImGui::InputTextEx)
        // Process regular text input (before we check for Return because using some IME will effectively send a Return?)
        // We ignore CTRL inputs, but need to allow ALT+CTRL as some keyboards (e.g. German) use AltGR (which _is_ Alt+Ctrl) to input certain characters.
        const bool ignore_char_inputs = (io.KeyCtrl && !io.KeyAlt) || (io.ConfigMacOSXBehaviors && io.KeyCtrl);
        if (auto action = ProduceKeyboardAction(); action && CanApply(*action)) Q(*action);
        else if (!io.InputQueueCharacters.empty() && !ignore_char_inputs) {
            for (const auto ch : io.InputQueueCharacters) {
                if (ch != 0 && (ch == '\n' || ch >= 32)) Q(Action::TextBuffer::EnterChar{.component_id = Id, .value = ch});
            }
            io.InputQueueCharacters.resize(0);
        }
    }
    if (auto action = Impl->Render(b, is_focused)) Q(*action);
    if (font_changed) Fonts::Pop();

    EndChild();
    PopStyleVar();
    PopStyleColor();
}

void TextBuffer::RenderMenu() const {
    FileMenu.Draw();

    if (BeginMenu("Edit")) {
        MenuItem("Read-only mode", nullptr, &Impl->ReadOnly);
        Separator();
        if (const auto a = Action::TextBuffer::Copy{Id}; MenuItem("Copy", "cmd+c", nullptr, CanApply(a))) Q(a);
        if (const auto a = Action::TextBuffer::Cut{Id}; MenuItem("Cut", "cmd+x", nullptr, CanApply(a))) Q(a);
        if (const auto a = Action::TextBuffer::Paste{Id}; MenuItem("Paste", "cmd+v", nullptr, CanApply(a))) Q(a);
        Separator();
        if (MenuItem("Select all", nullptr, nullptr)) Q(Action::TextBuffer::SelectAll{Id});
        EndMenu();
    }

    if (BeginMenu("View")) {
        if (BeginMenu("Palette")) {
            if (MenuItem("Mariana palette")) Impl->SetPalette(TextBufferPaletteId::Mariana);
            if (MenuItem("Dark palette")) Impl->SetPalette(TextBufferPaletteId::Dark);
            if (MenuItem("Light palette")) Impl->SetPalette(TextBufferPaletteId::Light);
            if (MenuItem("Retro blue palette")) Impl->SetPalette(TextBufferPaletteId::RetroBlue);
            EndMenu();
        }
        MenuItem("Show style transition points", nullptr, &Impl->ShowStyleTransitionPoints);
        MenuItem("Show changed capture ranges", nullptr, &Impl->ShowChangedCaptureRanges);
        gWindows.ToggleDebugMenuItem(Debug);
        EndMenu();
    }
}

void TextBuffer::RenderDebug() const {
    const auto b = GetBuffer();
    if (CollapsingHeader("Editor state")) {
        ImGui::Text("Cursor count: %lu", b.Cursors.size());
        for (const auto &c : b.Cursors) {
            const auto &start = c.Start, &end = c.End;
            ImGui::Text("Start: {%d, %d}(%u), End: {%d, %d}(%u)", start.L, start.C, b.ToByteIndex(start), end.L, end.C, b.ToByteIndex(end));
        }
        if (CollapsingHeader("Line lengths")) {
            for (u32 i = 0; i < b.Text.size(); i++) ImGui::Text("%u: %lu", i, b.Text[i].size());
        }
    }
    Text("Edits: %lu", b.Edits.size());
    for (const auto &edit : b.Edits) {
        BulletText("Start: %d, Old end: %d, New end: %d", edit.StartByte, edit.OldEndByte, edit.NewEndByte);
    }

    if (CollapsingHeader("Tree-Sitter")) {
        ImGui::Text("S-expression:\n%s", Impl->GetSyntaxTreeSExp().c_str());
    }
}
