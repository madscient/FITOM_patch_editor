// FITOM_X Patch Editor - GUI shell.
//
// Owns the window/OpenGL context and the Dear ImGui context, and runs the
// main render loop. Implements the first slice of the patch browser UI on
// top of fpe::PatchWorkspace (see include/fpe/PatchWorkspace.h):
//
//   MainMenu -> (load profile) -> FileBrowser -> (pick *.profile.json)
//     -> load succeeds -> Outline (read-only tree of the loaded profile)
//     -> load fails    -> error popup, stays on FileBrowser
//
// If a profile path is given as argv[1], that profile is loaded up front
// and the app starts directly on Outline (as if it had just been picked
// from FileBrowser), skipping MainMenu/FileBrowser entirely. This is for
// launching from an already-running FITOM_X instance, which knows which
// profile it currently has loaded and can hand that path straight to the
// editor. On load failure, falls back to the normal MainMenu + error
// popup (see main()).
//
// "新規プロファイル作成"/"プロファイル削除" are shown in the main menu
// but intentionally left disabled - not implemented yet. Patch editing
// forms, CRUD UI, and the virtual MIDI controller are also still future
// work (see docs/STATUS.md).
//
// Backend: GLFW (window/input) + OpenGL3 (rendering) + GLEW (GL function
// loading). All three, plus Dear ImGui itself and nlohmann/json, are
// resolved via vcpkg (see vcpkg.json / CMakePresets.json) - there is no
// vendored/submoduled third-party source in this repository.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// GLEW must be included before any other header that may pull in the
// platform's own (older) OpenGL headers - including GLFW's.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "fpe/PatchWorkspace.h"
#include "fpe/VoicePatchType.h"

namespace fs = std::filesystem;

namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// Dear ImGui's built-in font only covers Basic Latin; this project's UI
// text is Japanese, so without a CJK-capable font every label renders as
// tofu ("?") boxes. Try a few common system fonts; if none are found,
// fall back to the default font (tofu) rather than failing to start.
void loadFonts(ImGuiIO& io) {
    static const char* candidates[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\meiryo.ttc",
        "C:\\Windows\\Fonts\\YuGothM.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJKjp-Regular.otf",
#endif
    };
    for (const char* path : candidates) {
        if (io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese())) {
            return;
        }
    }
    std::fprintf(stderr, "warning: no CJK-capable font found; Japanese UI text will render as tofu boxes\n");
}

enum class AppState { MainMenu, FileBrowser, Outline };

// Lists *.profile.json files (and subdirectories, for navigation) in one
// directory. Re-scanned via refresh() whenever the current directory
// changes - not re-scanned every frame.
struct FileBrowserState {
    fs::path currentDir;
    std::vector<fs::path> subdirs;
    std::vector<fs::path> profileFiles;
    char pathInput[1024] = {};
    std::string listError;

