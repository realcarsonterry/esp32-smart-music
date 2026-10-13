#include "music_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__FAST_MATH__)
#error "music_core requires strict floating point; fast-math is forbidden"
#endif

typedef struct {
    float delay[MUSIC_DELAY_CAPACITY];
    size_t length, cursor;
    float previous, ap_input, ap_output, ap_coefficient, decay, gain;
    uint64_t end;
    bool active;
    music_rng_t rng;
} music_voice;

struct music_context {
    music_data_t data;
    music_rng_t rng;
    music_voice voices[MUSIC_MAX_POLYPHONY];
    size_t order[MUSIC_MAX_EVENTS], next_event;
    uint32_t note_seeds[MUSIC_MAX_EVENTS];
    uint64_t starts[MUSIC_MAX_EVENTS], ends[MUSIC_MAX_EVENTS];
    uint64_t position, total;
    /* Finished voices contribute their complete release to this fixed ring,
     * allowing eight new notes at the same tick without stealing a tail. */
    float release[MUSIC_RELEASE_SAMPLES];
    size_t release_cursor;
};

static bool valid_string(const char *s, size_t capacity)
{
    return s[0] != '\0' && memchr(s, '\0', capacity) != NULL;
}

static bool valid_midi(int midi)
{
    return midi >= MUSIC_MIDI_MIN && midi <= MUSIC_MIDI_MAX;
}

static bool valid_span(uint32_t start, uint32_t duration, uint32_t total)
{
    /* Subtraction avoids overflow for untrusted uint32_t durations. */
    return start < total && duration > 0 && duration <= total - start;
}

static bool chord_has_pitch(const music_chord_t *chord, int midi)
{
    for (size_t i = 0; i < chord->note_count; ++i) {
        if (chord->midi_notes[i] % 12 == midi % 12) {
            return true;
        }
    }
    return false;
}

static bool accompaniment_valid(const music_data_t *data,
                                const music_event_t *event)
{
    uint32_t cursor = event->start_tick;
    const uint32_t end = cursor + event->duration_tick;
    /* Every tick in the event must be covered by a chord with its pitch
     * class. This also handles unsorted chords and chord changes/gaps. */
    while (cursor < end) {
        uint32_t covered_until = cursor;
        for (size_t i = 0; i < data->chord_count; ++i) {
            const music_chord_t *chord = &data->chords[i];
            const uint32_t chord_end = chord->start_tick + chord->duration_tick;
            if (chord->start_tick <= cursor && chord_end > covered_until &&
                chord_has_pitch(chord, event->midi)) {
                covered_until = chord_end;
            }
        }
        if (covered_until == cursor) {
            return false;
        }
        cursor = covered_until;
    }
    return true;
}

