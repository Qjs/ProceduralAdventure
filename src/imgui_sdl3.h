#ifndef IMGUI_SDL3_H
#define IMGUI_SDL3_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ImGui_SDL3_Init(SDL_Window *w, SDL_Renderer *r);
void ImGui_SDL3_Shutdown(void);
bool ImGui_SDL3_ProcessEvent(const SDL_Event *e);
void ImGui_SDL3_NewFrame(void);
void ImGui_SDL3_Render(SDL_Renderer *r);

#ifdef __cplusplus
}
#endif

#endif /* IMGUI_SDL3_H */
