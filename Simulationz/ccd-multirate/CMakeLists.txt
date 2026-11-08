cmake_minimum_required(VERSION 3.20)
project(ccd_multirate LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# 2D rigid body: CCD vs multi-rate simulation
#   physcore  - физика без графики (статическая библиотека)
#   physbench - запуск экспериментов без окна, пишет CSV
#   physsim   - интерактивное приложение (SDL2 + Dear ImGui)
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_GUI "Собирать интерактивное приложение physsim (SDL2 + ImGui)" ON)

# Детерминированность: запрещаем компилятору переставлять операции с плавающей
# точкой (никакого -ffast-math) и сливать умножение со сложением в FMA —
# иначе результат может зависеть от флагов оптимизации.
# Флаги применяются только к нашим целям, а не к исходникам SDL/ImGui.
if(MSVC)
  # /utf-8: исходники в UTF-8 (русские комментарии), иначе MSVC читает их
  # в кодировке системы и выдаёт C4819 или портит строковые литералы.
  # _CRT_SECURE_NO_WARNINGS: fopen/sprintf стандартные и переносимые; MSVC-версии *_s
  # есть только в Windows, поэтому предупреждение C4996 отключаем.
  set(PHYS_FLAGS /fp:precise /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
else()
  set(PHYS_FLAGS -Wall -Wextra -ffp-contract=off)
endif()

# ----------------------------- physcore -----------------------------------
add_library(physcore STATIC
  src/core/world.cpp
  src/scenes/scenes.cpp
  src/scenes/main_scene.cpp
)
target_include_directories(physcore PUBLIC src)
target_compile_options(physcore PRIVATE ${PHYS_FLAGS})

# ----------------------------- physbench ----------------------------------
add_executable(physbench src/bench/bench_main.cpp src/bench/main_compare.cpp)
target_link_libraries(physbench PRIVATE physcore)
target_compile_options(physbench PRIVATE ${PHYS_FLAGS})

# ----------------------------- physsim ------------------------------------
if(BUILD_GUI)
  include(FetchContent)

  # SDL2 собирается из исходников статически: ничего не нужно ставить руками
  # ни на Windows, ни на Linux, ни на macOS.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST   OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE)

  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE)

  FetchContent_MakeAvailable(SDL2 imgui)

  add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
  target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static)

  add_executable(physsim
    src/app/main.cpp
    src/app/renderer.cpp)
  target_link_libraries(physsim PRIVATE physcore imgui_lib SDL2::SDL2main SDL2::SDL2-static)
  target_compile_options(physsim PRIVATE ${PHYS_FLAGS})
endif()
