# imgui.cmake - Dear ImGui (docking branch), fetched at build time.
#
# Provides the static `imgui` library: core + SDL2 + Vulkan backends.
# The Vulkan backend goes through volk (IMGUI_IMPL_VULKAN_USE_VOLK, the
# loader this port already initializes) and dynamic rendering
# (VK_API_VERSION_1_3, no render pass object on our side).
include(FetchContent)

set(RBREW_IMGUI_GIT_REPOSITORY "https://github.com/ocornut/imgui.git" CACHE STRING "ImGui git repository")
set(RBREW_IMGUI_GIT_TAG "docking" CACHE STRING "ImGui git branch/tag (docking adds window docking)")

FetchContent_Declare(imgui
    GIT_REPOSITORY ${RBREW_IMGUI_GIT_REPOSITORY}
    GIT_TAG ${RBREW_IMGUI_GIT_TAG}
    GIT_SHALLOW TRUE)
# No FetchContent_MakeAvailable: upstream ships no CMakeLists, the static
# library below is the build definition.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    # SDL_GL_* fallbacks: the backend references them, the Vulkan-only SDL
    # profile has no OpenGL (see the file).
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/imgui_sdl2_gl_stubs.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static Vulkan::Headers volk::volk)