music_status_t music_validate(const music_data_t *data)
{
    if (data == NULL) {
        return MUSIC_INVALID_MUSIC_DATA;
    }
    if (!valid_string(data->protocol_version, sizeof data->protocol_version) ||
        strcmp(data->protocol_version, MUSIC_PROTOCOL_VERSION) != 0) {
        return MUSIC_UNSUPPORTED_PROTOCOL_VERSION;
    }
    if (data->bpm < MUSIC_BPM_MIN || data->bpm > MUSIC_BPM_MAX) {
        return MUSIC_INVALID_BPM;
    }
    if (!data->ok || !valid_string(data->request_id, sizeof data->request_id) ||
        (data->source != MUSIC_RULES && data->source != MUSIC_AI &&
         data->source != MUSIC_MOCK) ||
        data->numerator != 4 || data->denominator != 4 ||
        data->ppq != MUSIC_PPQ || data->bars < 1 || data->bars > MUSIC_MAX_BARS ||
        data->root < 'A' || data->root > 'G' ||
        (data->mode != MUSIC_MAJOR && data->mode != MUSIC_MINOR) ||
        data->chord_count > MUSIC_MAX_CHORDS ||
        data->event_count > MUSIC_MAX_EVENTS) {
        return MUSIC_INVALID_MUSIC_DATA;
    }
    const uint32_t total = (uint32_t)data->bars * MUSIC_TICKS_PER_BAR;
    for (size_t i = 0; i < data->chord_count; ++i) {
        const music_chord_t *chord = &data->chords[i];
        if (!valid_span(chord->start_tick, chord->duration_tick, total) ||
            !valid_string(chord->symbol, sizeof chord->symbol) ||
            chord->note_count < 1 || chord->note_count > MUSIC_MAX_CHORD_NOTES) {
            return MUSIC_INVALID_MUSIC_DATA;
        }
        for (size_t j = 0; j < chord->note_count; ++j) {
            if (!valid_midi(chord->midi_notes[j])) {
                return MUSIC_INVALID_MUSIC_DATA;
            }
        }
    }
    /* Validate all spans before adding any endpoints below. */
    for (size_t i = 0; i < data->event_count; ++i) {
        const music_event_t *event = &data->events[i];
        if (!valid_span(event->start_tick, event->duration_tick, total) ||
            !valid_string(event->id, sizeof event->id) ||
            (event->part != MUSIC_ACCOMPANIMENT && event->part != MUSIC_MELODY) ||
            !valid_midi(event->midi) || event->velocity < 1 || event->velocity > 127) {
            return MUSIC_INVALID_MUSIC_DATA;
        }
    }
    for (size_t i = 0; i < data->event_count; ++i) {
        const music_event_t *event = &data->events[i];
        size_t active = 0;
        for (size_t j = 0; j < data->event_count; ++j) {
            const music_event_t *other = &data->events[j];
            if (j < i && strcmp(event->id, other->id) == 0) {
                return MUSIC_INVALID_MUSIC_DATA;
            }
            /* Half-open intervals: note ends precede starts on the same tick. */
            if (other->start_tick <= event->start_tick &&
                event->start_tick < other->start_tick + other->duration_tick) {
                ++active;
            }
        }
        if (active > MUSIC_MAX_POLYPHONY ||
            (event->part == MUSIC_ACCOMPANIMENT &&
             !accompaniment_valid(data, event))) {
            return MUSIC_INVALID_MUSIC_DATA;
        }
    }
    return MUSIC_OK;
}

const char *music_status_code(music_status_t status)
{
    switch (status) {
    case MUSIC_OK: return "OK";
    case MUSIC_INVALID_BPM: return "INVALID_BPM";
    case MUSIC_INVALID_MUSIC_DATA: return "INVALID_MUSIC_DATA";
    case MUSIC_UNSUPPORTED_PROTOCOL_VERSION: return "UNSUPPORTED_PROTOCOL_VERSION";
    default: return "INTERNAL_ERROR";
    }
}

size_t music_context_bytes(void)
{
    return sizeof(music_context);
}

size_t music_context_alignment(void)
{
    return _Alignof(music_context);
}

music_status_t music_init(void *memory, size_t bytes, const music_data_t *data,
                          music_context **context)
{
    if (context == NULL) {
        return MUSIC_INVALID_MUSIC_DATA;
    }
    *context = NULL;
    if (memory == NULL || bytes < sizeof(music_context) ||
        (uintptr_t)memory % _Alignof(music_context) != 0) {
        return MUSIC_INVALID_MUSIC_DATA;
    }
    const music_status_t status = music_validate(data);
    if (status != MUSIC_OK) {
        return status;
    }
    music_context *initialized = memory;
    initialized->data = *data;
    music_rng_seed(&initialized->rng, data->seed);
    (void)music_render_reset(initialized);
    *context = initialized;
    return MUSIC_OK;
}

void music_rng_seed(music_rng_t *rng, uint32_t seed)
{
    rng->state = seed == 0 ? MUSIC_ZERO_SEED_STATE : seed;
}

