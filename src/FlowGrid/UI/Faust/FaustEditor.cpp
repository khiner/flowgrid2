#include "FaustEditor.h"
#include "../../AppContext.h"

#include "zep/editor.h"
#include "zep/mode_repl.h"
#include "zep/mode_standard.h"
#include "zep/mode_vim.h"
#include "zep/regress.h"
#include "zep/tab_window.h"

using namespace fg;
using namespace ImGui;
using namespace Zep;

inline NVec2f toNVec2f(const ImVec2 &im) { return {im.x, im.y}; }
inline ImVec2 toImVec2(const NVec2f &im) { return {im.x, im.y}; }

struct ZepFont_ImGui : ZepFont {
    ZepFont_ImGui(ZepDisplay &display, ImFont *font, float heightRatio) : ZepFont(display), font(font) {
        SetPixelHeight(int(font->FontSize * heightRatio * GetIO().FontGlobalScale));
    }

    void SetPixelHeight(int pixelHeight) override {
        InvalidateCharCache();
        this->pixelHeight = pixelHeight;
    }

    NVec2f GetTextSize(const uint8_t *begin, const uint8_t *end) const override {
        // This is the code from ImGui internals; we can't call GetTextSize, because it doesn't return the correct 'advance' formula, which we
        // need as we draw one character at a time...
        ImVec2 text_size = font->CalcTextSizeA(float(pixelHeight), FLT_MAX, FLT_MAX, (const char *)begin, (const char *)end, nullptr);
        if (text_size.x == 0.0) {
            // Make invalid characters a default fixed_size
            const char default_char = 'A';
            text_size = font->CalcTextSizeA(float(pixelHeight), FLT_MAX, FLT_MAX, &default_char, (&default_char + 1), nullptr);
        }

        return toNVec2f(text_size);
    }

    ImFont *font;
};

static ImU32 GetStyleModulatedColor(const NVec4f &color) {
    return ToPackedABGR(NVec4f(color.x, color.y, color.z, color.w * GetStyle().Alpha));
}

struct ZepDisplay_ImGui : ZepDisplay {
    ZepDisplay_ImGui() : ZepDisplay() {}

    void DrawChars(ZepFont &font, const NVec2f &pos, const NVec4f &col, const uint8_t *text_begin, const uint8_t *text_end) const override {
        const auto &imFont = dynamic_cast<ZepFont_ImGui &>(font).font;
        auto *drawList = GetWindowDrawList();
        if (text_end == nullptr) {
            text_end = text_begin + strlen((const char *)text_begin);
        }
        const auto modulatedColor = GetStyleModulatedColor(col);
        if (clipRect.Width() == 0) {
            drawList->AddText(imFont, float(font.pixelHeight), toImVec2(pos), modulatedColor, (const char *)text_begin, (const char *)text_end);
        } else {
            drawList->PushClipRect(toImVec2(clipRect.topLeftPx), toImVec2(clipRect.bottomRightPx));
            drawList->AddText(imFont, float(font.pixelHeight), toImVec2(pos), modulatedColor, (const char *)text_begin, (const char *)text_end);
            drawList->PopClipRect();
        }
    }

    void DrawLine(const NVec2f &start, const NVec2f &end, const NVec4f &color, float width) const override {
        auto *drawList = GetWindowDrawList();
        const auto modulatedColor = GetStyleModulatedColor(color);

        // Background rect for numbers
        if (clipRect.Width() == 0) {
            drawList->AddLine(toImVec2(start), toImVec2(end), modulatedColor, width);
        } else {
            drawList->PushClipRect(toImVec2(clipRect.topLeftPx), toImVec2(clipRect.bottomRightPx));
            drawList->AddLine(toImVec2(start), toImVec2(end), modulatedColor, width);
            drawList->PopClipRect();
        }
    }

    void DrawRectFilled(const NRectf &rc, const NVec4f &color) const override {
        auto *drawList = GetWindowDrawList();
        const auto modulatedColor = GetStyleModulatedColor(color);
        // Background rect for numbers
        if (clipRect.Width() == 0) {
            drawList->AddRectFilled(toImVec2(rc.topLeftPx), toImVec2(rc.bottomRightPx), modulatedColor);
        } else {
            drawList->PushClipRect(toImVec2(clipRect.topLeftPx), toImVec2(clipRect.bottomRightPx));
            drawList->AddRectFilled(toImVec2(rc.topLeftPx), toImVec2(rc.bottomRightPx), modulatedColor);
            drawList->PopClipRect();
        }
    }

