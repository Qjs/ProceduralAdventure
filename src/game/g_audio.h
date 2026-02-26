#ifndef G_AUDIO_H
#define G_AUDIO_H

#include <stdbool.h>

typedef enum {
    SFX_MELEE_HIT,
    SFX_ARROW_SHOT,
    SFX_MAGIC_CAST,
    SFX_HEAL,
    SFX_DEATH,
    SFX_ORB_PICKUP,
    SFX_PORTAL_OPEN,
    SFX_LEVEL_COMPLETE,
    SFX_COUNT
} SoundID;

bool g_audio_init(void);
void g_audio_shutdown(void);
void g_audio_play(SoundID id);
void g_audio_set_volume(float vol);
float g_audio_get_volume(void);
void g_audio_set_muted(bool muted);
bool g_audio_get_muted(void);

#endif
