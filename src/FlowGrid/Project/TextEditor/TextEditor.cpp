#include "TextEditor.h"

#include <algorithm>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/transform.hpp>
#include <ranges>
#include <set>
#include <string>

#include "imgui_internal.h"
#include <tree_sitter/api.h>

#include "Helper/File.h"

using std::string, std::views::filter, std::ranges::reverse_view, std::ranges::any_of, std::ranges::all_of, std::ranges::subrange;

// Implemented by the grammar libraries in `lib/tree-sitter-grammars/`.
extern "C" TSLanguage *tree_sitter_json();
extern "C" TSLanguage *tree_sitter_cpp();

void AddTypes(LanguageDefinition::PaletteT &palette, PaletteIndex index, std::initializer_list<std::string> types) {
    for (const auto &type : types) palette[type] = index;
}

LanguageDefinition::PaletteT LanguageDefinition::CreatePalette(LanguageID id) {
    PaletteT p;
    using PI = PaletteIndex;
    switch (id) {
        case LanguageID::Cpp:
            AddTypes(p, PI::Keyword, {"auto", "break", "case", "const", "constexpr", "continue", "default", "do", "else", "extern", "false", "for", "if", "nullptr", "private", "return", "static", "struct", "switch", "this", "true", "using", "while"});
            AddTypes(p, PI::Operator, {"!", "!=", "&", "&&", "&=", "*", "++", "+=", "-", "--", "-=", "->", "/", "<", "<=", "=", "==", ">", ">=", "[", "]", "^=", "|", "||", "~"});
            AddTypes(p, PI::NumberLiteral, {"number_literal"});
            AddTypes(p, PI::CharLiteral, {"character"});
            AddTypes(p, PI::StringLiteral, {"string_content", "\"", "'", "system_lib_string"});
            AddTypes(p, PI::Identifier, {"identifier", "field_identifier", "namespace_identifier", "translation_unit", "type_identifier"});
            AddTypes(p, PI::Type, {"primitive_type"});
            AddTypes(p, PI::Preprocessor, {"#define", "#include", "preproc_arg"});
            AddTypes(p, PI::Punctuation, {"(", ")", "+", ",", ".", ":", "::", ";", "?", "{", "}"});
            AddTypes(p, PI::Comment, {"escape_sequence", "comment"});
            break;
        case LanguageID::Json:
            AddTypes(p, PI::Type, {"true", "false", "null"});
            AddTypes(p, PI::NumberLiteral, {"number"});
            AddTypes(p, PI::StringLiteral, {"string_content", "\""});
            AddTypes(p, PI::Punctuation, {",", ":", "[", "]", "{", "}"});
            break;
        default:
    }

    return p;
}

LanguageDefinitions::LanguageDefinitions()
    : ById{
          {ID::None, {ID::None, "None"}},
          {ID::Cpp, {ID::Cpp, "C++", tree_sitter_cpp(), {".h", ".hpp", ".cpp"}, "//"}},
          {ID::Json, {ID::Json, "JSON", tree_sitter_json(), {".json"}}},
      } {
    for (const auto &[id, lang] : ById) {
        for (const auto &ext : lang.FileExtensions) ByFileExtension[ext] = id;
    }
    for (const auto &ext : ByFileExtension | std::views::keys) AllFileExtensionsFilter += ext + ',';
}

struct TextEditor::CodeParser {
    CodeParser() : Parser(ts_parser_new()) {}
    ~CodeParser() { ts_parser_delete(Parser); }

    void SetLanguage(TSLanguage *language) {
        Language = language;
        ts_parser_set_language(Parser, Language);
    }
    TSLanguage *GetLanguage() const { return Language; }

    TSParser *get() const { return Parser; }

private:
    TSParser *Parser;
    TSLanguage *Language;
};

TextEditor::TextEditor(std::string_view text, LanguageID language_id) : Parser(std::make_unique<CodeParser>()) {
    SetText(string(text));
    SetLanguage(language_id);
    SetPalette(DefaultPaletteId);
}
TextEditor::TextEditor(const fs::path &file_path) : Parser(std::make_unique<CodeParser>()) {
    SetText(FileIO::read(file_path));
    SetFilePath(file_path);
    SetPalette(DefaultPaletteId);
}

TextEditor::~TextEditor() {
    ts_tree_delete(Tree);
}

const char *TSReadText(void *payload, uint32_t byte_index, TSPoint position, uint32_t *bytes_read) {
    (void)byte_index; // Unused.
    static const char newline = '\n';

    const auto *editor = static_cast<TextEditor *>(payload);
    if (position.row >= editor->LineCount()) {
        *bytes_read = 0;
        return nullptr;
    }
    const auto &line = editor->GetLine(position.row);
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
    return line.data() + position.column;
}

void TextEditor::Parse() {
    TSInput input{this, TSReadText, TSInputEncodingUTF8};
    Tree = ts_parser_parse(Parser->get(), Tree, std::move(input));
}

string TextEditor::GetSyntaxTreeSExp() const {
    char *c_string = ts_node_string(ts_tree_root_node(Tree));
    string s_expression(c_string);
    free(c_string);
    return s_expression;
}

void TextEditor::Highlight() {
    if (Parser->GetLanguage() == nullptr) return;

    Parse();
    const auto &palette = GetLanguage().Palette;
    TSNode root_node = ts_tree_root_node(Tree);
    TSTreeCursor cursor = ts_tree_cursor_new(root_node);
    while (true) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        const TSPoint start_point = ts_node_start_point(node), end_point = ts_node_end_point(node);
        const string type_name = ts_node_type(node);
        const bool is_error = type_name == "ERROR";
        // todo Handle node group types other than comments.
        if (ts_node_child_count(node) == 0 || type_name == "comment" || is_error) {
            const auto palette_index = is_error ? PaletteIndex::Error :
                palette.contains(type_name)     ? palette.at(type_name) :
                                                  PaletteIndex::Default;
            // if (!palette.contains(type_name)) std::println("Unknown type name: {}", type_name);

            // Add palette index for each char in the node.
            for (auto b = start_point; b.row < end_point.row || (b.row == end_point.row && b.column < end_point.column);) {
                if (b.row >= Lines.size()) break;
                if (b.column >= Lines[b.row].size()) {
                    ++b.row;
                    b.column = 0;
                    continue;
                }
                PaletteIndices[b.row][b.column] = palette_index;
                ++b.column;
            }
        }

        /**
          Highlight everything within an error node as an error.
          E.g. TS will parse an incomplete multi-line comment as an "ERROR" token, with the children:
            (Operator '/'), (pointer dereference '*'), ... identifiers, etc.
          Note that most text editors highlight the remainder of incomplete nodes (like unclosed string literals of multi-line comments)
          as a continuation of the incomplete type (e.g. highlighting all chars in the document after an unclosed multi-line comment in comment color).
          Rather than doing manual language-specific work to emulate this common behavior, we lean into tree-sitter's focus on accuracy.
          Highlighting incomplete tokens as errors is a feature!
        */
        if (!is_error && ts_tree_cursor_goto_first_child(&cursor)) continue;

        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return;
            }
        }
    }
}

const TextEditor::PaletteT &TextEditor::GetPalette() const {
    switch (PaletteId) {
        case PaletteIdT::Dark:
            return DarkPalette;
        case PaletteIdT::Light:
            return LightPalette;
        case PaletteIdT::Mariana:
            return MarianaPalette;
        case PaletteIdT::RetroBlue:
            return RetroBluePalette;
    }
}

void TextEditor::SetPalette(PaletteIdT palette_id) { PaletteId = palette_id; }

void TextEditor::SetLanguage(LanguageID language_id) {
    if (LanguageId == language_id) return;

    LanguageId = language_id;
    Parser->SetLanguage(GetLanguage().TsLanguage);
    Tree = nullptr;
    TextChanged = true;
}