    void SetClipRect(const NRectf &rc) override { clipRect = rc; }

    ZepFont &GetFont(ZepTextType type) override { return *fonts[(int)type]; }

    NRectf clipRect;
};

struct ZepEditor_ImGui : ZepEditor {
    explicit ZepEditor_ImGui(const ZepPath &root, uint32_t flags = 0, ZepFileSystem *pFileSystem = nullptr)
        : ZepEditor(new ZepDisplay_ImGui(), root, flags, pFileSystem) {}

    bool sendImGuiKeyPressToBuffer(ImGuiKey key, ImGuiModFlags mod = ImGuiModFlags_None) {
        if (IsKeyPressed(key)) {
            GetActiveBuffer()->GetMode()->AddKeyPress(key, mod);
            return true;
        }
        return false;
    }

    void handleMouseEventAndHideFromImGui(size_t mouseButtonIndex, ZepMouseButton zepMouseButton, bool down) {
        auto &io = GetIO();
        if (down) {
            if (io.MouseClicked[mouseButtonIndex] && OnMouseDown(toNVec2f(io.MousePos), zepMouseButton)) io.MouseClicked[mouseButtonIndex] = false;
        }
        if (io.MouseReleased[mouseButtonIndex] && OnMouseUp(toNVec2f(io.MousePos), zepMouseButton)) io.MouseReleased[mouseButtonIndex] = false;
    }

    void HandleInput() override {
        auto &io = GetIO();
        bool handled = false;
        ImGuiModFlags mod = 0;

        static vector<ImGuiKey> F_KEYS = {
            ImGuiKey_F1,
            ImGuiKey_F2,
            ImGuiKey_F3,
            ImGuiKey_F4,
            ImGuiKey_F5,
            ImGuiKey_F6,
            ImGuiKey_F7,
            ImGuiKey_F8,
            ImGuiKey_F9,
            ImGuiKey_F10,
            ImGuiKey_F11,
            ImGuiKey_F12,
        };

        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
            OnMouseMove(toNVec2f(io.MousePos));
        }

        handleMouseEventAndHideFromImGui(0, ZepMouseButton::Left, true);
        handleMouseEventAndHideFromImGui(1, ZepMouseButton::Right, true);
        handleMouseEventAndHideFromImGui(0, ZepMouseButton::Left, false);
        handleMouseEventAndHideFromImGui(1, ZepMouseButton::Right, false);

        if (io.KeyCtrl) mod |= ImGuiModFlags_Ctrl;
        if (io.KeyShift) mod |= ImGuiModFlags_Shift;

        const auto *buffer = GetActiveBuffer();
        if (!buffer) return;

        // Check USB Keys
        for (auto &f_key : F_KEYS) {
            if (IsKeyPressed(f_key)) {
                buffer->GetMode()->AddKeyPress(f_key, mod);
                return;
            }
        }

        if (sendImGuiKeyPressToBuffer(ImGuiKey_Tab, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_Escape, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_Enter, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_Delete, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_Home, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_End, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_Backspace, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_RightArrow, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_LeftArrow, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_UpArrow, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_DownArrow, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_PageDown, mod)) return;
        if (sendImGuiKeyPressToBuffer(ImGuiKey_PageUp, mod)) return;

        if (io.KeyCtrl) {
            if (IsKeyPressed(ImGuiKey_1)) {
                SetGlobalMode(ZepMode_Standard::StaticName());
                handled = true;
            } else if (IsKeyPressed(ImGuiKey_2)) {
                SetGlobalMode(ZepMode_Vim::StaticName());
                handled = true;
            } else {
                for (ImGuiKey key = ImGuiKey_A; key < ImGuiKey_Z; key = ImGuiKey(key + 1)) {
                    if (IsKeyPressed(key)) {
                        buffer->GetMode()->AddKeyPress(key, mod);
                        handled = true;
                    }
                }

                if (IsKeyPressed(ImGuiKey_Space)) {
                    buffer->GetMode()->AddKeyPress(ImGuiKey_Space, mod);
                    handled = true;
                }
            }
        }

