#include "app.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static App app;

static void main_loop(void) {
    app_process_events(&app);
    app_update(&app);
    app_render(&app);
    if (!app.running) {
        emscripten_cancel_main_loop();
        app_shutdown(&app);
    }
}
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

#ifdef __EMSCRIPTEN__
    if (!app_init(&app, "ProceduralAdventure", 1280, 720))
        return 1;
    emscripten_set_main_loop(main_loop, 0, 1);
#else
    App app;
    if (!app_init(&app, "ProceduralAdventure", 1280, 720))
        return 1;

    while (app.running) {
        app_process_events(&app);
        app_update(&app);
        app_render(&app);
    }

    app_shutdown(&app);
#endif

    return 0;
}