void TextEditor::SetNumTabSpaces(uint tab_size) { NumTabSpaces = std::clamp(tab_size, 1u, 8u); }
void TextEditor::SetLineSpacing(float line_spacing) { LineSpacing = std::clamp(line_spacing, 1.f, 2.f); }

void TextEditor::SelectAll() {
    Cursors.Reset();
    Cursors.MoveTop();
    Cursors.MoveBottom(*this, true);
}

void TextEditor::Copy() {
    const string str = Cursors.AnyRanged() ?
        Cursors |
            // Using range-v3 here since clang's libc++ doesn't yet implement `join_with` (which is just `join` in range-v3).
            ranges::views::filter([](const auto &c) { return c.IsRange(); }) |
            ranges::views::transform([this](const auto &c) { return GetSelectedText(c); }) |
            ranges::views::join('\n') | ranges::to<string> :
        Lines[GetCursorPosition().L] | ranges::to<string>;
    ImGui::SetClipboardText(str.c_str());
}

void TextEditor::Cut() {
    if (!Cursors.AnyRanged()) return;

    UndoRecord u{Cursors};
    Copy();
    for (auto &c : reverse_view(Cursors)) DeleteSelection(c, u);
    AddUndo(u);
}

void TextEditor::Paste() {
    // Check if we should do multicursor paste.
    const string clip_text = ImGui::GetClipboardText();
    bool can_paste_to_multiple_cursors = false;
    std::vector<std::pair<uint, uint>> clip_text_lines;
    if (Cursors.size() > 1) {
        clip_text_lines.push_back({0, 0});
        for (uint i = 0; i < clip_text.length(); ++i) {
            if (clip_text[i] == '\n') {
                clip_text_lines.back().second = i;
                clip_text_lines.push_back({i + 1, 0});
            }
        }
        clip_text_lines.back().second = clip_text.length();
        can_paste_to_multiple_cursors = clip_text_lines.size() == Cursors.size() + 1;
    }

    if (clip_text.length() > 0) {
        UndoRecord u{Cursors};
        for (auto &c : reverse_view(Cursors)) DeleteSelection(c, u);

        for (int c = Cursors.size() - 1; c > -1; --c) {
            auto &cursor = Cursors[c];
            const string insert_text = can_paste_to_multiple_cursors ? clip_text.substr(clip_text_lines[c].first, clip_text_lines[c].second - clip_text_lines[c].first) : clip_text;
            InsertTextAtCursor(insert_text, cursor, u);
        }

        AddUndo(u);
    }
}

void TextEditor::Undo(uint steps) {
    while (CanUndo() && steps-- > 0) UndoBuffer[--UndoIndex].Undo(this);
}
void TextEditor::Redo(uint steps) {
    while (CanRedo() && steps-- > 0) UndoBuffer[UndoIndex++].Redo(this);
}

void TextEditor::SetText(const string &text) {
    const uint old_end_byte = ToByteIndex(EndLC());
    Lines.clear();
    PaletteIndices.clear();
    Lines.push_back({});
    PaletteIndices.push_back({});
    for (auto chr : text) {
        if (chr == '\r') continue; // Ignore the carriage return character.
        if (chr == '\n') {
            Lines.push_back({});
            PaletteIndices.push_back({});
        } else {
            Lines.back().emplace_back(chr);
            PaletteIndices.back().emplace_back(PaletteIndex::Default);
        }
    }

    ScrollToTop = true;

    UndoBuffer.clear();
    UndoIndex = 0;

    OnTextChanged(0, old_end_byte, ToByteIndex(EndLC()));
}

void TextEditor::SetFilePath(const fs::path &file_path) {
    const string extension = file_path.extension();
    SetLanguage(!extension.empty() && Languages.ByFileExtension.contains(extension) ? Languages.ByFileExtension.at(extension) : LanguageID::None);
}

void TextEditor::AddUndoOp(UndoRecord &record, UndoOperationType type, LineChar start, LineChar end) {
    auto text = GetText(start, end);
    if (!text.empty()) record.Operations.emplace_back(std::move(text), std::move(start), std::move(end), type);
}

string TextEditor::GetText(LineChar start, LineChar end) const {
    if (end <= start) return "";

    const uint start_li = start.L, end_li = std::min(uint(Lines.size()) - 1, end.L);
    const uint start_ci = start.C, end_ci = end.C;
    string result;
    for (uint ci = start_ci, li = start_li; li < end_li || ci < end_ci;) {
        const auto &line = Lines[li];
        if (ci < line.size()) {
            result += line[ci++];
        } else {
            ++li;
            ci = 0;
            result += '\n';
        }
    }

    return result;
}

bool TextEditor::Render(const char *title, bool is_parent_focused, const ImVec2 &size, bool border) {
    if (CursorPositionChanged) OnCursorPositionChanged();
    CursorPositionChanged = false;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetColor(PaletteIndex::Background));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
    ImGui::BeginChild(title, size, border, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

    const bool is_focused = ImGui::IsWindowFocused();
    HandleKeyboardInputs(is_parent_focused);
    HandleMouseInputs();

    if (TextChanged) {
        Highlight();
        TextChanged = false;
    }

    Render(is_parent_focused);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    return is_focused;
}

// https://en.wikipedia.org/wiki/UTF-8
// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
static uint UTF8CharLength(char ch) {
    if ((ch & 0xFE) == 0xFC) return 6;
    if ((ch & 0xFC) == 0xF8) return 5;
    if ((ch & 0xF8) == 0xF0) return 4;
    if ((ch & 0xF0) == 0xE0) return 3;
    if ((ch & 0xE0) == 0xC0) return 2;
    return 1;
}

static bool IsUTFSequence(char c) { return (c & 0xC0) == 0x80; }

static bool IsWordChar(char ch) {
    return UTF8CharLength(ch) > 1 || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
}

bool TextEditor::Cursors::AnyRanged() const {
    return any_of(Cursors, [](const auto &c) { return c.IsRange(); });
}
bool TextEditor::Cursors::AllRanged() const {
    return all_of(Cursors, [](const auto &c) { return c.IsRange(); });
}
bool TextEditor::Cursors::AnyMultiline() const {
    return any_of(Cursors, [](const auto &c) { return c.IsMultiline(); });
}

void TextEditor::Cursors::Add() {
    Cursors.push_back({});
    LastAddedIndex = size() - 1;
}
void TextEditor::Cursors::Reset() {
    Cursors.clear();
    Add();
}
void TextEditor::Cursors::SortAndMerge() {
    if (size() <= 1) return;

    // Sort cursors.
    const auto last_added_cursor_lc = GetLastAdded().LC();
    std::ranges::sort(Cursors, [](const auto &a, const auto &b) { return a.Min() < b.Min(); });

    // Merge overlapping cursors.
    std::vector<Cursor> merged;
    Cursor current = front();
    for (size_t c = 1; c < size(); ++c) {
        const auto &next = (*this)[c];
        if (current.Max() >= next.Min()) {
            // Overlap. Extend the current cursor to to include the next.
            const auto start = std::min(current.Min(), next.Min());
            const auto end = std::max(current.Max(), next.Max());
            current.Set(start, end);
        } else {
            // No overlap. Finalize the current cursor and start a new merge.
            merged.push_back(current);
            current = next;
        }
    }

    merged.push_back(current);
    Cursors = std::move(merged);

    // Update last added cursor index to be valid after sort/merge.
    const auto it = std::ranges::find_if(*this, [&last_added_cursor_lc](const auto &c) { return c.LC() == last_added_cursor_lc; });
    LastAddedIndex = it != end() ? std::distance(begin(), it) : 0;
}

