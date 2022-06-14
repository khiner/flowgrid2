#include "imgui.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui_internal.h"

#include "ImGuiFileDialog.h"
#include "ImGuiFileDialogDemo.h"

#include <sstream>
#include <string>

static bool canValidateDialog = false;

// If `cantContinue` is false, the user can't validate the dialog.
inline void InfosPane(const char *filter, IGFDUserDatas userData, bool *cantContinue) {
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Infos Pane");
    ImGui::Text("Selected Filter: %s", filter);
    if (userData) ImGui::Text("User Data: %s", (const char *) userData);
    ImGui::Checkbox("If not checked, you can't validate the dialog", &canValidateDialog);
    if (cantContinue) *cantContinue = canValidateDialog;
}

inline bool RadioButtonLabeled(const char *label, const char *help, bool active, bool disabled) {
    using namespace ImGui;

    ImGuiWindow *window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext &g = *GImGui;
    const ImGuiStyle &style = g.Style;
    float w = CalcItemWidth();
    if (w == window->ItemWidthDefault) w = 0.0f; // no push item width
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, nullptr, true);
    ImVec2 bb_size = ImVec2(style.FramePadding.x * 2 - 1, style.FramePadding.y * 2 - 1) + label_size;
    bb_size.x = ImMax(w, bb_size.x);

    const ImRect check_bb(window->DC.CursorPos, window->DC.CursorPos + bb_size);
    ItemSize(check_bb, style.FramePadding.y);

    if (!ItemAdd(check_bb, id)) return false;

    // check
    bool pressed = false;
    if (!disabled) {
        bool hovered, held;
        pressed = ButtonBehavior(check_bb, id, &hovered, &held);

        window->DrawList->AddRectFilled(check_bb.Min, check_bb.Max, GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);
        if (active) {
            const ImU32 col = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            window->DrawList->AddRectFilled(check_bb.Min, check_bb.Max, col, style.FrameRounding);
        }
    }

    // circle shadow + bg
    if (style.FrameBorderSize > 0.0f) {
        window->DrawList->AddRect(check_bb.Min + ImVec2(1, 1), check_bb.Max, GetColorU32(ImGuiCol_BorderShadow), style.FrameRounding);
        window->DrawList->AddRect(check_bb.Min, check_bb.Max, GetColorU32(ImGuiCol_Border), style.FrameRounding);
    }

    if (label_size.x > 0.0f) RenderText(check_bb.GetCenter() - label_size * 0.5f, label);
    if (help && ImGui::IsItemHovered())ImGui::SetTooltip("%s", help);

    return pressed;
}

template<typename T>
inline bool RadioButtonLabeled_BitWise(
    const char *label, const char *help, T *container, T flag,
    bool oneOrZeroAtTime = false, // only one selected at a time
    bool alwaysOne = true, // radio behavior, always one selected
    T flagsToTakeIntoAccount = (T) 0,
    bool disableSelection = false,
    ImFont *labelFont = nullptr) // radio will use only these flags
{
    (void) labelFont; // remove unused warnings

    bool selected = (*container) & flag;
    const bool res = RadioButtonLabeled(label, help, selected, disableSelection);
    if (res) {
        if (!selected) {
            if (oneOrZeroAtTime) {
                if (flagsToTakeIntoAccount) {
                    if (flag & flagsToTakeIntoAccount) {
                        *container = (T) (*container & ~flagsToTakeIntoAccount); // remove these flags
                        *container = (T) (*container | flag); // add
                    }
                } else *container = flag; // set
            } else {
                if (flagsToTakeIntoAccount) {
                    if (flag & flagsToTakeIntoAccount) {
                        *container = (T) (*container & ~flagsToTakeIntoAccount); // remove these flags
                        *container = (T) (*container | flag); // add
                    }
                } else *container = (T) (*container | flag); // add
            }
        } else {
            if (oneOrZeroAtTime) {
                if (!alwaysOne) *container = (T) (0); // remove all
            } else *container = (T) (*container & ~flag); // remove one
        }
    }
    return res;
}

