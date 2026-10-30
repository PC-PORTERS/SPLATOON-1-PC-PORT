// imgui_sdl2_gl_stubs.cpp - OpenGL entry points ImGui's SDL2 backend calls.
//
// imgui_impl_sdl2.cpp references SDL_GL_* unconditionally: the multi-viewport
// code paths and the window-framebuffer fallback of
// ImGui_ImplSDL2_GetWindowSizeAndFramebufferScale(). The port links the
// audited Vulkan-only SDL2 profile, which is built without SDL_VIDEO_OPENGL,
// so those symbols do not exist and the link failed.
//
// The port never enables ImGuiConfigFlags_ViewportsEnable and every window it
// creates carries SDL_WINDOW_VULKAN, so the viewport/GL paths are unreachable;
// the definitions below only satisfy the linker and fail loudly (SDL_SetError)
// if that assumption ever changes.
#include <SDL.h>

extern "C" {

void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h) {
    // Fallback used by ImGui for non-Vulkan windows: the window size is the
    // drawable size at scale 1, which is what a Vulkan window reports anyway.
    if (w) *w = 0;
    if (h) *h = 0;
    SDL_GetWindowSize(window, w, h);
}

SDL_GLContext SDL_GL_GetCurrentContext(void) { return nullptr; }

int SDL_GL_SetAttribute(SDL_GLattr attr, int value) {
    (void)attr;
    (void)value;
    return SDL_SetError("this build of SDL2 has no OpenGL support");
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window* window) {
    (void)window;
    SDL_SetError("this build of SDL2 has no OpenGL support");
    return nullptr;
}

int SDL_GL_MakeCurrent(SDL_Window* window, SDL_GLContext context) {
    (void)window;
    (void)context;
    return SDL_SetError("this build of SDL2 has no OpenGL support");
}

void SDL_GL_DeleteContext(SDL_GLContext context) { (void)context; }

int SDL_GL_SetSwapInterval(int interval) {
    (void)interval;
    return SDL_SetError("this build of SDL2 has no OpenGL support");
}

void SDL_GL_SwapWindow(SDL_Window* window) { (void)window; }

}  // extern "C"