std::optional<std::pair<TextEditor::Coords, TextEditor::Coords>> TextEditor::Cursors::GetEditedCoordRange(const TextEditor &editor) {
    std::optional<Coords> min_coord, max_coord;
    for (auto &c : Cursors) {
        if (c.IsStartEdited()) {
            c.SetStart({0, 0});
            auto start = c.GetStartCoords(editor);
            min_coord = std::min(min_coord.value_or(start), start);
            max_coord = std::max(max_coord.value_or(start), start);
        }
        if (c.IsEndEdited()) {
            auto end = c.GetEndCoords(editor);
            min_coord = std::min(min_coord.value_or(end), end);
            max_coord = std::max(max_coord.value_or(end), end);
        }
    }
    if (min_coord.has_value() && max_coord.has_value()) return std::make_pair(*min_coord, *max_coord);
    return {};
}

void TextEditor::UndoRecord::Undo(TextEditor *editor) {
    editor->Cursors = After; // todo this needs work
    for (const auto &op : reverse_view(Operations)) {
        switch (op.Type) {
            case UndoOperationType::Delete: {
                editor->InsertTextAt(op.Start, op.Text);
                break;
            }
            case UndoOperationType::Add: {
                editor->DeleteRange(op.Start, op.End);
                break;
            }
        }
    }
    editor->Cursors = Before;
}

void TextEditor::UndoRecord::Redo(TextEditor *editor) {
    editor->Cursors = Before;
    for (const auto &op : Operations) {
        switch (op.Type) {
            case UndoOperationType::Delete: {
                editor->DeleteRange(op.Start, op.End);
                break;
            }
            case UndoOperationType::Add: {
                editor->InsertTextAt(op.Start, op.Text);
                break;
            }
        }
    }
    editor->Cursors = After;
}

void TextEditor::InsertTextAtCursor(const string &text, Cursor &c, UndoRecord &u) {
    if (text.empty()) return;

    const auto start = c.Min();
    c.Set(InsertTextAt(start, text));
    u.Operations.emplace_back(text, start, c.GetEnd(), UndoOperationType::Add);
}

void TextEditor::LinesIter::MoveRight() {
    if (LC == End) return;

    if (LC.C == Lines[LC.L].size()) {
        ++LC.L;
        LC.C = 0;
    } else {
        LC.C = std::min(LC.C + UTF8CharLength(Lines[LC.L][LC.C]), uint(Lines[LC.L].size()));
    }
}

void TextEditor::LinesIter::MoveLeft() {
    if (LC == Begin) return;

    if (LC.C == 0) {
        --LC.L;
        LC.C = Lines[LC.L].size();
    } else {
        do { --LC.C; } while (LC.C > 0 && IsUTFSequence(Lines[LC.L][LC.C]));
    }
}

static uint NextTabstop(uint column, uint tab_size) { return ((column / tab_size) + 1) * tab_size; }

void TextEditor::MoveCharIndexAndColumn(const LineT &line, uint &ci, uint &column) const {
    const char ch = line[ci];
    ci += UTF8CharLength(ch);
    column = ch == '\t' ? NextTabstop(column, NumTabSpaces) : column + 1;
}

uint TextEditor::NumStartingSpaceColumns(uint li) const {
    const auto &line = Lines[li];
    uint column = 0;
    for (uint ci = 0; ci < line.size() && isblank(line[ci]);) MoveCharIndexAndColumn(line, ci, column);
    return column;
}

void TextEditor::Cursor::MoveLines(const TextEditor &editor, int amount, bool select) {
    Set({uint(std::clamp(int(Line()) + amount, 0, int(editor.LineCount() - 1))), CharIndex()}, !select);
}

void TextEditor::Cursor::MoveChar(const TextEditor &editor, bool right, bool select, bool is_word_mode) {
    auto lci = editor.Iter(LC());
    if ((!right && !lci.IsBegin()) || (right && !lci.IsEnd())) {
        if (right) ++lci;
        else --lci;
        Set(is_word_mode ? editor.FindWordBoundary(*lci, !right) : *lci, !select);
    }
}

void TextEditor::Cursors::MoveLines(const TextEditor &editor, int amount, bool select) {
    for (auto &c : Cursors) c.MoveLines(editor, amount, select);
}
void TextEditor::Cursors::MoveChar(const TextEditor &editor, bool right, bool select, bool is_word_mode) {
    const bool any_selections = AnyRanged();
    for (auto &c : Cursors) {
        if (any_selections && !select && !is_word_mode) c.Set(right ? c.Max() : c.Min(), true);
        else c.MoveChar(editor, right, select, is_word_mode);
    }
}
void TextEditor::Cursors::MoveTop(bool select) { Cursors.back().Set({0, 0}, !select); }
void TextEditor::Cursors::MoveBottom(const TextEditor &editor, bool select) { Cursors.back().Set(editor.LineMaxLC(editor.LineCount() - 1), !select); }
void TextEditor::Cursors::MoveStart(bool select) {
    for (auto &c : Cursors) c.Set({c.Line(), 0}, !select);
}
void TextEditor::Cursors::MoveEnd(const TextEditor &editor, bool select) {
    for (auto &c : Cursors) c.Set(editor.LineMaxLC(c.Line()), !select);
}

void TextEditor::EnterChar(ImWchar ch, bool is_shift) {
    if (ch == '\t' && Cursors.AnyMultiline()) return ChangeCurrentLinesIndentation(!is_shift);

    UndoRecord u{Cursors};
    for (auto &c : reverse_view(Cursors)) DeleteSelection(c, u);

    // Order is important here for typing '\n' in the same line with multiple cursors.
    for (auto &c : reverse_view(Cursors)) {
        string insert_text;
        if (ch == '\n') {
            insert_text = "\n";
            if (AutoIndent) {
                // Match the indentation of the current or next line, whichever has more indentation.
                const uint li = c.Line();
                const uint indent_li = li < Lines.size() - 1 && NumStartingSpaceColumns(li + 1) > NumStartingSpaceColumns(li) ? li + 1 : li;
                const auto &indent_line = Lines[indent_li];
                for (uint i = 0; i < indent_line.size() && isblank(indent_line[i]); ++i) insert_text += indent_line[i];
            }
        } else {
            char buf[5];
            ImTextCharToUtf8(buf, ch);
            insert_text = buf;
        }

        InsertTextAtCursor(insert_text, c, u);
    }

    AddUndo(u);
}

void TextEditor::Backspace(bool is_word_mode) {
    if (Cursors.AnyRanged()) {
        Delete(is_word_mode);
    } else {
        auto before_state = Cursors;
        Cursors.MoveChar(*this, false, true, is_word_mode);
        // Can't do backspace if any cursor is at {0,0}.
        if (!Cursors.AllRanged()) {
            if (Cursors.AnyRanged()) Cursors.MoveChar(*this, true);
            return;
        }
        OnCursorPositionChanged(); // Might combine cursors.
        Delete(is_word_mode, &before_state);
    }
}

void TextEditor::Delete(bool is_word_mode, const struct Cursors *editor_state) {
    if (Cursors.AnyRanged()) {
        UndoRecord u{editor_state == nullptr ? Cursors : *editor_state};
        for (auto &c : reverse_view(Cursors)) DeleteSelection(c, u);
        AddUndo(u);
    } else {
        auto before_state = Cursors;
        Cursors.MoveChar(*this, true, true, is_word_mode);
        // Can't do delete if any cursor is at the end of the last line.
        if (!Cursors.AllRanged()) {
            if (Cursors.AnyRanged()) Cursors.MoveChar(*this, false);
            return;
        }

        OnCursorPositionChanged(); // Might combine cursors.
        Delete(is_word_mode, &before_state);
    }
}

void TextEditor::SetSelection(LineChar start, LineChar end, Cursor &c) {
    const LineChar min_lc{0, 0}, max_lc{LineMaxLC(Lines.size() - 1)};
    c.Set(std::clamp(start, min_lc, max_lc), std::clamp(end, min_lc, max_lc));
}