ImGuiFileDialog *cfileDialog;
ImGuiFileDialog fileDialog2;
ImGuiFileDialog fileDialogEmbedded3;

void IGFD::InitializeDemo() {
#ifdef USE_THUMBNAILS
    ImGuiFileDialog::Instance()->SetCreateThumbnailCallback([](IGFD_Thumbnail_Info *thumbnail_info) -> void
    {
        if (thumbnail_info && thumbnail_info->isReadyToUpload && thumbnail_info->textureFileDatas) {
            GLuint textureId = 0;
            glGenTextures(1, &textureId);
            thumbnail_info->textureID = (void*)(size_t)textureId;

            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                (GLsizei)thumbnail_info->textureWidth, (GLsizei)thumbnail_info->textureHeight,
                0, GL_RGBA, GL_UNSIGNED_BYTE, thumbnail_info->textureFileDatas);
            glFinish();
            glBindTexture(GL_TEXTURE_2D, 0);

            delete[] thumbnail_info->textureFileDatas;
            thumbnail_info->textureFileDatas = nullptr;

            thumbnail_info->isReadyToUpload = false;
            thumbnail_info->isReadyToDisplay = true;
        }
    });
    fileDialogEmbedded3.SetCreateThumbnailCallback([](IGFD_Thumbnail_Info* thumbnail_info) -> void
    {
        if (thumbnail_info && thumbnail_info->isReadyToUpload && thumbnail_info->textureFileDatas) {
            GLuint textureId = 0;
            glGenTextures(1, &textureId);
            thumbnail_info->textureID = (void*)(size_t)textureId;

            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                (GLsizei)thumbnail_info->textureWidth, (GLsizei)thumbnail_info->textureHeight,
                0, GL_RGBA, GL_UNSIGNED_BYTE, thumbnail_info->textureFileDatas);
            glFinish();
            glBindTexture(GL_TEXTURE_2D, 0);

            delete[] thumbnail_info->textureFileDatas;
            thumbnail_info->textureFileDatas = nullptr;

            thumbnail_info->isReadyToUpload = false;
            thumbnail_info->isReadyToDisplay = true;
        }
    });
    ImGuiFileDialog::Instance()->SetDestroyThumbnailCallback([](IGFD_Thumbnail_Info* thumbnail_info)
    {
        if (thumbnail_info)
        {
            GLuint texID = (GLuint)(size_t)thumbnail_info->textureID;
            glDeleteTextures(1, &texID);
            glFinish();
        }
    });
    fileDialogEmbedded3.SetDestroyThumbnailCallback([](IGFD_Thumbnail_Info* thumbnail_info)
    {
        if (thumbnail_info)
        {
            GLuint texID = (GLuint)(size_t)thumbnail_info->textureID;
            glDeleteTextures(1, &texID);
            glFinish();
        }
    });