uint32_t music_rng_next(music_rng_t *rng)
{
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t music_random_next(music_context *context)
{
    return music_rng_next(&context->rng);
}

music_status_t music_render_reset(music_context *context)
{
    if (context == NULL) {
        return MUSIC_INVALID_MUSIC_DATA;
    }
    memset(context->voices, 0, sizeof context->voices);
    memset(context->release, 0, sizeof context->release);
    context->position = 0;
    context->next_event = 0;
    context->release_cursor = 0;
    context->total = music_tick_to_sample(
        (uint32_t)context->data.bars * MUSIC_TICKS_PER_BAR,
        (uint32_t)context->data.bpm) + MUSIC_RELEASE_SAMPLES;
    music_rng_t notes;
    music_rng_seed(&notes, context->data.seed);
    /* One seed draw per event, in original array order. Playback and the
     * public random API cannot change any note's excitation sequence. */
    for (size_t i = 0; i < context->data.event_count; ++i) {
        const music_event_t *event = &context->data.events[i];
        context->note_seeds[i] = music_rng_next(&notes);
        context->starts[i] = music_tick_to_sample(event->start_tick,
                                                  (uint32_t)context->data.bpm);
        context->ends[i] = music_tick_to_sample(
            event->start_tick + event->duration_tick, (uint32_t)context->data.bpm);
        /* Stable insertion sort: unsorted input, original order for ties. */
        size_t j = i;
        while (j > 0 && context->starts[context->order[j - 1]] >
                        context->starts[i]) {
            context->order[j] = context->order[j - 1];
            --j;
        }
        context->order[j] = i;
    }
    return MUSIC_OK;
}

static bool trigger_voice(music_context *context, size_t index)
{
    const music_event_t *event = &context->data.events[index];
    const double frequency = 440.0 * pow(2.0, (event->midi - 69) / 12.0);
    const double delay = MUSIC_SAMPLE_RATE / frequency - 0.5;
    const size_t length = (size_t)delay;
    if (length < 2 || length > MUSIC_DELAY_CAPACITY) {
        return false;
    }
    for (size_t i = 0; i < MUSIC_MAX_POLYPHONY; ++i) {
        music_voice *voice = &context->voices[i];
        if (voice->active) {
            continue;
        }
        memset(voice, 0, sizeof *voice);
        const double fraction = delay - (double)length;
        voice->length = length;
        voice->ap_coefficient = (float)((1.0 - fraction) / (1.0 + fraction));
        voice->decay = (float)pow(0.996, 1.0 / (double)length);
        voice->gain = (float)(event->velocity / 127.0 * 0.12);
        voice->end = context->ends[index];
        music_rng_seed(&voice->rng, context->note_seeds[index]);
        for (size_t j = 0; j < length; ++j) {
            voice->delay[j] = (float)(music_rng_next(&voice->rng) & 0xffffu) /
                             32768.0f - 1.0f;
        }
        voice->active = true;
        return true;
    }
    return false; /* Validated intervals guarantee a free held-note slot. */
}

static float voice_next(music_voice *voice)
{
    const float value = voice->delay[voice->cursor];
    const float average = 0.5f * (value + voice->previous);
    voice->previous = value;
    const float filtered = voice->ap_coefficient * average + voice->ap_input -
                           voice->ap_coefficient * voice->ap_output;
    voice->ap_input = average;
    voice->ap_output = filtered;
    voice->delay[voice->cursor] = voice->decay * filtered;
    voice->cursor = (voice->cursor + 1) % voice->length;
    return value * voice->gain;
}

size_t music_render_next_pcm16(music_context *context, int16_t *output,
                               size_t capacity)
{
    if (context == NULL || output == NULL) {
        return 0;
    }
    size_t written = 0;
    while (written < capacity && context->position < context->total) {
        for (size_t i = 0; i < MUSIC_MAX_POLYPHONY; ++i) {
            music_voice *voice = &context->voices[i];
            if (voice->active && context->position >= voice->end) {
                /* Compute exactly 640 future samples before freeing the slot.
                 * The gain is 1 at note-off, 1/640 on the last tail sample. */
                for (size_t j = 0; j < MUSIC_RELEASE_SAMPLES; ++j) {
                    const float envelope = (float)(MUSIC_RELEASE_SAMPLES - j) /
                                           MUSIC_RELEASE_SAMPLES;
                    const size_t slot = (context->release_cursor + j) %
                                        MUSIC_RELEASE_SAMPLES;
                    context->release[slot] += voice_next(voice) * envelope;
                }
                voice->active = false;
            }
        }
        while (context->next_event < context->data.event_count &&
               context->starts[context->order[context->next_event]] ==
               context->position) {
            if (!trigger_voice(context, context->order[context->next_event])) {
                return written;
            }
            ++context->next_event;
        }
        float mixed = context->release[context->release_cursor];
        context->release[context->release_cursor] = 0.0f;
        for (size_t i = 0; i < MUSIC_MAX_POLYPHONY; ++i) {
            if (context->voices[i].active) {
                mixed += voice_next(&context->voices[i]);
            }
        }
        if (mixed > 0.98f) mixed = 0.98f;
        if (mixed < -0.98f) mixed = -0.98f;
        output[written++] = music_float_to_pcm16(mixed);
        context->release_cursor = (context->release_cursor + 1) %
                                  MUSIC_RELEASE_SAMPLES;
        ++context->position;
    }
    return written;
}

uint64_t music_tick_to_sample(uint32_t tick, uint32_t bpm)
{
    if (bpm < MUSIC_BPM_MIN || bpm > MUSIC_BPM_MAX) {
        return 0;
    }
    const uint64_t denominator = (uint64_t)bpm * MUSIC_PPQ;
    return ((uint64_t)tick * MUSIC_SAMPLE_RATE * 60 + denominator / 2) /
           denominator;
}

uint64_t music_sample_to_tick(uint64_t sample, uint32_t bpm)
{
    if (bpm < MUSIC_BPM_MIN || bpm > MUSIC_BPM_MAX) {
        return 0;
    }
    const uint64_t rate = (uint64_t)MUSIC_SAMPLE_RATE * 60;
    const uint64_t factor = (uint64_t)bpm * MUSIC_PPQ;
    /* Quotient/remainder avoids overflow even for UINT64_MAX samples. */
    return (sample / rate) * factor +
           ((sample % rate) * factor + rate / 2) / rate;
}

int16_t music_float_to_pcm16(float sample)
{
    if (!isfinite(sample)) {
        return 0;
    }
    const double scaled = (double)sample * 32767.0;
    if (scaled >= 32767.0) {
        return INT16_MAX;
    }
    if (scaled <= -32768.0) {
        return INT16_MIN;
    }
    return (int16_t)round(scaled);
}

static void put_u16(unsigned char *bytes, uint16_t value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

music_status_t music_write_wav(const char *path, const int16_t *samples,
                              size_t sample_count)
{
    if (path == NULL || (samples == NULL && sample_count != 0) ||
        sample_count > (UINT32_MAX - UINT32_C(36)) / 2) {
        return MUSIC_INVALID_MUSIC_DATA;
    }
    unsigned char header[44] = {0};
    const uint32_t data_bytes = (uint32_t)sample_count * 2;
    memcpy(header, "RIFF", 4);
    put_u32(header + 4, 36 + data_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_u32(header + 16, 16);
    put_u16(header + 20, 1);
    put_u16(header + 22, 1);
    put_u32(header + 24, MUSIC_SAMPLE_RATE);
    put_u32(header + 28, MUSIC_SAMPLE_RATE * 2);
    put_u16(header + 32, 2);
    put_u16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put_u32(header + 40, data_bytes);
    FILE *file = NULL;
#ifdef _MSC_VER
    if (fopen_s(&file, path, "wb") != 0) {
        return MUSIC_INTERNAL_ERROR;
    }
#else
    file = fopen(path, "wb");
#endif
    if (file == NULL) {
        return MUSIC_INTERNAL_ERROR;
    }
    bool success = fwrite(header, 1, sizeof header, file) == sizeof header;
    for (size_t i = 0; success && i < sample_count; ++i) {
        unsigned char bytes[2];
        put_u16(bytes, (uint16_t)samples[i]);
        success = fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes;
    }
    if (fclose(file) != 0) {
        success = false;
    }
    return success ? MUSIC_OK : MUSIC_INTERNAL_ERROR;
}
