#include "app.h"
#include <string.h>
#include <stdlib.h>

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

static s32 parse_seed(int argc, char *argv[]) {
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--seed") == 0) {
            return (s32)atoi(argv[i + 1]);
        }
    }
    return -1; // randomize
}

int main(int argc, char *argv[]) {
    s32 seed = parse_seed(argc, argv);

#ifdef __EMSCRIPTEN__
    if (!app_init(&app, "ProceduralAdventure", 1280, 720, seed))
        return 1;
    emscripten_set_main_loop(main_loop, 0, 1);
#else
    App app;
    if (!app_init(&app, "ProceduralAdventure", 1280, 720, seed))
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
