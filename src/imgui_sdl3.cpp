#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_sdl3.h"

extern "C" {

bool ImGui_SDL3_Init(SDL_Window *w, SDL_Renderer *r)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(w, r))
        return false;
    if (!ImGui_ImplSDLRenderer3_Init(r))
        return false;
    return true;
}

void ImGui_SDL3_Shutdown(void)
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

bool ImGui_SDL3_ProcessEvent(const SDL_Event *e)
{
    return ImGui_ImplSDL3_ProcessEvent(e);
}

void ImGui_SDL3_NewFrame(void)
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGui_SDL3_Render(SDL_Renderer *r)
{
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), r);
}

} /* extern "C" */
