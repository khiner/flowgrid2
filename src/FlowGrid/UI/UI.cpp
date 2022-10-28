#include <Tracy.hpp>
#include "SDL.h"
#include "SDL_opengl.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h" // TODO vulkan
#include "zep/stringutils.h"

#include "UI.h"
#include "../App.h"
#include "../StateJson.h"

#include "Faust/FaustEditor.h"
#include "../FileDialog/ImGuiFileDialogDemo.h"

using namespace fg;

/**md
## UI methods

These are the only public methods.

```cpp
    create_ui();
    tick_ui();
    destroy_ui(render_context);
```

## Render context methods

```cpp
    create_render_context();
    destroy_render_context(render_context);
```

## UI context methods

Superset of render context.

```cpp
    create_ui_context();
```

## Frame methods

```cpp
    prepare_frame();
    draw_frame();
    render_frame(render_context);
```
 */

struct RenderContext {
    SDL_Window *window = nullptr;
    SDL_GLContext gl_context{};
    const char *glsl_version = "#version 150";
    ImGuiIO io;
};

RenderContext create_render_context() {
#if defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    context.glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    auto window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED;

    RenderContext draw_context;
    draw_context.window = SDL_CreateWindow("FlowGrid", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    draw_context.gl_context = SDL_GL_CreateContext(draw_context.window);

    return draw_context;
}

void destroy_render_context(const RenderContext &rc) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    SDL_GL_DeleteContext(rc.gl_context);
    SDL_DestroyWindow(rc.window);
    SDL_Quit();
}

UIContext create_ui_context(const RenderContext &render_context) {
    SDL_GL_MakeCurrent(render_context.window, render_context.gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    auto *imgui_context = ImGui::CreateContext();
    auto *implot_context = ImPlot::CreateContext();

    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; // Disable ImGui's .ini file saving. We handle this manually.

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //io.FontAllowUserScaling = true;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(render_context.window, render_context.gl_context);
    ImGui_ImplOpenGL3_Init(render_context.glsl_version);

    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    const static float atlas_scale = Style::ImGuiStyle::FontAtlasScale;
    io.FontGlobalScale = s.Style.ImGui.FontScale / atlas_scale;
    c.defaultFont = io.Fonts->AddFontFromFileTTF("../res/fonts/AbletonSansMedium.otf", 16 * atlas_scale);
    c.fixedWidthFont = io.Fonts->AddFontFromFileTTF("../lib/imgui/misc/fonts/Cousine-Regular.ttf", 15 * atlas_scale);
    io.Fonts->AddFontFromFileTTF("../lib/imgui/misc/fonts/ProggyClean.ttf", 14 * atlas_scale);
//    c.defaultFont = io.Fonts->AddFontFromFileTTF("../lib/imgui/misc/fonts/Roboto-Medium.ttf", font_scale * 16.0f);
    return {imgui_context, implot_context};
}

void prepare_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void render_frame(RenderContext &rc) {
    ImGui::Render();
    glViewport(0, 0, (int) rc.io.DisplaySize.x, (int) rc.io.DisplaySize.y);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(rc.window);
}

using KeyShortcut = std::pair<ImGuiModFlags, ImGuiKey>;

const std::map<string, ImGuiModFlags> mod_keys{
    {"shift", ImGuiModFlags_Shift},
    {"ctrl", ImGuiModFlags_Ctrl},
    {"alt", ImGuiModFlags_Alt},
    {"cmd", ImGuiModFlags_Super},
};

// Handles any number of mods, along with any single non-mod character.
// Example: 'shift+cmd+s'
// **Case-sensitive. `shortcut` must be lowercase.**
std::optional<KeyShortcut> parse_shortcut(const string &shortcut) {
    vector<string> tokens;
    Zep::string_split(shortcut, "+", tokens);
    if (tokens.empty()) return {};

    const string command = tokens.back();
    if (command.length() != 1) return {};

    tokens.pop_back();

    auto key = ImGuiKey(command[0] - 'a' + ImGuiKey_A);
    ImGuiModFlags mod_flags = ImGuiModFlags_None;
    while (!tokens.empty()) {
        mod_flags |= mod_keys.at(tokens.back());
        tokens.pop_back();
    }

    return {{mod_flags, key}};
}

// Transforming `map<ActionID, string>` to `map<KeyShortcut, ActionID>`
const auto key_map = action::shortcut_for_id | transform([](const auto &entry) {
    const auto &[action_id, shortcut] = entry;
    return std::pair<KeyShortcut, ActionID>(parse_shortcut(shortcut).value(), action_id);
}) | to<std::map<KeyShortcut, ActionID>>;

// TODO what about going the other way? Get list of pressed KeyShortcuts.
//  Then map from action_id to KeyShortcut. See `faust_editor::HandleInput`.
bool is_shortcut_pressed(const KeyShortcut &key_shortcut) {
    const auto &[mod, key] = key_shortcut;
    return mod == ImGui::GetIO().KeyMods && ImGui::IsKeyPressed(ImGui::GetKeyIndex(key));
}

RenderContext render_context;

UIContext create_ui() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) throw std::runtime_error(SDL_GetError());

    render_context = create_render_context();
    const auto &uiContext = create_ui_context(render_context);

    IGFD::InitializeDemo();

    return uiContext;
}

// Main UI tick function
void tick_ui() {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
    // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(render_context.window))) {
            q(close_application{}, true);
        }
    }

    for (const auto &[shortcut, action_id]: key_map) {
        if (is_shortcut_pressed(shortcut) && c.action_allowed(action_id)) {
            q(action::create(action_id));
        }
    }

    prepare_frame();
    s.Draw(); // All the actual application content drawing, along with initial dockspace setup, happens in this main state `Draw()` method.
    render_frame(render_context);

    auto &io = ImGui::GetIO();
    if (io.WantSaveIniSettings) {
        // ImGui sometimes sets this flags when settings have not, in fact, changed.
        // E.g. if you click and hold a window-resize, it will set this every frame, even if the cursor is still (no window size change).
        const json new_settings = ImGuiSettingsData(c.ui->imgui_context);
        if (json(s.ImGuiSettings) != new_settings) q(set_imgui_settings{new_settings});
        io.WantSaveIniSettings = false;
    }

    FrameMark
}

void destroy_ui() {
    IGFD::CleanupDemo();
    destroy_faust_editor();
    destroy_render_context(render_context);
}