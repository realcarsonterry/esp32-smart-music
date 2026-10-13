#ifndef MUSIC_CORE_H
#define MUSIC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIC_PROTOCOL_VERSION "1.0"
#define MUSIC_SAMPLE_RATE 32000
#define MUSIC_PPQ 480
#define MUSIC_TICKS_PER_BAR 1920
#define MUSIC_MAX_BARS 16
#define MUSIC_MAX_EVENTS 512
#define MUSIC_MAX_CHORDS 64
#define MUSIC_MAX_POLYPHONY 8
#define MUSIC_BLOCK_SIZE 256
#define MUSIC_MIDI_MIN 40
#define MUSIC_MIDI_MAX 84
#define MUSIC_BPM_MIN 40
#define MUSIC_BPM_MAX 220
#define MUSIC_DELAY_CAPACITY 512
#define MUSIC_RELEASE_SAMPLES 640
#define MUSIC_MAX_CHORD_NOTES 6
/* SPEC's decimal/hex values disagree; user confirmed the decimal value. */
#define MUSIC_ZERO_SEED_STATE UINT32_C(2164325893)

typedef enum {
    MUSIC_OK = 0,
    MUSIC_INVALID_BPM,
    MUSIC_INVALID_MUSIC_DATA,
    MUSIC_UNSUPPORTED_PROTOCOL_VERSION,
    MUSIC_INTERNAL_ERROR
} music_status_t;

typedef enum { MUSIC_MAJOR, MUSIC_MINOR } music_mode_t;
typedef enum { MUSIC_RULES, MUSIC_AI, MUSIC_MOCK } music_source_t;
typedef enum { MUSIC_ACCOMPANIMENT, MUSIC_MELODY } music_part_t;

typedef struct {
    uint32_t start_tick;
    uint32_t duration_tick;
    char symbol[16];
    size_t note_count;
    int midi_notes[MUSIC_MAX_CHORD_NOTES];
} music_chord_t;

typedef struct {
    char id[16];
    music_part_t part;
    int midi;
    int velocity;
    uint32_t start_tick;
    uint32_t duration_tick;
} music_event_t;

typedef struct {
    bool ok;
    char protocol_version[16];
    char request_id[65];
    music_source_t source;
    uint32_t seed;
    int bpm;
    int numerator;
    int denominator;
    int ppq;
    int bars;
    char root;
    music_mode_t mode;
    size_t chord_count;
    music_chord_t chords[MUSIC_MAX_CHORDS];
    size_t event_count;
    music_event_t events[MUSIC_MAX_EVENTS];
} music_data_t;

typedef struct { uint32_t state; } music_rng_t;
typedef struct music_context music_context;

music_status_t music_validate(const music_data_t *data);
const char *music_status_code(music_status_t status);

size_t music_context_bytes(void);
size_t music_context_alignment(void);
/* Caller owns suitably aligned memory for the lifetime of the context.
 * Initialization validates and copies data; it never allocates memory. */
music_status_t music_init(void *memory, size_t bytes, const music_data_t *data,
                          music_context **context);
uint32_t music_random_next(music_context *context);

/* Reset playback and each note's deterministic RNG to the copied input seed.
 * No allocation occurs in reset or rendering. Init also resets playback. */
music_status_t music_render_reset(music_context *context);
/* Returns samples written, up to capacity (any block size is allowed).
 * EOF is bars' sample length + MUSIC_RELEASE_SAMPLES; further calls return 0.
 * NULL context/output returns 0 without advancing. */
size_t music_render_next_pcm16(music_context *context, int16_t *output,
                               size_t capacity);

void music_rng_seed(music_rng_t *rng, uint32_t seed);
uint32_t music_rng_next(music_rng_t *rng);

/* Invalid BPM returns zero. Validated tick values fit in uint32_t.
 * Both conversions round to nearest, with exact halves rounded up. */
uint64_t music_tick_to_sample(uint32_t tick, uint32_t bpm);
uint64_t music_sample_to_tick(uint64_t sample, uint32_t bpm);

int16_t music_float_to_pcm16(float sample);
/* Writes canonical 44-byte RIFF/WAVE header and little-endian PCM data. */
music_status_t music_write_wav(const char *path, const int16_t *samples,
                              size_t sample_count);

#ifdef __cplusplus
}
#endif
#endif