        if (!handled) {
            for (const ImWchar ch : io.InputQueueCharacters) {
                if (ch == '\r') continue; // Ignore '\r' - sometimes ImGui generates it!
                auto key = ImGuiKey(ch - 'a' + ImGuiKey_A);
                buffer->GetMode()->AddKeyPress(key, mod);
            }
        }
    }
};

bool IgnoreChanges = false; // Used to ignore zep messages triggered when programmatically setting the buffer.

struct ZepWrapper : ZepComponent, IZepReplProvider {
    explicit ZepWrapper(ZepEditor_ImGui &editor) : ZepComponent(editor) {
        ZepRegressExCommand::Register(editor);

        // Repl
        ZepReplExCommand::Register(editor, this);
        ZepReplEvaluateOuterCommand::Register(editor, this);
        ZepReplEvaluateInnerCommand::Register(editor, this);
        ZepReplEvaluateCommand::Register(editor, this);
    }

    void Notify(const std::shared_ptr<ZepMessage> &message) override {
        if (IgnoreChanges) return;

        if (message->messageId == Msg::Buffer) {
            const auto buffer_message = std::static_pointer_cast<BufferMessage>(message);
            switch (buffer_message->type) {
                case BufferMessageType::TextChanged:
                case BufferMessageType::TextDeleted:
                case BufferMessageType::TextAdded: {
                    auto *buffer = buffer_message->buffer;
                    if (buffer->name == s.Faust.Editor.FileName) {
                        // Redundant `c_str()` call removes an extra null char that seems to be at the end of the buffer string
                        q(SetValue{s.Faust.Code.Path, buffer->workingBuffer.string().c_str()}); // NOLINT(readability-redundant-string-cstr)
                    }
                    break;
                }
                case BufferMessageType::PreBufferChange:
                case BufferMessageType::Loaded:
                case BufferMessageType::MarkersChanged: break;
            }
        }
    }

    string ReplParse(ZepBuffer &buffer, const GlyphIterator &cursorOffset, ReplParseType type) override {
        ZEP_UNUSED(cursorOffset);
        ZEP_UNUSED(type);

        GlyphRange range = type == ReplParseType::OuterExpression ?
            buffer.GetExpression(ExpressionType::Outer, cursorOffset, {'('}, {')'}) :
            type == ReplParseType::SubExpression ?
            buffer.GetExpression(ExpressionType::Inner, cursorOffset, {'('}, {')'}) :
            GlyphRange(buffer.Begin(), buffer.End());

        if (range.first >= range.second) return "<No Expression>";

        // Flash the evaluated expression
        auto flashType = FlashType::Flash;
        float time = 1;
        buffer.BeginFlash(time, flashType, range);

        //        const auto &text = buffer.workingBuffer;
        //        auto eval = string(text.begin() + range.first.index, text.begin() + range.second.index);
        //        auto ret = chibi_repl(scheme, nullptr, eval);
        //        ret = RTrim(ret);
        //
        //        editor->SetCommandText(ret);
        //        return ret;

        return "";
    }

    string ReplParse(const string &str) override {
        //        auto ret = chibi_repl(scheme, nullptr, str);
        //        ret = RTrim(ret);
        //        return ret;
        return str;
    }

    bool ReplIsFormComplete(const string &str, int &indent) override {
        int count = 0;
        for (const auto &ch : str) {
            if (ch == '(') count++;
            if (ch == ')') count--;
        }

        if (count < 0) {
            indent = -1;
            return false;
        }

        if (count == 0) return true;

        int count2 = 0;
        indent = 1;
        for (auto &ch : str) {
            if (ch == '(') count2++;
            if (ch == ')') count2--;
            if (count2 == count) break;
            indent++;
        }
        return false;
    }

    std::function<void(std::shared_ptr<ZepMessage>)> callback;
};

static unique_ptr<ZepWrapper> Wrapper;

static const Menu FileMenu = {"File", {ShowOpenFaustFileDialog{}, ShowSaveFaustFileDialog{}}};
/*
 * TODO
 *   Implement `w` forward-word navigation for Vim mode
 *   Two-finger mouse pad scrolling
 *   Add mouse selection https://github.com/Rezonality/zep/issues/56
 *   Standard mode select-all left navigation moves cursor from the end of the selection, but should move from beginning
 *     (and right navigation should move from the end)
 */