    void refresh() {
        subdirs.clear();
        profileFiles.clear();
        listError.clear();
        try {
            for (const auto& entry : fs::directory_iterator(currentDir, fs::directory_options::skip_permission_denied)) {
                const auto& p = entry.path();
                std::error_code ec;
                if (entry.is_directory(ec) && !ec) {
                    subdirs.push_back(p);
                } else if (entry.is_regular_file(ec) && !ec) {
                    const std::string name = p.filename().string();
                    const std::string suffix = ".profile.json";
                    // Matches both "<name>.profile.json" (the naming convention used by
                    // production profiles) and a bare "profile.json" (used by fixtures/
                    // and presumably valid too - it's just ".profile.json" with an empty
                    // <name> prefix).
                    const bool matches = name == "profile.json" ||
                                         (name.size() > suffix.size() &&
                                          name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
                    if (matches) profileFiles.push_back(p);
                }
            }
        } catch (const fs::filesystem_error& e) {
            listError = e.what();
        }
        std::sort(subdirs.begin(), subdirs.end());
        std::sort(profileFiles.begin(), profileFiles.end());
        std::snprintf(pathInput, sizeof(pathInput), "%s", currentDir.string().c_str());
    }

    void setDir(const fs::path& dir) {
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(dir, ec);
        currentDir = (ec || canon.empty()) ? dir : canon;
        refresh();
    }
};

struct AppContext {
    fpe::PatchWorkspace workspace;
    AppState state = AppState::MainMenu;
    FileBrowserState browser;
    std::string errorMessage; // non-empty => error popup is showing
};

void tryLoadProfile(AppContext& ctx, const fs::path& file) {
    fpe::PatchWorkspace newWorkspace;
    try {
        newWorkspace.load(file);
    } catch (const std::exception& e) {
        ctx.errorMessage = "読み込みに失敗しました:\n" + file.string() + "\n\n" + e.what();
        return;
    }
    ctx.workspace = std::move(newWorkspace);
    ctx.state = AppState::Outline;
}

void renderMainMenu(AppContext& ctx) {
    ImGui::TextUnformatted("FITOM_X Patch Editor");
    ImGui::Separator();
    ImGui::Spacing();

    const ImVec2 buttonSize(260, 0);

    if (ImGui::Button("プロファイル読み込み", buttonSize)) {
        ctx.browser.setDir(fs::current_path());
        ctx.state = AppState::FileBrowser;
    }

    ImGui::BeginDisabled();
    ImGui::Button("新規プロファイル作成", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("未実装 (次回以降)");

    ImGui::BeginDisabled();
    ImGui::Button("プロファイル削除", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("未実装 (次回以降)");
}

void renderFileBrowser(AppContext& ctx) {
    FileBrowserState& b = ctx.browser;

    ImGui::TextUnformatted("プロファイル読み込み - *.profile.json を選択 (ダブルクリック)");
    ImGui::Separator();

    ImGui::TextUnformatted("パス:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-80);
    const bool enterPressed = ImGui::InputText("##pathinput", b.pathInput, sizeof(b.pathInput),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("移動") || enterPressed) {
        fs::path p(b.pathInput);
        std::error_code ec;
        if (fs::is_directory(p, ec) && !ec) {
            b.setDir(p);
        } else {
            ctx.errorMessage = "フォルダが見つかりません:\n" + p.string();
        }
    }

    if (!b.listError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", b.listError.c_str());
    }

    ImGui::Separator();

    ImGui::BeginChild("filelist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);

    if (b.currentDir.has_parent_path() && b.currentDir != b.currentDir.parent_path()) {
        if (ImGui::Selectable("../ (上へ)", false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            b.setDir(b.currentDir.parent_path());
        }
    }

    for (const auto& d : b.subdirs) {
        const std::string label = "[D] " + d.filename().string();
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            b.setDir(d);
        }
    }

    for (const auto& f : b.profileFiles) {
        const std::string label = f.filename().string();
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            tryLoadProfile(ctx, f);
        }
    }

    if (b.subdirs.empty() && b.profileFiles.empty()) {
        ImGui::TextDisabled("(このフォルダに *.profile.json はありません)");
    }

    ImGui::EndChild();

    if (ImGui::Button("戻る (メニュー)")) {
        ctx.state = AppState::MainMenu;
    }
}

void renderToneLayer(int index, const fpe::ToneLayer& layer) {
    const std::string vt = fpe::voicePatchTypeToString(layer.voice_patch_type);
    ImGui::BulletText("ToneLayer %d: %s hw_bank=%d hw_prog=%d note=[%d-%d] transpose=%d pan=%d%s",
                       index, vt.c_str(), layer.hw_bank, layer.hw_prog, layer.note_range_lo,
                       layer.note_range_hi, layer.transpose, layer.pan_offset,
                       layer.enabled ? "" : " (disabled)");
}

void renderOutline(AppContext& ctx) {
    fpe::PatchWorkspace& ws = ctx.workspace;

    ImGui::Text("プロファイル: %s", ws.profile().profile_name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("閉じる")) {
        ctx.workspace = fpe::PatchWorkspace{};
        ctx.state = AppState::MainMenu;
        return;
    }
    ImGui::Separator();

    if (!ws.warnings().empty()) {
        if (ImGui::TreeNode("warnings", "警告 (%zu件)", ws.warnings().size())) {
            for (const auto& w : ws.warnings()) ImGui::TextWrapped("%s", w.c_str());
            ImGui::TreePop();
        }
        ImGui::Separator();
    }

    ImGui::BeginChild("outline", ImVec2(0, 0), true);

    if (ImGui::TreeNode("native", "ネイティブパッチバンク (%zu)", ws.nativePatchBanks().size())) {
        for (auto& bank : ws.nativePatchBanks()) {
            ImGui::PushID(&bank);
            if (ImGui::TreeNode("bank", "[bank %d] %s (%zu patches)", bank.bankIndex, bank.name.c_str(),
                                 bank.patches.size())) {
                for (auto& patch : bank.patches) {
                    ImGui::PushID(&patch);
                    if (ImGui::TreeNode("patch", "[prog %d] %s (%zu layers)", patch.prog, patch.name.c_str(),
                                         patch.layers.size())) {
                        for (size_t i = 0; i < patch.layers.size(); ++i) {
                            renderToneLayer(static_cast<int>(i), patch.layers[i]);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("perf", "パフォーマンスバンク (%zu)", ws.performanceBanks().size())) {
        for (auto& bank : ws.performanceBanks()) {
            ImGui::PushID(&bank);
            if (ImGui::TreeNode("bank", "[bank %d] %s (%zu patches)", bank.bankIndex, bank.name.c_str(),
                                 bank.patches.size())) {
                for (auto& patch : bank.patches) {
                    ImGui::BulletText("[prog %d] %s", patch.prog, patch.name.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("device", "デバイスパッチバンク (%zu)", ws.deviceBanks().size())) {
        for (auto& bank : ws.deviceBanks()) {
            ImGui::PushID(&bank);
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            if (ImGui::TreeNode("bank", "[%s bank %d] %s (%zu patches)", groupStr.c_str(), bank.bankIndex,
                                 bank.name.c_str(), bank.patches.size())) {
                for (auto& patch : bank.patches) {
                    std::string label = "[prog " + std::to_string(patch.prog) + "] " + patch.name;
                    if (patch.sw_bank >= 0 && patch.sw_prog >= 0) {
                        label += " (sw_bank=" + std::to_string(patch.sw_bank) +
                                 " sw_prog=" + std::to_string(patch.sw_prog);
                        auto* sw = ws.resolvePerformancePatch(patch.sw_bank, patch.sw_prog);
                        label += sw ? (" -> " + sw->name + ")") : std::string(" -> ?)");
                    }
                    ImGui::BulletText("%s", label.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("samplezone", "サンプルゾーンバンク (%zu)", ws.sampleZoneBanks().size())) {
        for (auto& bank : ws.sampleZoneBanks()) {
            ImGui::PushID(&bank);
            const std::string groupStr = fpe::voicePatchTypeToString(bank.voicePatchType);
            if (ImGui::TreeNode("bank", "[%s bank %d] %s (%zu patches)", groupStr.c_str(), bank.bankIndex,
                                 bank.name.c_str(), bank.patches.size())) {
                for (auto& patch : bank.patches) {
                    ImGui::BulletText("[prog %d] %s (%zu zones)", patch.prog, patch.name.c_str(),
                                       patch.zones.size());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("drum", "ドラムキットマップ (%zu)", ws.drumKits().size())) {
        for (auto& kit : ws.drumKits()) {
            ImGui::PushID(&kit);
            const char* typeStr = (kit.type == fpe::DrumKitType::Routed) ? "routed" : "direct";
            if (kit.type == fpe::DrumKitType::Routed) {
                if (ImGui::TreeNode("kit", "[prog %d] %s (%s, %zu notes)", kit.prog, kit.name.c_str(), typeStr,
                                     kit.notes.size())) {
                    for (auto& note : kit.notes) {
                        ImGui::BulletText("note %d: %s -> play_note %d", note.note, note.name.c_str(),
                                           note.play_note);
                    }
                    ImGui::TreePop();
                }
            } else {
                ImGui::BulletText("[prog %d] %s (%s, note %d-%d -> patch_bank=%d patch_prog=%d)", kit.prog,
                                   kit.name.c_str(), typeStr, kit.note_min, kit.note_max, kit.patch_bank,
                                   kit.patch_prog);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::EndChild();
}

void renderErrorPopup(AppContext& ctx) {
    if (ctx.errorMessage.empty()) return;

    ImGui::OpenPopup("読み込みエラー");
    bool open = true;
    if (ImGui::BeginPopupModal("読み込みエラー", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", ctx.errorMessage.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ctx.errorMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open) ctx.errorMessage.clear();
}

} // namespace

int main(int argc, char** argv) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit() failed\n");
        return 1;
    }

    // OpenGL 3.0 + GLSL 130: the same baseline Dear ImGui's own GLFW+OpenGL3
    // example uses, for maximum portability across platforms/drivers.
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "FITOM_X Patch Editor", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow() failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "glewInit() failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadFonts(io);

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    AppContext ctx;

    // argv[1], if given, is the path to the profile that should already be
    // "open" on startup (see file-level comment above). Loading doesn't
    // touch the GL/ImGui state, so it's safe to do before the render loop
    // starts; tryLoadProfile() already handles success (-> Outline) and
    // failure (errorMessage set, state stays MainMenu) uniformly with the
    // FileBrowser pick path.
    if (argc > 1) {
        tryLoadProfile(ctx, fs::path(argv[1]));
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
        ImGui::Begin("FITOM_X Patch Editor");

        switch (ctx.state) {
            case AppState::MainMenu:
                renderMainMenu(ctx);
                break;
            case AppState::FileBrowser:
                renderFileBrowser(ctx);
                break;
            case AppState::Outline:
                renderOutline(ctx);
                break;
        }
        renderErrorPopup(ctx);

        ImGui::End();

        ImGui::Render();
        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
