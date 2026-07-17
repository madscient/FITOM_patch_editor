cmake_minimum_required(VERSION 3.20)
project(fitom_patch_editor_data CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(FPE_BUILD_TESTS "Build fpe smoke tests" ON)

# --- nlohmann/json (header-only) ---------------------------------------
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# --- fpe_data: the JSON data-model / I/O library -------------------------
add_library(fpe_data STATIC
  src/VoicePatchType.cpp
  src/HwPatch.cpp
  src/SwPatch.cpp
  src/NativePatch.cpp
  src/DrumKit.cpp
  src/SampleZone.cpp
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