void Faust::FaustEditor::Render() const {
    static unique_ptr<ZepEditor_ImGui> editor;
    if (!editor) {
        // Called once after the fonts are initialized
        editor = make_unique<ZepEditor_ImGui>(ZepPath(fs::current_path()));
        Wrapper = make_unique<ZepWrapper>(*editor);

        auto *display = editor->display;
        auto *font = UiContext.Fonts.FixedWidth;
        display->SetFont(ZepTextType::UI, std::make_shared<ZepFont_ImGui>(*display, font, 1.0));
        display->SetFont(ZepTextType::Text, std::make_shared<ZepFont_ImGui>(*display, font, 1.0));
        display->SetFont(ZepTextType::Heading1, std::make_shared<ZepFont_ImGui>(*display, font, 1.5));
        display->SetFont(ZepTextType::Heading2, std::make_shared<ZepFont_ImGui>(*display, font, 1.25));
        display->SetFont(ZepTextType::Heading3, std::make_shared<ZepFont_ImGui>(*display, font, 1.125));
        editor->InitWithText(s.Faust.Editor.FileName, s.Faust.Code);
    }

    auto *buffer = editor->GetActiveBuffer();

    if (BeginMenuBar()) {
        FileMenu.Draw();
        if (BeginMenu("Settings")) {
            if (BeginMenu("Editor mode")) {
                bool vim_enabled = strcmp(buffer->GetMode()->Name(), ZepMode_Vim::StaticName()) == 0;
                bool normal_enabled = !vim_enabled;
                if (ImGui::MenuItem("Vim", "CTRL+2", &vim_enabled)) {
                    editor->SetGlobalMode(ZepMode_Vim::StaticName());
                } else if (ImGui::MenuItem("Standard", "CTRL+1", &normal_enabled)) {
                    editor->SetGlobalMode(ZepMode_Standard::StaticName());
                }
                EndMenu();
            }
            if (BeginMenu("Theme")) {
                bool dark_enabled = editor->theme->GetThemeType() == ThemeType::Dark ? true : false;
                bool light_enabled = !dark_enabled;

                if (ImGui::MenuItem("Dark", "", &dark_enabled)) {
                    editor->theme->SetThemeType(ThemeType::Dark);
                } else if (ImGui::MenuItem("Light", "", &light_enabled)) {
                    editor->theme->SetThemeType(ThemeType::Light);
                }
                EndMenu();
            }
            EndMenu();
        }
        if (BeginMenu("Window")) {
            auto *tab_window = editor->activeTabWindow;
            if (ImGui::MenuItem("Horizontal split")) {
                tab_window->AddWindow(buffer, tab_window->GetActiveWindow(), RegionLayoutType::VBox);
            } else if (ImGui::MenuItem("Vertical split")) {
                tab_window->AddWindow(buffer, tab_window->GetActiveWindow(), RegionLayoutType::HBox);
            }
            EndMenu();
        }
        EndMenuBar();
    }

    const auto &pos = GetWindowPos();
    const auto &top_left = GetWindowContentRegionMin();
    const auto &bottom_right = GetWindowContentRegionMax();
    editor->SetDisplayRegion({{top_left.x + pos.x, top_left.y + pos.y}, {bottom_right.x + pos.x, bottom_right.y + pos.y}});

    //    editor->RefreshRequired(); // TODO Save battery by skipping display if not required.
    editor->Display();
    if (IsWindowFocused()) editor->HandleInput();
    else editor->ResetCursorTimer();

    // TODO this is not the usual immediate-mode case. Only set text if the text changed.
    //  Really what I want is for an application undo/redo containing code text changes to do exactly what zep does for undo/redo internally.
    //  XXX This currently always redundantly re-sets the buffer when the change comes from the editor.
    //  The comparison is also slow.
    // Redundant `c_str()` call removes an extra null char that seems to be at the end of the buffer string
    if (s.Faust.Code != buffer->workingBuffer.string().c_str()) { // NOLINT(readability-redundant-string-cstr)
        IgnoreChanges = true;
        editor->GetActiveBuffer()->SetText(s.Faust.Code);
        IgnoreChanges = false;
    }
}
void DestroyFaustEditor() {
    if (Wrapper) Wrapper.reset();
}
