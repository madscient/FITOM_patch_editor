// FITOM_X Patch Editor - GUI shell.
//
// Owns the window/OpenGL context and the Dear ImGui context, and runs the
// main render loop. This file is intentionally just the windowing shell -
// it does not yet build any patch-browsing/editing UI. That UI is meant to
// be built on top of fpe::PatchWorkspace (see include/fpe/PatchWorkspace.h),
// which already models the full profile/bank/patch hierarchy and handles
// JSON load/save; this shell just proves the window + render loop + data
// layer link together correctly.
//
// Backend: GLFW (window/input) + OpenGL3 (rendering) + GLEW (GL function
// loading). All three, plus Dear ImGui itself and nlohmann/json, are
// resolved via vcpkg (see vcpkg.json / CMakePresets.json) - there is no
// vendored/submoduled third-party source in this repository.

#include <cstdio>

// GLEW must be included before any other header that may pull in the
// platform's own (older) OpenGL headers - including GLFW's.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "fpe/PatchWorkspace.h"

namespace {

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

} // namespace

int main(int, char**) {
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

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    // Not wired to any UI yet - just proves fpe_data links into the GUI
    // binary and is ready for the patch browser/editor to be built on it.
    fpe::PatchWorkspace workspace;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("FITOM_X Patch Editor");
        ImGui::TextUnformatted("GUI shell placeholder.");
        ImGui::TextUnformatted("Patch browser / editor UI not implemented yet.");
        if (workspace.profile().profile_name.empty()) {
            ImGui::TextUnformatted("No profile loaded.");
        }
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
