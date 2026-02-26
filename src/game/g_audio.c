#include "g_audio.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE   44100
#define MAX_VOICES    8

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    Sint16 *samples;
    Uint32  length;  /* number of samples */
} SoundBuffer;

typedef struct {
    const SoundBuffer *buf;
    Uint32             pos;
    bool               active;
} Voice;

static SoundBuffer  s_buffers[SFX_COUNT];
static Voice        s_voices[MAX_VOICES];
static SDL_AudioStream *s_stream;
static float        s_volume = 0.5f;
static bool         s_muted;
static bool         s_initialized;

/* ---- Synthesis primitives ---- */

static float synth_sine(float freq, float t) {
    return sinf(2.0f * (float)M_PI * freq * t);
}

static float synth_noise(void) {
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

static float envelope(float t, float attack, float decay,
                      float sustain_level, float release, float duration) {
    if (t < 0.0f) return 0.0f;
    if (t < attack) return t / attack;
    t -= attack;
    if (t < decay) return 1.0f - (1.0f - sustain_level) * (t / decay);
    t -= decay;
    float sustain_dur = duration - attack - decay - release;
    if (sustain_dur < 0.0f) sustain_dur = 0.0f;
    if (t < sustain_dur) return sustain_level;
    t -= sustain_dur;
    if (t < release) return sustain_level * (1.0f - t / release);
    return 0.0f;
}

static void alloc_buffer(SoundBuffer *buf, float duration_sec) {
    buf->length = (Uint32)(SAMPLE_RATE * duration_sec);
    buf->samples = (Sint16 *)calloc(buf->length, sizeof(Sint16));
}

static void write_sample(SoundBuffer *buf, Uint32 i, float val) {
    if (i >= buf->length) return;
    float clamped = val;
    if (clamped >  1.0f) clamped =  1.0f;
    if (clamped < -1.0f) clamped = -1.0f;
    /* Mix (add) into existing sample with clamp */
    float existing = (float)buf->samples[i] / 32767.0f;
    float mixed = existing + clamped;
    if (mixed >  1.0f) mixed =  1.0f;
    if (mixed < -1.0f) mixed = -1.0f;
    buf->samples[i] = (Sint16)(mixed * 32767.0f);
}

/* ---- Sound generators ---- */

static void generate_sfx_melee_hit(SoundBuffer *buf) {
    float dur = 0.15f;
    alloc_buffer(buf, dur);
    for (Uint32 i = 0; i < buf->length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = envelope(t, 0.002f, 0.05f, 0.3f, 0.08f, dur);
        float noise = synth_noise() * 0.7f;
        float thump = synth_sine(80.0f, t) * 0.5f;
        write_sample(buf, i, (noise + thump) * env);
    }
}

static void generate_sfx_arrow_shot(SoundBuffer *buf) {
    float dur = 0.2f;
    alloc_buffer(buf, dur);
    for (Uint32 i = 0; i < buf->length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = envelope(t, 0.005f, 0.08f, 0.2f, 0.1f, dur);
        /* Sine sweep from 1200 Hz down to 400 Hz */
        float freq = 1200.0f - 800.0f * (t / dur);
        float sine = synth_sine(freq, t) * 0.5f;
        float whisper = synth_noise() * 0.15f * envelope(t, 0.001f, 0.04f, 0.0f, 0.0f, 0.05f);
        write_sample(buf, i, (sine + whisper) * env);
    }
}

static void generate_sfx_magic_cast(SoundBuffer *buf) {
    float dur = 0.45f;
    alloc_buffer(buf, dur);
    for (Uint32 i = 0; i < buf->length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = envelope(t, 0.03f, 0.1f, 0.6f, 0.25f, dur);
        float vibrato = 1.0f + 0.02f * synth_sine(6.0f, t);
        float chord = synth_sine(440.0f * vibrato, t) * 0.35f
                    + synth_sine(554.0f * vibrato, t) * 0.25f
                    + synth_sine(659.0f * vibrato, t) * 0.2f;
        write_sample(buf, i, chord * env);
    }
}

static void generate_sfx_heal(SoundBuffer *buf) {
    float dur = 0.4f;
    alloc_buffer(buf, dur);
    /* 3-note rising arpeggio: C5, E5, G5 */
    float notes[] = {523.25f, 659.25f, 783.99f};
    float note_dur = dur / 3.0f;
    for (int n = 0; n < 3; n++) {
        float start = n * note_dur;
        for (Uint32 i = 0; i < buf->length; i++) {
            float t = (float)i / SAMPLE_RATE;
            float local_t = t - start;
            if (local_t < 0.0f || local_t > note_dur) continue;
            float env = envelope(local_t, 0.01f, 0.04f, 0.5f, 0.08f, note_dur);
            float val = synth_sine(notes[n], t) * 0.35f * env;
            write_sample(buf, i, val);
        }
    }
}

static void generate_sfx_death(SoundBuffer *buf) {
    float dur = 0.5f;
    alloc_buffer(buf, dur);
    for (Uint32 i = 0; i < buf->length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = envelope(t, 0.005f, 0.15f, 0.3f, 0.3f, dur);
        /* Pitch drops from 200 to 50 Hz */
        float freq = 200.0f - 150.0f * (t / dur);
        float tone = synth_sine(freq, t) * 0.4f;
        float noise = synth_noise() * 0.4f * envelope(t, 0.002f, 0.08f, 0.1f, 0.2f, dur * 0.5f);
        write_sample(buf, i, (tone + noise) * env);
    }
}

static void generate_sfx_orb_pickup(SoundBuffer *buf) {
    float dur = 0.25f;
    alloc_buffer(buf, dur);
    /* Quick ascending 3-note chime: E6, G#6, B6 */
    float notes[] = {1318.5f, 1661.2f, 1975.5f};
    float note_dur = dur / 3.0f;
    for (int n = 0; n < 3; n++) {
        float start = n * note_dur;
        for (Uint32 i = 0; i < buf->length; i++) {
            float t = (float)i / SAMPLE_RATE;
            float local_t = t - start;
            if (local_t < 0.0f || local_t > note_dur) continue;
            float env = envelope(local_t, 0.005f, 0.03f, 0.4f, 0.04f, note_dur);
            float val = synth_sine(notes[n], t) * 0.3f * env;
            write_sample(buf, i, val);
        }
    }
}

static void generate_sfx_portal_open(SoundBuffer *buf) {
    float dur = 0.8f;
    alloc_buffer(buf, dur);
    for (Uint32 i = 0; i < buf->length; i++) {
        float t = (float)i / SAMPLE_RATE;
        float env = envelope(t, 0.1f, 0.1f, 0.7f, 0.3f, dur);
        /* Low rumble */
        float rumble = synth_sine(55.0f, t) * 0.3f + synth_sine(65.0f, t) * 0.2f;
        /* Rising shimmer (layered sines sweeping up) */
        float shimmer_freq = 400.0f + 800.0f * (t / dur);
        float shimmer = synth_sine(shimmer_freq, t) * 0.15f
                      + synth_sine(shimmer_freq * 1.5f, t) * 0.1f;
        float shimmer_env = t / dur; /* fades in */
        write_sample(buf, i, (rumble + shimmer * shimmer_env) * env);
    }
}

static void generate_sfx_level_complete(SoundBuffer *buf) {
    float dur = 1.0f;
    alloc_buffer(buf, dur);
    /* Triumphant ascending chord sequence: C4-E4-G4 then E4-G#4-B4 then C5 */
    typedef struct { float freq; float start; float length; float amp; } Note;
    Note notes[] = {
        {261.6f, 0.00f, 0.30f, 0.30f},  /* C4 */
        {329.6f, 0.05f, 0.30f, 0.25f},  /* E4 */
        {392.0f, 0.10f, 0.30f, 0.25f},  /* G4 */
        {329.6f, 0.35f, 0.30f, 0.25f},  /* E4 */
        {415.3f, 0.40f, 0.30f, 0.25f},  /* G#4 */
        {493.9f, 0.45f, 0.30f, 0.25f},  /* B4 */
        {523.3f, 0.70f, 0.30f, 0.35f},  /* C5 */
        {659.3f, 0.72f, 0.28f, 0.20f},  /* E5 */
    };
    int num_notes = (int)(sizeof(notes) / sizeof(notes[0]));
    for (int n = 0; n < num_notes; n++) {
        for (Uint32 i = 0; i < buf->length; i++) {
            float t = (float)i / SAMPLE_RATE;
            float local_t = t - notes[n].start;
            if (local_t < 0.0f || local_t > notes[n].length) continue;
            float env = envelope(local_t, 0.02f, 0.05f, 0.6f, 0.15f, notes[n].length);
            float val = synth_sine(notes[n].freq, t) * notes[n].amp * env;
            write_sample(buf, i, val);
        }
    }
}

/* ---- Audio callback feeds the stream ---- */

static void audio_generate_samples(Sint16 *output, int num_samples) {
    memset(output, 0, (size_t)num_samples * sizeof(Sint16));

    float effective_vol = s_muted ? 0.0f : s_volume;

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *voice = &s_voices[v];
        if (!voice->active) continue;

        const SoundBuffer *sb = voice->buf;
        for (int i = 0; i < num_samples; i++) {
            if (voice->pos >= sb->length) {
                voice->active = false;
                break;
            }
            float sample = (float)sb->samples[voice->pos] / 32767.0f;
            float existing = (float)output[i] / 32767.0f;
            float mixed = existing + sample * effective_vol;
            if (mixed >  1.0f) mixed =  1.0f;
            if (mixed < -1.0f) mixed = -1.0f;
            output[i] = (Sint16)(mixed * 32767.0f);
            voice->pos++;
        }
    }
}