#endif // USE_THUMBNAILS

    ImGui::GetIO().Fonts->AddFontDefault();
    static const ImWchar icons_ranges[] = {ICON_MIN_IGFD, ICON_MAX_IGFD, 0};
    ImFontConfig icons_config;
    icons_config.DstFont = ImGui::GetDefaultFont();
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_IGFD, 15.0f, &icons_config, icons_ranges);

    // Singleton access
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f));
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByExtention, ".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeDir, nullptr, ImVec4(0.5f, 1.0f, 0.9f, 0.9f), ICON_IGFD_FOLDER); // for all dirs
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile, "CMakeLists.txt", ImVec4(0.1f, 0.5f, 0.5f, 0.9f), ICON_IGFD_ADD);
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByFullName, "doc", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_FILE_PIC);
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile, nullptr, ImVec4(0.2f, 0.9f, 0.2f, 0.9f), ICON_IGFD_FILE); // for all link files
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeDir | IGFD_FileStyleByTypeLink, nullptr, ImVec4(0.8f, 0.8f, 0.8f, 0.8f), ICON_IGFD_FOLDER); // for all link dirs
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile | IGFD_FileStyleByTypeLink, nullptr, ImVec4(0.8f, 0.8f, 0.8f, 0.8f), ICON_IGFD_FILE); // for all link files
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeDir | IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_BOOKMARK);
    ImGuiFileDialog::Instance()->SetFileStyle(IGFD_FileStyleByTypeFile | IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.5f, 0.8f, 0.5f, 0.9f), ICON_IGFD_SAVE);

    // Multi dialog instance behavior
    fileDialog2.SetFileStyle(IGFD_FileStyleByExtention, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f));
    fileDialog2.SetFileStyle(IGFD_FileStyleByExtention, ".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f));
    fileDialog2.SetFileStyle(IGFD_FileStyleByExtention, ".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f));
    fileDialog2.SetFileStyle(IGFD_FileStyleByExtention, ".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f));
    fileDialog2.SetFileStyle(IGFD_FileStyleByExtention, ".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
    fileDialog2.SetFileStyle(IGFD_FileStyleByExtention, ".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type
    fileDialog2.SetFileStyle(IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_BOOKMARK);

    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByExtention, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f));
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByExtention, ".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f));
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByExtention, ".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f));
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByExtention, ".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f));
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByExtention, ".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC); // add an icon for the filter type
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByExtention, ".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]"); // add an text for a filter type
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_BOOKMARK);
    fileDialogEmbedded3.SetFileStyle(IGFD_FileStyleByFullName, "doc", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_FILE_PIC);

    // C interface
    cfileDialog = IGFD_Create();
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f), "", nullptr);
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".cpp", ImVec4(1.0f, 1.0f, 0.0f, 0.9f), "", nullptr);
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".h", ImVec4(0.0f, 1.0f, 0.0f, 0.9f), "", nullptr);
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".hpp", ImVec4(0.0f, 0.0f, 1.0f, 0.9f), "", nullptr);
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".md", ImVec4(1.0f, 0.0f, 1.0f, 0.9f), "", nullptr);
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".png", ImVec4(0.0f, 1.0f, 1.0f, 0.9f), ICON_IGFD_FILE_PIC, nullptr); // add an icon for the filter type
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByExtention, ".gif", ImVec4(0.0f, 1.0f, 0.5f, 0.9f), "[GIF]", nullptr); // add an text for a filter type
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByFullName, "doc", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_FILE_PIC, nullptr);
    IGFD_SetFileStyle(cfileDialog, IGFD_FileStyleByTypeDir | IGFD_FileStyleByContainedInFullName, ".git", ImVec4(0.9f, 0.2f, 0.0f, 0.9f), ICON_IGFD_BOOKMARK, nullptr);

#ifdef USE_BOOKMARK
    // Load bookmarks
    std::ifstream docFile_1("bookmarks_1.conf", std::ios::in);
    if (docFile_1.is_open()) {
        std::stringstream strStream;
        strStream << docFile_1.rdbuf();//read the file
        ImGuiFileDialog::Instance()->DeserializeBookmarks(strStream.str());
        docFile_1.close();
    }

    std::ifstream docFile_2("bookmarks_2.conf", std::ios::in);
    if (docFile_2.is_open()) {
        std::stringstream strStream;
        strStream << docFile_2.rdbuf();//read the file
        fileDialog2.DeserializeBookmarks(strStream.str());
        docFile_2.close();
    }

    // C interface
    std::ifstream docFile_c("bookmarks_c.conf", std::ios::in);
    if (docFile_c.is_open()) {
        std::stringstream strStream;
        strStream << docFile_c.rdbuf();//read the file
        IGFD_DeserializeBookmarks(cfileDialog, strStream.str().c_str());
        docFile_c.close();
    }

    // Add bookmark by code
    ImGuiFileDialog::Instance()->AddBookmark("Current dir", ".");
#endif
}

