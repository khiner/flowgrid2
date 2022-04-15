#include "../windows.h"

#include <filesystem>
#include "zep.h"
#include "ImGuiFileDialog.h"
#include "../../config.h"
#include "../../context.h"
#include "zep/regress.h"

using namespace Zep;

struct ZepWrapper : public IZepComponent, public IZepReplProvider {
    explicit ZepWrapper(const std::filesystem::path &root_path) : editor(ZepPath(root_path.string())) {
        editor.RegisterCallback(this);

        ZepRegressExCommand::Register(editor);

        // Repl
        ZepReplExCommand::Register(editor, this);
        ZepReplEvaluateOuterCommand::Register(editor, this);
        ZepReplEvaluateInnerCommand::Register(editor, this);
        ZepReplEvaluateCommand::Register(editor, this);
    }

    void Notify(const std::shared_ptr<ZepMessage> &message) override {
        if (message->messageId == Msg::Buffer) {
            auto buffer_message = std::static_pointer_cast<BufferMessage>(message);
            switch (buffer_message->type) {
                case BufferMessageType::TextChanged:
                case BufferMessageType::TextDeleted:
                    // Redundant `c_str()` call removes an extra null char that seems to be at the end of the buffer string
                case BufferMessageType::TextAdded: q.enqueue(set_faust_text{buffer_message->pBuffer->workingBuffer.string().c_str()}); // NOLINT(readability-redundant-string-cstr)
                    break;
                case BufferMessageType::PreBufferChange:
                case BufferMessageType::Loaded:
                case BufferMessageType::MarkersChanged: break;
            }
        }
    }


    std::string ReplParse(ZepBuffer &buffer, const GlyphIterator &cursorOffset, ReplParseType type) override {
        ZEP_UNUSED(cursorOffset);
        ZEP_UNUSED(type);

        GlyphRange range = type == ReplParseType::OuterExpression ?
                           buffer.GetExpression(ExpressionType::Outer, cursorOffset, {'('}, {')'}) :
                           type == ReplParseType::SubExpression ?
                           buffer.GetExpression(ExpressionType::Inner, cursorOffset, {'('}, {')'}) :
                           GlyphRange(buffer.Begin(), buffer.End());

        if (range.first >= range.second) return "<No Expression>";

        // Flash the evaluated expression
        FlashType flashType = FlashType::Flash;
        float time = 1.0f;
        buffer.BeginFlash(time, flashType, range);

//        const auto &text = buffer.workingBuffer;
//        auto eval = std::string(text.begin() + range.first.index, text.begin() + range.second.index);
//        auto ret = chibi_repl(scheme, NULL, eval);
//        ret = RTrim(ret);
//
//        editor.SetCommandText(ret);
//        return ret;

        return "";
    }

    std::string ReplParse(const std::string &str) override {
//        auto ret = chibi_repl(scheme, NULL, str);
//        ret = RTrim(ret);
//        return ret;
        return str;
    }