/* Push audio in a periodic manner — called from the main loop indirectly
   via SDL's audio stream pull model. We use an audio stream with a callback. */

static void SDLCALL audio_stream_callback(void *userdata, SDL_AudioStream *stream,
                                           int additional_amount, int total_amount) {
    (void)userdata;
    (void)total_amount;

    if (additional_amount <= 0) return;

    int num_samples = additional_amount / (int)sizeof(Sint16);
    if (num_samples <= 0) return;

    Sint16 *buf = (Sint16 *)SDL_malloc((size_t)additional_amount);
    if (!buf) return;

    audio_generate_samples(buf, num_samples);
    SDL_PutAudioStreamData(stream, buf, additional_amount);
    SDL_free(buf);
}

/* ---- Public API ---- */

bool g_audio_init(void) {
    if (s_initialized) return true;

    /* Pre-render all sound buffers */
    generate_sfx_melee_hit(&s_buffers[SFX_MELEE_HIT]);
    generate_sfx_arrow_shot(&s_buffers[SFX_ARROW_SHOT]);
    generate_sfx_magic_cast(&s_buffers[SFX_MAGIC_CAST]);
    generate_sfx_heal(&s_buffers[SFX_HEAL]);
    generate_sfx_death(&s_buffers[SFX_DEATH]);
    generate_sfx_orb_pickup(&s_buffers[SFX_ORB_PICKUP]);
    generate_sfx_portal_open(&s_buffers[SFX_PORTAL_OPEN]);
    generate_sfx_level_complete(&s_buffers[SFX_LEVEL_COMPLETE]);

    /* Create audio stream with callback */
    SDL_AudioSpec spec;
    spec.freq = SAMPLE_RATE;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;

    s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &spec, audio_stream_callback, NULL);
    if (!s_stream) {
        SDL_Log("g_audio_init: failed to open audio stream: %s", SDL_GetError());
        return false;
    }

    SDL_ResumeAudioStreamDevice(s_stream);

    memset(s_voices, 0, sizeof(s_voices));
    s_initialized = true;
    return true;
}