void IGFD::ShowDemo() {
    static std::string filePathName;
    static std::string filePath;
    static std::string filter;
    static std::string userData;
    static std::vector<std::pair<std::string, std::string>> selection = {};

#ifdef USE_EXPLORATION_BY_KEYS
    static float flashingAttenuationInSeconds = 1.0f;
    if (ImGui::Button("R##resetflashlifetime"))
    {
        flashingAttenuationInSeconds = 1.0f;
        ImGuiFileDialog::Instance()->SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);
        fileDialog2.SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);

        // c interface
        IGFD_SetFlashingAttenuationInSeconds(cfileDialog, flashingAttenuationInSeconds);
    }
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    if (ImGui::SliderFloat("Flash lifetime (s)", &flashingAttenuationInSeconds, 0.01f, 5.0f))
    {
        ImGuiFileDialog::Instance()->SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);
        fileDialog2.SetFlashingAttenuationInSeconds(flashingAttenuationInSeconds);

        // c interface
        IGFD_SetFlashingAttenuationInSeconds(cfileDialog, flashingAttenuationInSeconds);
    }
    ImGui::PopItemWidth();
#endif
    static bool UseWindowConstraints = true;
    ImGui::Separator();
    ImGui::Checkbox("Use file dialog constraint", &UseWindowConstraints);
    ImGui::Text("Constraints is used here for define min/max file dialog size");
    ImGui::Separator();
    static bool standardDialogMode = false;
    ImGui::Text("Open mode: ");
    ImGui::SameLine();
    if (RadioButtonLabeled("Standard", "Open dialog in standard mode", standardDialogMode, false)) standardDialogMode = true;
    ImGui::SameLine();
    if (RadioButtonLabeled("Modal", "Open dialog in modal mode", !standardDialogMode, false)) standardDialogMode = false;

    static ImGuiFileDialogFlags flags = ImGuiFileDialogFlags_Default;
    ImGui::Text("ImGuiFileDialog flags: ");
    ImGui::Indent();
    {
        ImGui::Text("Commons:");
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Overwrite", "Overwrite verification before dialog closing", &flags, ImGuiFileDialogFlags_ConfirmOverwrite);
        ImGui::SameLine();
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Hide hidden files", "Hide hidden files", &flags, ImGuiFileDialogFlags_DontShowHiddenFiles);
        ImGui::SameLine();
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Disable directory creation", "Disable directory creation button in dialog", &flags, ImGuiFileDialogFlags_DisableCreateDirectoryButton);
#ifdef USE_THUMBNAILS
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Disable thumbnails mode", "Disable thumbnails display in dialog", &flags, ImGuiFileDialogFlags_DisableThumbnailMode);
#endif
#ifdef USE_BOOKMARK
        ImGui::SameLine();
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Disable bookmark mode", "Disable bookmark display in dialog", &flags, ImGuiFileDialogFlags_DisableBookmarkMode);
#endif

        ImGui::Text("Hide Column by default: (saved in imgui.ini, \n\tso defined when the imgui.ini does not exist)");
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Hide 'Type' column", "Hide file type by default", &flags, ImGuiFileDialogFlags_HideColumnType);
        ImGui::SameLine();
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Hide 'Size' column", "Hide file size by default", &flags, ImGuiFileDialogFlags_HideColumnSize);
        ImGui::SameLine();
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Hide 'Date' column", "Hide file date by default", &flags, ImGuiFileDialogFlags_HideColumnDate);
        RadioButtonLabeled_BitWise<ImGuiFileDialogFlags>("Case-insensitive extensions", "will not take into account the case of file extensions", &flags, ImGuiFileDialogFlags_CaseInsensitiveExtention);
    }
    ImGui::Unindent();

    static const char *chooseFileDialogKey = "ChooseFileDlgKey";
    static const char *chooseFile = ICON_IGFD_FOLDER_OPEN " Choose a file";
    static const char *chooseFileSave = ICON_IGFD_SAVE " Choose a file";

    ImGui::Text("Singleton access:");
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open file dialog")) {
        const char *filters = ".*,.cpp,.h,.hpp";
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFile, filters, ".", "", 1, nullptr, flags);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFile, filters, ".", "", 1, nullptr, flags);
    }
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open file dialog with collections of filters")) {
        const char *filters = "All files{.*},Source files (*.cpp *.h *.hpp){.cpp,.h,.hpp},Image files (*.png *.gif *.jpg *.jpeg){.png,.gif,.jpg,.jpeg},.md";
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFile, filters, ".", "", 1, nullptr, flags);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFile, filters, ".", "", 1, nullptr, flags);
    }
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open file dialog with selection of 5 items")) {
        const char *filters = ".*,.cpp,.h,.hpp";
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFile, filters, ".", "", 5, nullptr, flags);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFile, filters, ".", "", 5, nullptr, flags);
    }
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open file dialog with infinite selection")) {
        const char *filters = ".*,.cpp,.h,.hpp";
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFile, filters, ".", "", 0, nullptr, flags);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFile, filters, ".", "", 0, nullptr, flags);
    }
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open file dialog with last file path name")) {
        const char *filters = ".*,.cpp,.h,.hpp";
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFile, filters, filePathName, 1, nullptr, flags);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFile, filters, filePathName, 1, nullptr, flags);
    }
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open all file types with \".*\" filter")) {
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFile, ".*", ".", "", 1, nullptr, flags);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFile, ".*", ".", "", 1, nullptr, flags);
    }
    auto saveFileUserData = IGFDUserDatas("SaveFile");
    if (ImGui::Button(ICON_IGFD_SAVE " Save file dialog with a custom pane")) {
        const char *filters = "C++ File (*.cpp){.cpp}";
        if (standardDialogMode)
            ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFileSave, filters,
                ".", "", [](auto &&PH1, auto &&PH2, auto &&PH3) { return InfosPane(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3)); }, 350, 1,
                saveFileUserData, flags);
        else
            ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFileSave, filters,
                ".", "", [](auto &&PH1, auto &&PH2, auto &&PH3) { return InfosPane(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3)); }, 350, 1,
                saveFileUserData, flags);
    }
    if (ImGui::Button(ICON_IGFD_SAVE "Save file dialog with confirm-overwrite dialog if file exists")) {
        const char *filters = "C/C++ file (*.c *.cpp){.c,.cpp}, Header file (*.h){.h}";
        if (standardDialogMode) ImGuiFileDialog::Instance()->OpenDialog(chooseFileDialogKey, chooseFileSave, filters, ".", "", 1, saveFileUserData, ImGuiFileDialogFlags_ConfirmOverwrite);
        else ImGuiFileDialog::Instance()->OpenModal(chooseFileDialogKey, chooseFileSave, filters, ".", "", 1, saveFileUserData, ImGuiFileDialogFlags_ConfirmOverwrite);
    }

    ImGui::Text("Other instance (multi dialog demo):");

    // Let filters be null for open directory chooser.
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN " Open directory dialog")) {
        if (standardDialogMode)fileDialog2.OpenDialog("ChooseDirDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a directory", nullptr, ".", 1, nullptr, flags);
        else fileDialog2.OpenModal("ChooseDirDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a directory", nullptr, ".", 1, nullptr, flags);
    }
    if (ImGui::Button(ICON_IGFD_FOLDER_OPEN "Open directory dialog with a selection of 5 items")) {
        if (standardDialogMode) fileDialog2.OpenDialog("ChooseDirDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a directory", nullptr, ".", "", 5, nullptr, flags);
        else fileDialog2.OpenModal("ChooseDirDlgKey", ICON_IGFD_FOLDER_OPEN " Choose a directory", nullptr, ".", "", 5, nullptr, flags);
    }

    ImGui::Separator();

    ImGui::Text("C instance demo:");
    if (ImGui::Button("C " ICON_IGFD_SAVE " Save file dialog with a custom pane")) {
        const char *filters = "C++ File (*.cpp){.cpp}";
        if (standardDialogMode) IGFD_OpenPaneDialog(cfileDialog, chooseFileDialogKey, chooseFileSave, filters, ".", "", &InfosPane, 350, 1, (void *) ("SaveFile"), flags);
        else IGFD_OpenPaneModal(cfileDialog, chooseFileDialogKey, chooseFileSave, filters, ".", "", &InfosPane, 350, 1, (void *) ("SaveFile"), flags);
    }

    ImGui::Separator();

    ImGui::Text("Embedded dialog:");
    fileDialogEmbedded3.OpenDialog("embedded", "Select file", ".*", "", -1, nullptr,
        ImGuiFileDialogFlags_NoDialog |
#ifdef USE_BOOKMARK
            ImGuiFileDialogFlags_DisableBookmarkMode |
#endif
            ImGuiFileDialogFlags_DisableCreateDirectoryButton | ImGuiFileDialogFlags_ReadOnlyFileNameField);

    // When embedded, `minSize` does nothing. Only `maxSize` can size the dialog frame.
    if (fileDialogEmbedded3.Display("embedded", ImGuiWindowFlags_NoCollapse, ImVec2(0, 0), ImVec2(0, 350))) {
        if (fileDialogEmbedded3.IsOk()) {
            filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
            filter = ImGuiFileDialog::Instance()->GetCurrentFilter();
            // Convert from string because a string was passed as a `userData`, but it can be what you want.
            if (ImGuiFileDialog::Instance()->GetUserDatas()) {
                userData = std::string((const char *) ImGuiFileDialog::Instance()->GetUserDatas());
            }
            auto sel = ImGuiFileDialog::Instance()->GetSelection(); // Multi-selection
            selection.clear();
            for (const auto &s: sel) {
                selection.emplace_back(s.first, s.second);
            }
        }
        fileDialogEmbedded3.Close();
    }

    ImGui::Separator();

    ImVec2 minSize = ImVec2(0, 0);
    ImVec2 maxSize = ImVec2(FLT_MAX, FLT_MAX);

    if (UseWindowConstraints) {
        maxSize = ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()) * 0.7f;
        minSize = maxSize * 0.25f;
    }

    // You can define your flags and min/max window size.
    // These settings are defined by default:
    //   flags => ImGuiWindowFlags_NoCollapse
    //   minSize => 0,0
    //   maxSize => FLT_MAX, FLT_MAX (defined is float.h)

    if (ImGuiFileDialog::Instance()->Display(chooseFileDialogKey, ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
            filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
            filter = ImGuiFileDialog::Instance()->GetCurrentFilter();
            // Convert from string because a string was passed as a `userData`, but it can be what you want.
            if (ImGuiFileDialog::Instance()->GetUserDatas()) {
                userData = std::string((const char *) ImGuiFileDialog::Instance()->GetUserDatas());
            }
            auto sel = ImGuiFileDialog::Instance()->GetSelection(); // Multi-selection
            selection.clear();
            for (const auto &s: sel) {
                selection.emplace_back(s.first, s.second);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (fileDialog2.Display("ChooseDirDlgKey", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
        if (fileDialog2.IsOk()) {
            filePathName = fileDialog2.GetFilePathName();
            filePath = fileDialog2.GetCurrentPath();
            filter = fileDialog2.GetCurrentFilter();
            // Convert from string because a string was passed as a `userData`, but it can be what you want.
            if (fileDialog2.GetUserDatas()) {
                userData = std::string((const char *) fileDialog2.GetUserDatas());
            }
            auto sel = fileDialog2.GetSelection(); // multiselection
            selection.clear();
            for (const auto &s: sel) {
                selection.emplace_back(s.first, s.second);
            }
        }
        fileDialog2.Close();
    }

    if (IGFD_DisplayDialog(cfileDialog, chooseFileDialogKey, ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
        if (IGFD_IsOk(cfileDialog)) {
            char *cfilePathName = IGFD_GetFilePathName(cfileDialog);
            if (cfilePathName) filePathName = cfilePathName;
            char *cfilePath = IGFD_GetCurrentPath(cfileDialog);
            if (cfilePath) filePath = cfilePath;
            char *cfilter = IGFD_GetCurrentFilter(cfileDialog);
            if (cfilter) filter = cfilter;
            // Convert from string because a string was passed as a `userData`, but it can be what you want.
            void *cUserData = IGFD_GetUserDatas(cfileDialog);
            if (cUserData) userData = (const char *) cUserData;
            IGFD_Selection cSelection = IGFD_GetSelection(cfileDialog); // Multi-selection

            selection.clear();
            for (size_t i = 0; i < cSelection.count; i++) {
                std::string _fileName = cSelection.table[i].fileName;
                std::string _filePathName = cSelection.table[i].filePathName;
                selection.emplace_back(_fileName, _filePathName);
            }

            // Destroy
            delete[] cfilePathName;
            delete[] cfilePath;
            delete[] cfilter;
            IGFD_Selection_DestroyContent(&cSelection);
        }
        IGFD_CloseDialog(cfileDialog);
    }

    ImGui::Separator();

    ImGui::Text("ImGuiFileDialog returns:\n");
    ImGui::Indent();
    {
        ImGui::Text("GetFilePathName(): %s", filePathName.c_str());
        ImGui::Text("GetFilePath(): %s", filePath.c_str());
        ImGui::Text("GetCurrentFilter(): %s", filter.c_str());
        ImGui::Text("GetUserDatas() (was a `std::string` in this sample): %s", userData.c_str());
        ImGui::Text("GetSelection(): ");
        ImGui::Indent();
        {
            static int selected = false;
            if (ImGui::BeginTable("##GetSelection", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                ImGui::TableSetupColumn("File name", ImGuiTableColumnFlags_WidthStretch, -1, 0);
                ImGui::TableSetupColumn("File path name", ImGuiTableColumnFlags_WidthFixed, -1, 1);
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin((int) selection.size(), ImGui::GetTextLineHeightWithSpacing());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                        const auto &sel = selection[i];
                        ImGui::TableNextRow();
                        if (ImGui::TableSetColumnIndex(0)) {
                            ImGuiSelectableFlags selectableFlags = ImGuiSelectableFlags_AllowDoubleClick;
                            selectableFlags |= ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;
                            if (ImGui::Selectable(sel.first.c_str(), i == selected, selectableFlags)) selected = i;
                        }
                        if (ImGui::TableSetColumnIndex(1)) {
                            ImGui::Text("%s", sel.second.c_str());
                        }
                    }
                }
                clipper.End();

                ImGui::EndTable();
            }
        }
        ImGui::Unindent();
    }
    ImGui::Unindent();
}

void IGFD::CleanupDemo() {
#ifdef USE_THUMBNAILS
    ImGuiFileDialog::Instance()->ManageGPUThumbnails();
    fileDialogEmbedded3.ManageGPUThumbnails();
#endif

#ifdef USE_BOOKMARK
    // Remove bookmark
    ImGuiFileDialog::Instance()->RemoveBookmark("Current dir");

    // Save bookmarks dialog 1
    std::ofstream configFileWriter_1("bookmarks_1.conf", std::ios::out);
    if (!configFileWriter_1.bad()) {
        configFileWriter_1 << ImGuiFileDialog::Instance()->SerializeBookmarks();
        configFileWriter_1.close();
    }
    // Save bookmarks dialog 2
    std::ofstream configFileWriter_2("bookmarks_2.conf", std::ios::out);
    if (!configFileWriter_2.bad()) {
        configFileWriter_2 << fileDialog2.SerializeBookmarks();
        configFileWriter_2.close();
    }
    // Save bookmarks dialog c interface
    std::ofstream configFileWriter_c("bookmarks_c.conf", std::ios::out);
    if (!configFileWriter_c.bad()) {
        if (char *s = IGFD_SerializeBookmarks(cfileDialog, true)) {
            configFileWriter_c << s;
            configFileWriter_c.close();
        }
    }
#endif
}
