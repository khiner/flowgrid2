#include "TextEditor.h"

#include "imgui.h"

TextEditor::TextEditor(ComponentArgs &&args, const fs::path &file_path)
    : Component(std::move(args)), _LastOpenedFilePath(file_path) {
    WindowFlags |= ImGuiWindowFlags_MenuBar;
}

TextEditor::~TextEditor() {}

bool TextEditor::Empty() const { return Buffer.Empty(); }
std::string TextEditor::GetText() const { return Buffer.GetText(); }

using namespace ImGui;

void TextEditor::RenderMenu() const {
    if (BeginMenuBar()) {
        Buffer.RenderMenu();
        EndMenuBar();
    }
}

void TextEditor::Render() const {
    RenderMenu();
    Buffer.Render();
}

void TextEditor::RenderDebug() const { Buffer.RenderDebug(); }