void g_audio_shutdown(void) {
    if (!s_initialized) return;

    if (s_stream) {
        SDL_DestroyAudioStream(s_stream);
        s_stream = NULL;
    }

    for (int i = 0; i < SFX_COUNT; i++) {
        free(s_buffers[i].samples);
        s_buffers[i].samples = NULL;
        s_buffers[i].length = 0;
    }

    s_initialized = false;
}

void g_audio_play(SoundID id) {
    if (!s_initialized) return;
    if (id < 0 || id >= SFX_COUNT) return;
    if (!s_buffers[id].samples) return;

    /* Find a free voice */
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!s_voices[i].active) {
            s_voices[i].buf = &s_buffers[id];
            s_voices[i].pos = 0;
            s_voices[i].active = true;
            return;
        }
    }

    /* All voices busy — steal the oldest (highest position) */
    Uint32 max_pos = 0;
    int oldest = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (s_voices[i].pos > max_pos) {
            max_pos = s_voices[i].pos;
            oldest = i;
        }
    }
    s_voices[oldest].buf = &s_buffers[id];
    s_voices[oldest].pos = 0;
    s_voices[oldest].active = true;
}

void g_audio_set_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    s_volume = vol;
}

float g_audio_get_volume(void) {
    return s_volume;
}

void g_audio_set_muted(bool muted) {
    s_muted = muted;
}

bool g_audio_get_muted(void) {
    return s_muted;
}