void TextEditor::AddCursorForNextOccurrence(bool case_sensitive) {
    const auto &c = Cursors.GetLastAdded();
    if (const auto match_range = FindNextOccurrence(GetSelectedText(c), c.Max(), case_sensitive)) {
        Cursors.Add();
        SetSelection(match_range->GetStart(), match_range->GetEnd(), Cursors.back());
        Cursors.SortAndMerge();
    }
}

static char ToLower(char ch, bool case_sensitive) { return (!case_sensitive && ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch; }

std::optional<TextEditor::Cursor> TextEditor::FindNextOccurrence(const string &text, LineChar start, bool case_sensitive) {
    if (text.empty()) return {};

    auto find_lci = Iter(start);
    do {
        auto match_lci = find_lci;
        for (uint i = 0; i < text.size(); ++i) {
            const auto &match_lc = *match_lci;
            if (match_lc.C == Lines[match_lc.L].size()) {
                if (text[i] != '\n' || match_lc.L + 1 >= Lines.size()) break;
            } else if (ToLower(match_lci, case_sensitive) != ToLower(text[i], case_sensitive)) {
                break;
            }

            ++match_lci;
            if (i == text.size() - 1) return Cursor{*find_lci, *match_lci};
        }

        ++find_lci;
        if (find_lci.IsEnd()) find_lci.Reset();
    } while (*find_lci != start);

    return {};
}

std::optional<TextEditor::Cursor> TextEditor::FindMatchingBrackets(const Cursor &c) {
    static const std::unordered_map<char, char>
        OpenToCloseChar{{'{', '}'}, {'(', ')'}, {'[', ']'}},
        CloseToOpenChar{{'}', '{'}, {')', '('}, {']', '['}};

    const LineChar cursor_lc = c.LC();
    const auto &line = Lines[cursor_lc.L];
    if (c.IsRange() || line.empty()) return {};

    uint ci = cursor_lc.C;
    // Considered on bracket if cursor is to the left or right of it.
    if (ci > 0 && (CloseToOpenChar.contains(line[ci - 1]) || OpenToCloseChar.contains(line[ci - 1]))) --ci;

    const char ch = line[ci];
    const bool is_close_char = CloseToOpenChar.contains(ch), is_open_char = OpenToCloseChar.contains(ch);
    if (!is_close_char && !is_open_char) return {};

    const char other_ch = is_close_char ? CloseToOpenChar.at(ch) : OpenToCloseChar.at(ch);
    uint match_count = 0;
    for (auto lci = Iter(cursor_lc); is_close_char ? lci.IsBegin() : lci.IsEnd(); is_close_char ? --lci : ++lci) {
        const char ch_inner = lci;
        if (ch_inner == ch) ++match_count;
        else if (ch_inner == other_ch && (match_count == 0 || --match_count == 0)) return Cursor{cursor_lc, *lci};
    }

    return {};
}

void TextEditor::ChangeCurrentLinesIndentation(bool increase) {
    UndoRecord u{Cursors};
    for (const auto &c : reverse_view(Cursors)) {
        for (uint li = c.Min().L; li <= c.Max().L; ++li) {
            // Check if selection ends at line start.
            if (c.IsRange() && c.Max() == LineChar{li, 0}) continue;

            const auto &line = Lines[li];
            if (increase) {
                if (!line.empty()) {
                    const LineChar start{li, 0}, end = InsertTextAt(start, "\t");
                    u.Operations.emplace_back("\t", start, end, UndoOperationType::Add);
                }
            } else {
                int ci = int(GetCharIndex(line, NumTabSpaces)) - 1;
                while (ci > -1 && isblank(line[ci])) --ci;
                const bool only_space_chars_found = ci == -1;
                if (only_space_chars_found) {
                    const LineChar start{li, 0}, end = {li, GetCharIndex(line, NumTabSpaces)};
                    const auto &text = GetText(start, end);
                    u.Operations.emplace_back(text, start, end, UndoOperationType::Delete);
                    DeleteRange(start, end);
                }
            }
        }
    }

    AddUndo(u);
}

void TextEditor::MoveCurrentLines(bool up) {
    UndoRecord u{Cursors};
    std::set<uint> affected_lines;
    uint min_li = std::numeric_limits<uint>::max(), max_li = std::numeric_limits<uint>::min();
    for (const auto &c : Cursors) {
        for (uint li = c.Min().L; li <= c.Max().L; ++li) {
            // Check if selection ends at line start.
            if (c.IsRange() && c.Max() == LineChar{li, 0}) continue;

            affected_lines.insert(li);
            min_li = std::min(li, min_li);
            max_li = std::max(li, max_li);
        }
    }
    if ((up && min_li == 0) || (!up && max_li == Lines.size() - 1)) return; // Can't move up/down anymore.

    const uint start_li = min_li - (up ? 1 : 0), end_li = max_li + (up ? 0 : 1);
    const LineChar start{start_li, 0};
    AddUndoOp(u, UndoOperationType::Delete, start, LineMaxLC(end_li));
    if (up) {
        for (const uint li : affected_lines) {
            std::swap(Lines[li - 1], Lines[li]);
            std::swap(PaletteIndices[li - 1], PaletteIndices[li]);
        }
    } else {
        for (auto it = affected_lines.rbegin(); it != affected_lines.rend(); ++it) {
            std::swap(Lines[*it + 1], Lines[*it]);
            std::swap(PaletteIndices[*it + 1], PaletteIndices[*it]);
        }
    }
    Cursors.MoveLines(*this, up ? -1 : 1);

    // No need to set CursorPositionChanged as cursors will remain sorted.
    AddUndoOp(u, UndoOperationType::Add, start, LineMaxLC(end_li));
    AddUndo(u);
}

static bool Equals(const auto &c1, const auto &c2, std::size_t c2_offset = 0) {
    if (c2.size() + c2_offset < c1.size()) return false;

    const auto begin = c2.cbegin() + c2_offset;
    return std::ranges::equal(c1, subrange(begin, begin + c1.size()));
}

void TextEditor::ToggleLineComment() {
    const string &comment = GetLanguage().SingleLineComment;
    if (comment.empty()) return;

    static const auto FindFirstNonSpace = [](const LineT &line) {
        return std::distance(line.begin(), std::ranges::find_if_not(line, isblank));
    };

    std::unordered_set<uint> affected_lines;
    for (const auto &c : Cursors) {
        for (uint li = c.Min().L; li <= c.Max().L; ++li) {
            if (!(c.IsRange() && c.Max() == LineChar{li, 0}) && !Lines[li].empty()) affected_lines.insert(li);
        }
    }

    UndoRecord u{Cursors};
    const bool should_add_comment = any_of(affected_lines, [&](uint li) {
        return !Equals(comment, Lines[li], FindFirstNonSpace(Lines[li]));
    });
    for (uint li : affected_lines) {
        if (should_add_comment) {
            const LineChar line_start{li, 0}, insertion_end = InsertTextAt(line_start, comment + ' ');
            u.Operations.emplace_back(comment + ' ', line_start, insertion_end, UndoOperationType::Add);
        } else {
            const auto &line = Lines[li];
            const uint ci = FindFirstNonSpace(line);
            uint comment_ci = ci + comment.length();
            if (comment_ci < line.size() && line[comment_ci] == ' ') ++comment_ci;

            const LineChar start = {li, ci}, end = {li, comment_ci};
            AddUndoOp(u, UndoOperationType::Delete, start, end);
            DeleteRange(start, end);
        }
    }
    AddUndo(u);
}

void TextEditor::RemoveCurrentLines() {
    UndoRecord u{Cursors};
    for (auto &c : reverse_view(Cursors)) DeleteSelection(c, u);
    Cursors.MoveStart();
    OnCursorPositionChanged(); // Might combine cursors.

    for (auto &c : reverse_view(Cursors)) {
        const uint li = c.Line();
        LineChar to_delete_start, to_delete_end;
        if (Lines.size() > li + 1) { // Next line exists.
            to_delete_start = {li, 0};
            to_delete_end = {li + 1, 0};
            c.Set({li, 0});
        } else if (li > 0) { // Previous line exists.
            to_delete_start = LineMaxLC(li - 1);
            to_delete_end = LineMaxLC(li);
            c.Set({li - 1, 0});
        } else {
            to_delete_start = {li, 0};
            to_delete_end = LineMaxLC(li);
            c.Set({li, 0});
        }

        AddUndoOp(u, UndoOperationType::Delete, to_delete_start, to_delete_end);
        DeleteRange(to_delete_start, to_delete_end);
    }

    AddUndo(u);
}

TextEditor::Coords TextEditor::ScreenPosToCoords(const ImVec2 &screen_pos, bool *is_over_li) const {
    static constexpr float PosToCoordsColumnOffset = 0.33;

    const auto local = ImVec2{screen_pos.x + 3.0f, screen_pos.y} - ImGui::GetCursorScreenPos();
    if (is_over_li != nullptr) *is_over_li = local.x < TextStart;

    Coords coords{
        std::min(uint(std::max(0.f, floor(local.y / CharAdvance.y))), uint(Lines.size()) - 1),
        uint(std::max(0.f, floor((local.x - TextStart + PosToCoordsColumnOffset * CharAdvance.x) / CharAdvance.x)))
    };
    // Check if the coord is in the middle of a tab character.
    if (coords.L < Lines.size()) {
        const auto &line = Lines[coords.L];
        const uint ci = GetCharIndex(line, coords.C);
        if (ci < line.size() && line[ci] == '\t') coords.C = GetCharColumn(line, ci);
    }

    return {coords.L, GetLineMaxColumn(coords.L, coords.C)};
}

TextEditor::LineChar TextEditor::FindWordBoundary(LineChar from, bool is_start) const {
    if (from.L >= Lines.size()) return from;

    const auto &line = Lines[from.L];
    uint ci = from.C;
    if (ci >= line.size()) return from;

    const char init_char = line[ci];
    const bool init_is_word_char = IsWordChar(init_char), init_is_space = isspace(init_char);
    for (; is_start ? ci > 0 : ci < line.size(); is_start ? --ci : ++ci) {
        if (ci == line.size() ||
            (init_is_space && !isspace(line[ci])) ||
            (init_is_word_char && !IsWordChar(line[ci])) ||
            (!init_is_word_char && !init_is_space && init_char != line[ci])) {
            if (is_start) ++ci; // Undo one left step before returning line/char.
            break;
        }
    }
    return {from.L, ci};
}

uint TextEditor::GetCharIndex(const LineT &line, uint column) const {
    uint ci = 0;
    for (uint column_i = 0; ci < line.size() && column_i < column;) MoveCharIndexAndColumn(line, ci, column_i);
    return ci;
}

uint TextEditor::GetCharColumn(const LineT &line, uint ci) const {
    uint column = 0;
    for (uint ci_i = 0; ci_i < ci && ci_i < line.size();) MoveCharIndexAndColumn(line, ci_i, column);
    return column;
}

uint TextEditor::GetFirstVisibleCharIndex(uint li) const {
    if (li >= Lines.size()) return 0;

    const auto &line = Lines[li];
    uint ci = 0, column = 0;
    while (column < FirstVisibleCoords.C && ci < line.size()) MoveCharIndexAndColumn(line, ci, column);
    return column > FirstVisibleCoords.C && ci > 0 ? ci - 1 : ci;
}

uint TextEditor::GetLineMaxColumn(uint li) const {
    if (li >= Lines.size()) return 0;

    const auto &line = Lines[li];
    uint column = 0;
    for (uint ci = 0; ci < line.size();) MoveCharIndexAndColumn(line, ci, column);
    return column;
}
uint TextEditor::GetLineMaxColumn(uint li, uint limit) const {
    if (li >= Lines.size()) return 0;

    const auto &line = Lines[li];
    uint column = 0;
    for (uint ci = 0; ci < line.size() && column < limit;) MoveCharIndexAndColumn(line, ci, column);
    return column;
}

void TextEditor::AddOrRemoveGlyphs(LineChar lc, std::span<const char> glyphs, bool is_add) {
    if (glyphs.empty()) return;

    auto cursors_to_right = Cursors | filter([&lc](const auto &c) { return !c.IsRange() && c.IsRightOf(lc); });
    for (auto &c : cursors_to_right) c.Set({c.Line(), uint(c.CharIndex() + (is_add ? glyphs.size() : -glyphs.size()))});

    auto &line = Lines[lc.L];
    auto &palette_line = PaletteIndices[lc.L];
    if (is_add) {
        line.insert(line.begin() + lc.C, glyphs.begin(), glyphs.end());
        palette_line.insert(palette_line.begin() + lc.C, glyphs.size(), PaletteIndex::Default);
    } else {
        line.erase(glyphs.begin(), glyphs.end());
        palette_line.erase(palette_line.begin() + lc.C, palette_line.begin() + lc.C + glyphs.size());
    }
}

// todo: all add/remove glyphs calls are made here in `InsertTextAt` and `DeleteRange`.
//   We can make multi-line insertion much more efficient by handling all cursor bookkeeping once instead of once per add/remove-glyphs call.
TextEditor::LineChar TextEditor::InsertTextAt(LineChar start, const string &text) {
    const uint start_byte = ToByteIndex(start);
    if (const uint num_newlines = std::ranges::count(text, '\n'); num_newlines > 0) {
        Lines.insert(Lines.begin() + start.L + 1, num_newlines, LineT{});
        PaletteIndices.insert(PaletteIndices.begin() + start.L + 1, num_newlines, std::vector<PaletteIndex>{});
        auto cursors_below = Cursors | filter([&](const auto &c) { return c.Line() >= start.L; });
        for (auto &c : cursors_below) c.Set({c.Line() + num_newlines, c.CharIndex()});
    }

    LineChar end = start;
    for (auto it = text.begin(); it != text.end(); ++it) {
        const char ch = *it;
        if (ch == '\r') continue;

        if (ch == '\n') {
            const auto &line = Lines[end.L];
            AddGlyphs({end.L + 1, 0}, {line.cbegin() + end.C, line.cend()});
            RemoveGlyphs(end, {Lines[end.L].cbegin() + end.C, Lines[end.L].cend()}); // from start to end of line
            ++end.L;
            end.C = 0;
        } else {
            // Add all characters up to the next newline.
            const string chars = std::string(it, std::find(it, text.end(), '\n'));
            AddGlyphs(end, chars);
            std::advance(it, chars.size() - 1);
            end.C += chars.size();
        }
    }

    OnTextChanged(start_byte, start_byte, ToByteIndex(end));
    return end;
}

void TextEditor::DeleteRange(LineChar start, LineChar end, const Cursor *exclude_cursor) {
    if (end <= start) return;

    const uint start_byte = ToByteIndex(start), old_end_byte = ToByteIndex(end);

    if (start.L == end.L) {
        RemoveGlyphs(start, {Lines[start.L].cbegin() + start.C, Lines[start.L].cbegin() + end.C});
    } else {
        RemoveGlyphs(start, {Lines[start.L].cbegin() + start.C, Lines[start.L].cend()}); // from start to end of line
        RemoveGlyphs({end.L, 0}, {Lines[end.L].cbegin(), Lines[end.L].cbegin() + end.C}); // from beginning of line to end
        AddGlyphs({start.L, uint(Lines[start.L].size())}, Lines[end.L]);

        // Move up all cursors after the last removed line.
        if (const uint line_count = end.L - start.L; line_count > 0) {
            auto cursors_below = Cursors | filter([&](const auto &c) { return (!exclude_cursor || c != *exclude_cursor) && c.Line() >= end.L; });
            for (auto &c : cursors_below) c.MoveLines(*this, -line_count, false);
        }

        Lines.erase(Lines.begin() + start.L + 1, Lines.begin() + end.L + 1);
        PaletteIndices.erase(PaletteIndices.begin() + start.L + 1, PaletteIndices.begin() + end.L + 1);
    }
    OnTextChanged(start_byte, old_end_byte, start_byte);
}

void TextEditor::DeleteSelection(Cursor &c, UndoRecord &record) {
    if (!c.IsRange()) return;

    const auto start = c.Min(), end = c.Max();
    AddUndoOp(record, UndoOperationType::Delete, start, end);
    // Exclude the cursor whose selection is currently being deleted from having its position changed in `DeleteRange`.
    DeleteRange(start, end, &c);
    c.Set(start);
}

static bool IsPressed(ImGuiKey key) {
    const auto key_index = ImGui::GetKeyIndex(key);
    const auto window_id = ImGui::GetCurrentWindowRead()->ID;
    ImGui::SetKeyOwner(key_index, window_id); // Prevent app from handling this key press.
    return ImGui::IsKeyPressed(key_index, window_id);
}

void TextEditor::HandleKeyboardInputs(bool is_parent_focused) {
    if (!ImGui::IsWindowFocused() && !is_parent_focused) return;

    if (ImGui::IsWindowHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    auto &io = ImGui::GetIO();
    io.WantCaptureKeyboard = io.WantTextInput = true;

    const bool
        is_osx = io.ConfigMacOSXBehaviors,
        alt = io.KeyAlt, ctrl = io.KeyCtrl, shift = io.KeyShift, super = io.KeySuper,
        is_shortcut = (is_osx ? (super && !ctrl) : (ctrl && !super)) && !alt && !shift,
        is_shift_shortcut = (is_osx ? (super && !ctrl) : (ctrl && !super)) && shift && !alt,
        is_wordmove_key = is_osx ? alt : ctrl,
        is_alt_only = alt && !ctrl && !shift && !super,
        is_ctrl_only = ctrl && !alt && !shift && !super,
        is_shift_only = shift && !alt && !ctrl && !super;

    if (!ReadOnly && is_shortcut && IsPressed(ImGuiKey_Z))
        Undo();
    else if (!ReadOnly && is_alt_only && IsPressed(ImGuiKey_Backspace))
        Undo();
    else if (!ReadOnly && is_shortcut && IsPressed(ImGuiKey_Y))
        Redo();
    else if (!ReadOnly && is_shift_shortcut && IsPressed(ImGuiKey_Z))
        Redo();
    else if (!alt && !ctrl && !super && IsPressed(ImGuiKey_UpArrow))
        Cursors.MoveLines(*this, -1, shift);
    else if (!alt && !ctrl && !super && IsPressed(ImGuiKey_DownArrow))
        Cursors.MoveLines(*this, 1, shift);
    else if ((is_osx ? !ctrl : !alt) && !super && IsPressed(ImGuiKey_LeftArrow))
        Cursors.MoveChar(*this, false, shift, is_wordmove_key);
    else if ((is_osx ? !ctrl : !alt) && !super && IsPressed(ImGuiKey_RightArrow))
        Cursors.MoveChar(*this, true, shift, is_wordmove_key);
    else if (!alt && !ctrl && !super && IsPressed(ImGuiKey_PageUp))
        Cursors.MoveLines(*this, -(VisibleLineCount - 2), shift);
    else if (!alt && !ctrl && !super && IsPressed(ImGuiKey_PageDown))
        Cursors.MoveLines(*this, VisibleLineCount - 2, shift);
    else if (ctrl && !alt && !super && IsPressed(ImGuiKey_Home))
        Cursors.MoveTop(shift);
    else if (ctrl && !alt && !super && IsPressed(ImGuiKey_End))
        Cursors.MoveBottom(*this, shift);
    else if (!alt && !ctrl && !super && IsPressed(ImGuiKey_Home))
        Cursors.MoveStart(shift);
    else if (!alt && !ctrl && !super && IsPressed(ImGuiKey_End))
        Cursors.MoveEnd(*this, shift);
    else if (!ReadOnly && !alt && !shift && !super && IsPressed(ImGuiKey_Delete))
        Delete(ctrl);
    else if (!ReadOnly && !alt && !shift && !super && IsPressed(ImGuiKey_Backspace))
        Backspace(ctrl);
    else if (!ReadOnly && !alt && ctrl && shift && !super && IsPressed(ImGuiKey_K))
        RemoveCurrentLines();
    else if (!ReadOnly && !alt && ctrl && !shift && !super && IsPressed(ImGuiKey_LeftBracket))
        ChangeCurrentLinesIndentation(false);
    else if (!ReadOnly && !alt && ctrl && !shift && !super && IsPressed(ImGuiKey_RightBracket))
        ChangeCurrentLinesIndentation(true);
    else if (!ReadOnly && !alt && ctrl && shift && !super && IsPressed(ImGuiKey_UpArrow))
        MoveCurrentLines(true);
    else if (!ReadOnly && !alt && ctrl && shift && !super && IsPressed(ImGuiKey_DownArrow))
        MoveCurrentLines(false);
    else if (!ReadOnly && !alt && ctrl && !shift && !super && IsPressed(ImGuiKey_Slash))
        ToggleLineComment();
    else if (!alt && !ctrl && !shift && !super && IsPressed(ImGuiKey_Insert))
        Overwrite ^= true;
    else if (is_ctrl_only && IsPressed(ImGuiKey_Insert))
        Copy();
    else if (is_shortcut && IsPressed(ImGuiKey_C))
        Copy();
    else if (!ReadOnly && is_shift_only && IsPressed(ImGuiKey_Insert))
        Paste();
    else if (!ReadOnly && is_shortcut && IsPressed(ImGuiKey_V))
        Paste();
    else if ((is_shortcut && IsPressed(ImGuiKey_X)) || (is_shift_only && IsPressed(ImGuiKey_Delete)))
        if (ReadOnly) Copy();
        else Cut();
    else if (is_shortcut && IsPressed(ImGuiKey_A))
        SelectAll();
    else if (is_shortcut && IsPressed(ImGuiKey_D))
        AddCursorForNextOccurrence();
    else if (!ReadOnly && !alt && !ctrl && !shift && !super && (IsPressed(ImGuiKey_Enter) || IsPressed(ImGuiKey_KeypadEnter)))
        EnterChar('\n', false);
    else if (!ReadOnly && !alt && !ctrl && !super && IsPressed(ImGuiKey_Tab))
        EnterChar('\t', shift);
    if (!ReadOnly && !io.InputQueueCharacters.empty() && ctrl == alt && !super) {
        for (const auto ch : io.InputQueueCharacters) {
            if (ch != 0 && (ch == '\n' || ch >= 32)) EnterChar(ch, shift);
        }
        io.InputQueueCharacters.resize(0);
    }
}

static float Distance(const ImVec2 &a, const ImVec2 &b) {
    const float x = a.x - b.x, y = a.y - b.y;
    return sqrt(x * x + y * y);
}

void TextEditor::HandleMouseInputs() {
    auto &io = ImGui::GetIO();
    const auto shift = io.KeyShift;
    const auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    const auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

    // Pan with middle mouse button
    Panning &= ImGui::IsMouseDown(2);
    if (Panning && ImGui::IsMouseDragging(2)) {
        ImVec2 scroll{ImGui::GetScrollX(), ImGui::GetScrollY()};
        ImVec2 mouse_pos = ImGui::GetMouseDragDelta(2);
        ImVec2 mouse_delta = mouse_pos - LastMousePos;
        ImGui::SetScrollY(scroll.y - mouse_delta.y);
        ImGui::SetScrollX(scroll.x - mouse_delta.x);
        LastMousePos = mouse_pos;
    }

    // Mouse left button dragging (=> update selection)
    IsDraggingSelection &= ImGui::IsMouseDown(0);
    if (IsDraggingSelection && ImGui::IsMouseDragging(0)) {
        io.WantCaptureMouse = true;
        Cursors.GetLastAdded().SetEnd(ScreenPosToLC(ImGui::GetMousePos()));
    }

    if (ImGui::IsWindowHovered()) {
        const auto is_click = ImGui::IsMouseClicked(0);
        if (!shift && !alt) {
            if (is_click) IsDraggingSelection = true;

            // Pan with middle mouse button
            if (ImGui::IsMouseClicked(2)) {
                Panning = true;
                LastMousePos = ImGui::GetMouseDragDelta(2);
            }

            const bool is_double_click = ImGui::IsMouseDoubleClicked(0);
            const auto t = ImGui::GetTime();
            const bool is_triple_click = is_click && !is_double_click && (LastClickTime != -1.0f && (t - LastClickTime) < io.MouseDoubleClickTime && Distance(io.MousePos, LastClickPos) < 0.01f);
            if (is_triple_click) {
                if (ctrl) Cursors.Add();
                else Cursors.Reset();

                const auto cursor_lc = ScreenPosToLC(ImGui::GetMousePos());
                SetSelection(
                    {cursor_lc.L, 0},
                    cursor_lc.L < Lines.size() - 1 ? LineChar{cursor_lc.L + 1, 0} : LineMaxLC(cursor_lc.L),
                    Cursors.back()
                );

                LastClickTime = -1.0f;
            } else if (is_double_click) {
                if (ctrl) Cursors.Add();
                else Cursors.Reset();

                const auto cursor_lc = ScreenPosToLC(ImGui::GetMousePos());
                SetSelection(FindWordBoundary(cursor_lc, true), FindWordBoundary(cursor_lc, false), Cursors.back());

                LastClickTime = float(ImGui::GetTime());
                LastClickPos = io.MousePos;
            } else if (is_click) {
                if (ctrl) Cursors.Add();
                else Cursors.Reset();

                bool is_over_li;
                const auto cursor_lc = ScreenPosToLC(ImGui::GetMousePos(), &is_over_li);
                if (is_over_li) {
                    SetSelection(
                        {cursor_lc.L, 0},
                        cursor_lc.L < Lines.size() - 1 ? LineChar{cursor_lc.L + 1, 0} : LineMaxLC(cursor_lc.L),
                        Cursors.back()
                    );
                } else {
                    Cursors.GetLastAdded().Set(cursor_lc);
                }

                LastClickTime = float(ImGui::GetTime());
                LastClickPos = io.MousePos;
            } else if (ImGui::IsMouseReleased(0)) {
                Cursors.SortAndMerge();
            }
        } else if (shift && is_click) {
            Cursors.back().SetEnd(ScreenPosToLC(ImGui::GetMousePos()));
        }
    }
}

void TextEditor::UpdateViewVariables(float scroll_x, float scroll_y) {
    static constexpr float ImGuiScrollbarWidth = 14;

    ContentHeight = ImGui::GetWindowHeight() - (IsHorizontalScrollbarVisible() ? ImGuiScrollbarWidth : 0.0f);
    ContentWidth = ImGui::GetWindowWidth() - (IsVerticalScrollbarVisible() ? ImGuiScrollbarWidth : 0.0f);

    VisibleLineCount = std::max(uint(ceil(ContentHeight / CharAdvance.y)), 0u);
    VisibleColumnCount = std::max(uint(ceil((ContentWidth - std::max(TextStart - scroll_x, 0.0f)) / CharAdvance.x)), 0u);
    FirstVisibleCoords = {uint(scroll_y / CharAdvance.y), uint(std::max(scroll_x - TextStart, 0.0f) / CharAdvance.x)};
    LastVisibleCoords = {uint((ContentHeight + scroll_y) / CharAdvance.y), uint((ContentWidth + scroll_x - TextStart) / CharAdvance.x)};
}

void TextEditor::Render(bool is_parent_focused) {
    /* Compute CharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
    const float font_width = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
    const float font_height = ImGui::GetTextLineHeightWithSpacing();
    CharAdvance = {font_width, font_height * LineSpacing};

    // Deduce `TextStart` by evaluating `Lines` size plus two spaces as text width.
    TextStart = LeftMargin;
    static char li_buffer[16];
    if (ShowLineNumbers) {
        snprintf(li_buffer, 16, " %lu ", Lines.size());
        TextStart += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, li_buffer, nullptr, nullptr).x;
    }
    const ImVec2 cursor_screen_pos = ImGui::GetCursorScreenPos();
    ScrollX = ImGui::GetScrollX();
    ScrollY = ImGui::GetScrollY();
    UpdateViewVariables(ScrollX, ScrollY);

    uint max_column_limited = 0;
    auto dl = ImGui::GetWindowDrawList();
    const float font_size = ImGui::GetFontSize();
    const float space_size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, -1.0f, " ").x;

    for (uint li = FirstVisibleCoords.L; li <= LastVisibleCoords.L && li < Lines.size(); ++li) {
        const auto &line = Lines[li];
        const uint line_max_column_limited = GetLineMaxColumn(li, LastVisibleCoords.C);
        max_column_limited = std::max(line_max_column_limited, max_column_limited);

        const ImVec2 line_start_screen_pos{cursor_screen_pos.x, cursor_screen_pos.y + li * CharAdvance.y};
        const float text_screen_pos_x = line_start_screen_pos.x + TextStart;
        const Coords line_start_coord{li, 0}, line_end_coord{li, line_max_column_limited};
        // Draw current line selection
        for (const auto &c : Cursors) {
            const auto selection_start = ToCoords(c.Min()), selection_end = ToCoords(c.Max());
            float rect_start = -1.0f, rect_end = -1.0f;
            if (selection_start <= line_end_coord)
                rect_start = selection_start > line_start_coord ? selection_start.C * CharAdvance.x : 0.0f;
            if (selection_end > line_start_coord)
                rect_end = (selection_end < line_end_coord ? selection_end.C : line_end_coord.C) * CharAdvance.x;
            if (selection_end.L > li || (selection_end.L == li && selection_end > line_end_coord))
                rect_end += CharAdvance.x;

            if (rect_start != -1 && rect_end != -1 && rect_start < rect_end) {
                dl->AddRectFilled(
                    {text_screen_pos_x + rect_start, line_start_screen_pos.y},
                    {text_screen_pos_x + rect_end, line_start_screen_pos.y + CharAdvance.y},
                    GetColor(PaletteIndex::Selection)
                );
            }
        }

        // Draw line number (right aligned)
        if (ShowLineNumbers) {
            snprintf(li_buffer, 16, "%d  ", li + 1);
            const float line_num_width = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, -1.0f, li_buffer).x;
            dl->AddText({text_screen_pos_x - line_num_width, line_start_screen_pos.y}, GetColor(PaletteIndex::LineNumber), li_buffer);
        }

        // Render cursors
        if (ImGui::IsWindowFocused() || is_parent_focused) {
            for (const auto &c : filter(Cursors, [li](const auto &c) { return c.Line() == li; })) {
                const uint ci = c.CharIndex();
                const float cx = GetCharColumn(line, ci) * CharAdvance.x;
                float width = 1.0f;
                if (Overwrite && ci < line.size()) {
                    width = line[ci] == '\t' ? (1.f + std::floor((1.f + cx) / (NumTabSpaces * space_size))) * (NumTabSpaces * space_size) - cx : CharAdvance.x;
                }
                dl->AddRectFilled(
                    {text_screen_pos_x + cx, line_start_screen_pos.y},
                    {text_screen_pos_x + cx + width, line_start_screen_pos.y + CharAdvance.y},
                    GetColor(PaletteIndex::Cursor)
                );
            }
        }

        // Render colorized text
        for (uint ci = GetFirstVisibleCharIndex(li), column = FirstVisibleCoords.C; ci < line.size() && column <= LastVisibleCoords.C;) {
            const auto lc = LineChar{li, ci};
            const char ch = line[lc.C];
            const ImVec2 glyph_pos = line_start_screen_pos + ImVec2{TextStart + column * CharAdvance.x, 0};
            if (ch == '\t') {
                if (ShowWhitespaces) {
                    const ImVec2 p1{glyph_pos + ImVec2{CharAdvance.x * 0.3f, font_height * 0.5f}};
                    const ImVec2 p2{
                        glyph_pos.x + (ShortTabs ? (NumTabSpacesAtColumn(column) * CharAdvance.x - CharAdvance.x * 0.3f) : CharAdvance.x),
                        p1.y
                    };
                    const float gap = ImGui::GetFontSize() * (ShortTabs ? 0.16f : 0.2f);
                    const ImVec2 p3{p2.x - gap, p1.y - gap}, p4{p2.x - gap, p1.y + gap};
                    const ImU32 color = GetColor(PaletteIndex::ControlCharacter);
                    dl->AddLine(p1, p2, color);
                    dl->AddLine(p2, p3, color);
                    dl->AddLine(p2, p4, color);
                }
            } else if (ch == ' ') {
                if (ShowWhitespaces) {
                    dl->AddCircleFilled(
                        glyph_pos + ImVec2{space_size, ImGui::GetFontSize()} * 0.5f,
                        1.5f, GetColor(PaletteIndex::ControlCharacter), 4
                    );
                }
            } else {
                const uint seq_length = UTF8CharLength(ch);
                if (seq_length == 1 && MatchingBrackets && (MatchingBrackets->GetStart() == lc || MatchingBrackets->GetEnd() == lc)) {
                    const ImVec2 top_left{glyph_pos + ImVec2{0, font_height + 1.0f}};
                    dl->AddRectFilled(top_left, top_left + ImVec2{CharAdvance.x, 1.0f}, GetColor(PaletteIndex::Cursor));
                }
                const string text = subrange(line.begin() + ci, line.begin() + ci + seq_length) | ranges::to<string>();
                dl->AddText(glyph_pos, GetColor(lc), text.c_str());
            }
            MoveCharIndexAndColumn(line, ci, column);
        }
    }
    CurrentSpaceHeight = (Lines.size() + std::min(VisibleLineCount - 1, uint(Lines.size()))) * CharAdvance.y;
    CurrentSpaceWidth = std::max((max_column_limited + std::min(VisibleColumnCount - 1, max_column_limited)) * CharAdvance.x, CurrentSpaceWidth);

    ImGui::SetCursorPos({0, 0});
    ImGui::Dummy({CurrentSpaceWidth, CurrentSpaceHeight});
    if (const auto edited_coord_range = Cursors.GetEditedCoordRange(*this)) {
        // First pass for end and second pass for start.
        for (uint i = 0; i < 1; ++i) {
            if (i) UpdateViewVariables(ScrollX, ScrollY); // Second pass depends on changes made in first pass.
            const auto target = i > 0 ? edited_coord_range->first : edited_coord_range->second;
            if (target.L <= FirstVisibleCoords.L) {
                float scroll = std::max(0.0f, (target.L - 0.5f) * CharAdvance.y);
                if (scroll < ScrollY) ImGui::SetScrollY(scroll);
            }
            if (target.L >= LastVisibleCoords.L) {
                float scroll = std::max(0.0f, (target.L + 1.5f) * CharAdvance.y - ContentHeight);
                if (scroll > ScrollY) ImGui::SetScrollY(scroll);
            }
            if (target.C <= FirstVisibleCoords.C) {
                if (target.C >= LastVisibleCoords.C) {
                    float scroll = std::max(0.0f, TextStart + (target.C + 0.5f) * CharAdvance.x - ContentWidth);
                    if (scroll > ScrollX) ImGui::SetScrollX(ScrollX = scroll);
                } else {
                    float scroll = std::max(0.0f, TextStart + (target.C - 0.5f) * CharAdvance.x);
                    if (scroll < ScrollX) ImGui::SetScrollX(ScrollX = scroll);
                }
            }
        }
        Cursors.ClearEdited();
    }
    if (ScrollToTop) {
        ScrollToTop = false;
        ImGui::SetScrollY(0);
    }
    if (SetViewAtLineI > -1) {
        float scroll;
        switch (SetViewAtLineMode) {
            default:
            case SetViewAtLineMode::FirstVisibleLine:
                scroll = std::max(0.0f, SetViewAtLineI * CharAdvance.y);
                break;
            case SetViewAtLineMode::LastVisibleLine:
                scroll = std::max(0.0f, (float(SetViewAtLineI) - float((LastVisibleCoords - FirstVisibleCoords).L)) * CharAdvance.y);
                break;
            case SetViewAtLineMode::Centered:
                scroll = std::max(0.0f, (float(SetViewAtLineI) - float((LastVisibleCoords - FirstVisibleCoords).L) * 0.5f) * CharAdvance.y);
                break;
        }
        ImGui::SetScrollY(scroll);
        SetViewAtLineI = -1;
    }
}

uint TextEditor::ToByteIndex(LineChar lc) const {
    return ranges::accumulate(subrange(Lines.begin(), Lines.begin() + lc.L), 0u, [](uint sum, const auto &line) { return sum + line.size() + 1; }) + lc.C;
}

void TextEditor::OnTextChanged(uint start_byte, uint old_end_byte, uint new_end_byte) {
    if (Tree) {
        TSInputEdit edit;
        // Seems we only need to provide the bytes (without points), despite the documentation: https://github.com/tree-sitter/tree-sitter/issues/445
        edit.start_byte = start_byte;
        edit.old_end_byte = old_end_byte;
        edit.new_end_byte = new_end_byte;
        ts_tree_edit(Tree, &edit);
    }
    TextChanged = true;
}

void TextEditor::OnCursorPositionChanged() {
    const auto &c = Cursors[0];
    MatchingBrackets = Cursors.size() == 1 ? FindMatchingBrackets(c) : std::nullopt;

    if (!IsDraggingSelection) Cursors.SortAndMerge();
}

void TextEditor::AddUndo(UndoRecord &record) {
    if (record.Operations.empty()) return;

    record.After = Cursors;
    UndoBuffer.resize(UndoIndex + 1);
    UndoBuffer.back() = record;
    ++UndoIndex;
}

const TextEditor::PaletteT TextEditor::DarkPalette = {{
    0xffe4dfdc, // Default
    0xff756ce0, // Keyword
    0xff7bc0e5, // Number
    0xff79c398, // String
    0xff70a0e0, // Char
    0xff84736a, // Punctuation
    0xff408080, // Preprocessor
    0xffefaf61, // Operator
    0xffe4dfdc, // Identifier
    0xffdd78c6, // Type
    0xffa29636, // Comment

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

const TextEditor::PaletteT TextEditor::MarianaPalette = {{
    0xffffffff, // Default
    0xffc695c6, // Keyword
    0xff58aef9, // Number
    0xff94c799, // String
    0xff70a0e0, // Char
    0xffb4b45f, // Punctuation
    0xff408080, // Preprocessor
    0xff9bc64d, // Operator
    0xffffffff, // Identifier
    0xffffa0e0, // Type

    0xffb9aca6, // Comment
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

const TextEditor::PaletteT TextEditor::LightPalette = {{
    0xff404040, // Default
    0xffff0c06, // Keyword
    0xff008000, // Number
    0xff2020a0, // String
    0xff304070, // Char
    0xff000000, // Punctuation
    0xff406060, // Preprocessor
    0xff606010, // Operator
    0xff404040, // Identifier
    0xffc040a0, // Type
    0xff205020, // Comment

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

const TextEditor::PaletteT TextEditor::RetroBluePalette = {{
    0xff00ffff, // Default
    0xffffff00, // Keyword
    0xff00ff00, // Number
    0xff808000, // String
    0xff808000, // Char
    0xffffffff, // Punctuation
    0xff008000, // Preprocessor
    0xffffffff, // Operator
    0xff00ffff, // Identifier
    0xffff00ff, // Type

    0xff808080, // Comment
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
