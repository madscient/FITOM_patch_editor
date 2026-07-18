cmake_minimum_required(VERSION 3.20)
project(fitom_patch_editor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(FPE_BUILD_TESTS "Build fpe smoke tests" ON)
option(FPE_BUILD_GUI "Build the Dear ImGui GUI shell (apps/gui)" ON)

if(MSVC)
  # /utf-8: source files (including this project's Japanese comments and
  # string literals) are UTF-8; without this, MSVC falls back to the
  # active Windows code page (e.g. Shift-JIS/932 on a Japanese system
  # locale) to interpret them, which both misdisplays the comments
  # (C4819) and can corrupt multi-byte UTF-8 string literals outright
  # (a byte sequence that happens to decode as backslash/quote under
  # Shift-JIS). Applies project-wide via add_compile_options rather than
  # per-target since every target here compiles files that #include
  # fpe_data's Japanese-commented headers.
  add_compile_options(/utf-8)
endif()

# All third-party dependencies (nlohmann-json, imgui, glfw3, glew) are
# declared in vcpkg.json (manifest mode) and resolved via find_package()
# below. Configure with the vcpkg toolchain file, e.g.:
#   cmake --preset vcpkg   (see CMakePresets.json; requires VCPKG_ROOT)
# or manually:
#   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

find_package(nlohmann_json CONFIG REQUIRED)

# --- fpe_data: the JSON data-model / I/O library -------------------------
add_library(fpe_data STATIC
  src/VoicePatchType.cpp
  src/HwPatch.cpp
  src/SwPatch.cpp
  src/NativePatch.cpp
  src/DrumKit.cpp
  src/SampleZone.cpp
  src/PcmBank.cpp
  src/Profile.cpp
  src/PatchWorkspace.cpp
)

target_include_directories(fpe_data PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(fpe_data PUBLIC nlohmann_json::nlohmann_json)

target_compile_features(fpe_data PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(fpe_data PRIVATE /W4 /permissive-)
else()
  target_compile_options(fpe_data PRIVATE -Wall -Wextra)
endif()

# --- smoke tests -----------------------------------------------------------
# fixtures dir is located by the test itself (upward search for
# fixtures/profile.json starting at the process's current working
# directory), so no compile-time path injection is needed. Run
# fpe_smoke_test / ctest from anywhere inside the project tree.
if(FPE_BUILD_TESTS)
  enable_testing()
  add_executable(fpe_smoke_test tests/smoke_test.cpp)
  target_link_libraries(fpe_smoke_test PRIVATE fpe_data)
  add_test(NAME fpe_smoke_test COMMAND fpe_smoke_test
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# --- fitom_patch_editor_gui: Dear ImGui (GLFW + OpenGL3) shell -------------
# Currently just the windowing/render-loop shell (see apps/gui/main.cpp) -
# the patch browser/editor UI itself is built on top of fpe::PatchWorkspace
# in later work.
if(FPE_BUILD_GUI)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(GLEW REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(imgui CONFIG REQUIRED)

  add_executable(fitom_patch_editor_gui apps/gui/main.cpp apps/gui/MidiPipeClient.cpp apps/gui/BmpLoader.cpp)
  target_link_libraries(fitom_patch_editor_gui PRIVATE
    fpe_data
    imgui::imgui
    glfw
    GLEW::GLEW
    OpenGL::GL
  )
  target_compile_definitions(fitom_patch_editor_gui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLEW)
  target_compile_features(fitom_patch_editor_gui PRIVATE cxx_std_17)

  # assets/ (e.g. ALG connection-diagram .bmp files) is located the same way
  # fixtures/ is for the tests - upward search from the process's current
  # working directory for an assets/ marker file - so it's copied next to
  # the executable rather than baked in as a compile-time absolute path
  # (which wouldn't survive moving the build to another machine). Explorer
  # sets CWD to the exe's own directory on double-click, so this Just Works
  # for a normal launch; running from a different CWD still finds it via
  # the upward search as long as it's somewhere above the exe.
  add_custom_command(TARGET fitom_patch_editor_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:fitom_patch_editor_gui>/assets")
endif()