    bool ReplIsFormComplete(const std::string &str, int &indent) override {
        int count = 0;
        for (auto &ch: str) {
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
        for (auto &ch: str) {
            if (ch == '(') count2++;
            if (ch == ')') count2--;
            if (count2 == count) break;
            indent++;
        }
        return false;
    }

    ZepEditor_ImGui editor;
    std::function<void(std::shared_ptr<ZepMessage>)> callback;
};

std::unique_ptr<ZepWrapper> zep;

void zep_init() {
    zep = std::make_unique<ZepWrapper>(config.app_root);

    auto *display = zep->editor.display;
    auto pImFont = ImGui::GetIO().Fonts[0].Fonts[0];
    display->SetFont(ZepTextType::UI, std::make_shared<ZepFont_ImGui>(*display, pImFont, int(pImFont->FontSize)));
    display->SetFont(ZepTextType::Text, std::make_shared<ZepFont_ImGui>(*display, pImFont, int(pImFont->FontSize)));
    display->SetFont(ZepTextType::Heading1, std::make_shared<ZepFont_ImGui>(*display, pImFont, int(pImFont->FontSize * 1.5)));
    display->SetFont(ZepTextType::Heading2, std::make_shared<ZepFont_ImGui>(*display, pImFont, int(pImFont->FontSize * 1.25)));
    display->SetFont(ZepTextType::Heading3, std::make_shared<ZepFont_ImGui>(*display, pImFont, int(pImFont->FontSize * 1.125)));
    zep->editor.InitWithText("default.dsp", ui_s.audio.faust.code);
//    zep->editor.InitWithFileOrDir("...");
}

bool zep_initialized = false;

void zep_draw() {
    if (!zep_initialized) {
        // Called once after the fonts are initialized
        zep_init();
        zep_initialized = true;
    }

    const auto &pos = ImGui::GetWindowPos();
    const auto &top_left = ImGui::GetWindowContentRegionMin();
    const auto &bottom_right = ImGui::GetWindowContentRegionMax();
    const auto height = 200;
    zep->editor.SetDisplayRegion({
        {top_left.x + pos.x,     top_left.y + pos.y},
        {bottom_right.x + pos.x, top_left.y + pos.y + height}
    });
    zep->editor.Display();
    if (ImGui::IsWindowFocused()) zep->editor.HandleInput();
    else zep->editor.ResetCursorTimer();

    // TODO this is not the usual immediate-mode case. Only set text if an undo/redo has changed the text
    //  Really what I want is for an application undo/redo containing code text changes to do exactly what
    //  zep does for undo/redo internally.
//    if (false) zep->editor.buffers[0]->SetText(ui_s.audio.faust.code);
}

// TODO two-finger mouse pad scrolling
// TODO add mouse selection https://github.com/Rezonality/zep/issues/56
// TODO standard mode select-all left navigation moves cursor from the end of the selection, but should move from beginning
//  (and right navigation should move from the end)
void FaustEditor::draw(Window &) {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open")) {
                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".cpp,.h,.hpp", ".");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Settings")) {
            if (ImGui::BeginMenu("Editor Mode")) {
                const auto *buffer = zep->editor.activeTabWindow->GetActiveWindow()->buffer;
                bool enabledVim = strcmp(buffer->GetMode()->Name(), ZepMode_Vim::StaticName()) == 0;
                bool enabledNormal = !enabledVim;
                if (ImGui::MenuItem("Vim", "CTRL+2", &enabledVim)) {
                    zep->editor.SetGlobalMode(ZepMode_Vim::StaticName());
                } else if (ImGui::MenuItem("Standard", "CTRL+1", &enabledNormal)) {
                    zep->editor.SetGlobalMode(ZepMode_Standard::StaticName());
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Theme")) {
                bool enabledDark = zep->editor.theme->GetThemeType() == ThemeType::Dark ? true : false;
                bool enabledLight = !enabledDark;

                if (ImGui::MenuItem("Dark", "", &enabledDark)) {
                    zep->editor.theme->SetThemeType(ThemeType::Dark);
                } else if (ImGui::MenuItem("Light", "", &enabledLight)) {
                    zep->editor.theme->SetThemeType(ThemeType::Light);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            auto *tabWindow = zep->editor.activeTabWindow;
            if (ImGui::MenuItem("Horizontal Split")) {
                tabWindow->AddWindow(tabWindow->GetActiveWindow()->buffer, tabWindow->GetActiveWindow(), RegionLayoutType::VBox);
            } else if (ImGui::MenuItem("Vertical Split")) {
                tabWindow->AddWindow(tabWindow->GetActiveWindow()->buffer, tabWindow->GetActiveWindow(), RegionLayoutType::HBox);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();

        if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
            if (ImGuiFileDialog::Instance()->IsOk()) {
                auto filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                auto pBuffer = zep->editor.GetFileBuffer(filePathName);
                zep->editor.activeTabWindow->GetActiveWindow()->SetBuffer(pBuffer);
            }

            ImGuiFileDialog::Instance()->Close();
        }
    }

    zep_draw();
    ImGui::SetCursorPosY(zep->editor.editorRegion->rect.Bottom());
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
    if (!s.audio.faust.error.empty()) ImGui::Text("Faust error:\n%s", s.audio.faust.error.c_str());
    ImGui::PopStyleColor();
}

void FaustEditor::destroy() {
    zep.reset();
}
