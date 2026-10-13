#include "music_core.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unlike assert(), these checks remain enabled in Release/NDEBUG builds. */
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

static music_data_t valid_data(void)
{
    /* Direct C equivalent of shared/fixtures/valid-one-bar.json. */
    music_data_t data = {0};
    data.ok = true;
    memcpy(data.protocol_version, "1.0", 4);
    memcpy(data.request_id, "fixture-one-bar", 16);
    data.source = MUSIC_RULES;
    data.seed = 1;
    data.bpm = 96;
    data.numerator = 4;
    data.denominator = 4;
    data.ppq = 480;
    data.bars = 1;
    data.root = 'C';
    data.mode = MUSIC_MAJOR;
    data.chord_count = 1;
    data.chords[0] = (music_chord_t){
        .start_tick = 0, .duration_tick = 1920, .symbol = "C",
        .note_count = 4, .midi_notes = {48, 52, 55, 60}
    };
    data.event_count = 5;
    data.events[0] = (music_event_t){"a0", MUSIC_ACCOMPANIMENT, 48, 90, 0, 400};
    data.events[1] = (music_event_t){"a1", MUSIC_ACCOMPANIMENT, 52, 90, 10, 400};
    data.events[2] = (music_event_t){"a2", MUSIC_ACCOMPANIMENT, 55, 90, 20, 400};
    data.events[3] = (music_event_t){"a3", MUSIC_ACCOMPANIMENT, 60, 90, 30, 400};
    data.events[4] = (music_event_t){"m0", MUSIC_MELODY, 64, 80, 0, 480};
    return data;
}

static int test_ticks(void)
{
    const struct { uint32_t tick, bpm; uint64_t sample; } vectors[] = {
        {0, 96, 0}, {480, 60, 32000}, {1920, 96, 80000}, {1, 96, 42}
    };
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; ++i) {
        CHECK(music_tick_to_sample(vectors[i].tick, vectors[i].bpm) ==
              vectors[i].sample);
        CHECK(music_sample_to_tick(vectors[i].sample, vectors[i].bpm) ==
              vectors[i].tick);
    }
    /* All supported tempos/ticks, including the final endpoint. */
    for (uint32_t bpm = 40; bpm <= 220; ++bpm) {
        for (uint32_t tick = 0; tick <= 16 * 1920; ++tick) {
            CHECK(music_sample_to_tick(music_tick_to_sample(tick, bpm), bpm)
                  == tick);
        }
        CHECK(music_sample_to_tick(music_tick_to_sample(UINT32_MAX, bpm), bpm)
              == UINT32_MAX);
    }
    CHECK(music_tick_to_sample(480, 0) == 0);
    CHECK(music_sample_to_tick(32000, 0) == 0);
    return 0;
}

static int test_rng(void)
{
    music_rng_t zero, a, b, other;
    music_rng_seed(&zero, 0);
    CHECK(zero.state != 0);
    CHECK(zero.state == UINT32_C(2164325893));
    /* Known xorshift32 outputs catch incorrect shift order/width. */
    const uint32_t expected[] = {270369, 67634689, 2647435461, 307599695,
                                 2398689233};
    music_rng_seed(&a, 1);
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
        CHECK(music_rng_next(&a) == expected[i]);
    }
    const uint32_t seeds[] = {0, 1, 2164325893, UINT32_MAX};
    for (size_t i = 0; i < sizeof seeds / sizeof seeds[0]; ++i) {
        music_rng_seed(&a, seeds[i]);
        music_rng_seed(&b, seeds[i]);
        music_rng_seed(&other, 42);
        for (int j = 0; j < 1000; ++j) {
            const uint32_t value = music_rng_next(&a);
            (void)music_rng_next(&other); /* Independent state cannot interfere. */
            CHECK(value == music_rng_next(&b));
            CHECK(a.state != 0);
        }
    }
    return 0;
}

static int test_valid(void)
{
    music_data_t data = valid_data();
    CHECK(music_validate(&data) == MUSIC_OK);
    data.seed = 0;
    CHECK(music_validate(&data) == MUSIC_OK);
    CHECK(data.seed == 0);
    data.events[0].midi = 72; /* Octave changes preserve pitch class. */
    data.events[4].midi = 61; /* Melody is allowed outside the chord. */
    CHECK(music_validate(&data) == MUSIC_OK);
    data.chord_count = 0;
    data.event_count = 0; /* Schema permits empty arrays. */
    CHECK(music_validate(&data) == MUSIC_OK);
    return 0;
}

static int test_bpm(void)
{
    music_data_t data = valid_data();
    const int invalid[] = {0, -1, 39, 221};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        data.bpm = invalid[i];
        CHECK(music_validate(&data) == MUSIC_INVALID_BPM);
    }
    data.bpm = 40;
    CHECK(music_validate(&data) == MUSIC_OK);
    data.bpm = 220;
    CHECK(music_validate(&data) == MUSIC_OK);
    CHECK(strcmp(music_status_code(MUSIC_INVALID_BPM), "INVALID_BPM") == 0);
    return 0;
}

static int test_midi(void)
{
    music_data_t data = valid_data();
    const int invalid[] = {-1, 39, 85};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        data.events[4].midi = invalid[i];
        CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
        data = valid_data();
        data.chords[0].midi_notes[0] = invalid[i];
        CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
        data = valid_data();
    }
    data.events[4].midi = 40;
    CHECK(music_validate(&data) == MUSIC_OK);
    data.events[4].midi = 84;
    CHECK(music_validate(&data) == MUSIC_OK);
    return 0;
}

static int test_time(void)
{
    music_data_t data = valid_data();
    data.events[4].start_tick = 1900;
    data.events[4].duration_tick = 100;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.events[4].duration_tick = 20; /* End exactly at total length. */
    CHECK(music_validate(&data) == MUSIC_OK);
    data.events[4].duration_tick = UINT32_MAX; /* Addition must not wrap. */
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.events[4].start_tick = UINT32_MAX;
    data.events[4].duration_tick = 1;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.events[4].duration_tick = 0;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.chords[0].duration_tick = 1921;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.chords[0].duration_tick = UINT32_MAX;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    return 0;
}

static int test_polyphony(void)
{
    music_data_t data = valid_data();
    data.event_count = 9;
    for (size_t i = 0; i < data.event_count; ++i) {
        data.events[i] = (music_event_t){"p0", MUSIC_MELODY, 60, 90, 0, 200};
        data.events[i].id[1] = (char)('0' + i);
    }
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.event_count = 8;
    CHECK(music_validate(&data) == MUSIC_OK);
    data.event_count = 9;
    data.events[8].start_tick = 200;
    CHECK(music_validate(&data) == MUSIC_OK); /* Ends before simultaneous start. */
    data.events[8].start_tick = 199;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    /* Input order cannot hide a ninth overlapping event. */
    data.events[0].start_tick = 199;
    data.events[8].start_tick = 0;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    return 0;
}

static int test_protocol(void)
{
    music_data_t data = valid_data();
    memcpy(data.protocol_version, "2.0", 4);
    CHECK(music_validate(&data) == MUSIC_UNSUPPORTED_PROTOCOL_VERSION);
    memset(data.protocol_version, 'x', sizeof data.protocol_version);
    CHECK(music_validate(&data) == MUSIC_UNSUPPORTED_PROTOCOL_VERSION);
    data.protocol_version[0] = '\0';
    CHECK(music_validate(&data) == MUSIC_UNSUPPORTED_PROTOCOL_VERSION);
    CHECK(strcmp(music_status_code(MUSIC_UNSUPPORTED_PROTOCOL_VERSION),
                 "UNSUPPORTED_PROTOCOL_VERSION") == 0);
    return 0;
}

static int test_structure(void)
{
    music_data_t data = valid_data();
    CHECK(music_validate(NULL) == MUSIC_INVALID_MUSIC_DATA);
    data.bars = 0;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.bars = 17;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.bars = 16;
    CHECK(music_validate(&data) == MUSIC_OK);
    data = valid_data();
    data.event_count = 513;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.chord_count = 65;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    memcpy(data.events[1].id, "a0", 3);
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    memset(data.events[0].id, 'x', sizeof data.events[0].id);
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.events[0].id[0] = '\0';
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.events[0].velocity = 0;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.events[0].velocity = 128;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.ppq = 96;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.numerator = 3;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.denominator = 8;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.root = 'H';
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.mode = (music_mode_t)2;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.source = (music_source_t)3;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.events[0].part = (music_part_t)2;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.ok = false;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.request_id[0] = '\0';
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.chords[0].note_count = 0;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.chords[0].note_count = 7;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.chords[0].symbol[0] = '\0';
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    return 0;
}

static int test_chords(void)
{
    music_data_t data = valid_data();
    data.events[0].midi = 49;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.chord_count = 0;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data = valid_data();
    data.event_count = 1;
    data.events[0].duration_tick = 1000;
    data.chords[0].duration_tick = 500;
    data.chord_count = 2;
    data.chords[1] = (music_chord_t){500, 1420, "Dm", 3, {50, 53, 57}};
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    data.chords[1] = (music_chord_t){500, 1420, "C", 3, {60, 64, 67}};
    CHECK(music_validate(&data) == MUSIC_OK);
    data.chords[1].start_tick = 501;
    data.chords[1].duration_tick = 1419;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA); /* Chord gap. */
    data.chords[1].start_tick = 500;
    data.chords[1].duration_tick = 1420;
    music_chord_t temporary = data.chords[0];
    data.chords[0] = data.chords[1];
    data.chords[1] = temporary;
    CHECK(music_validate(&data) == MUSIC_OK); /* Unsorted chords. */
    return 0;
}

static int test_context(void)
{
    music_data_t data = valid_data();
    data.seed = 0;
    const size_t bytes = music_context_bytes();
    void *memory = malloc(bytes);
    CHECK(memory != NULL);
    CHECK((uintptr_t)memory % music_context_alignment() == 0);
    music_context *context = NULL;
    CHECK(music_init(memory, bytes, &data, &context) == MUSIC_OK);
    CHECK(context != NULL);
    CHECK(data.seed == 0);
    music_rng_t expected;
    music_rng_seed(&expected, 2164325893);
    CHECK(music_random_next(context) == music_rng_next(&expected));
    data.seed = 42; /* Context owns a copy, independent of caller data. */
    CHECK(music_random_next(context) == music_rng_next(&expected));
    CHECK(music_init(memory, bytes - 1, &data, &context) == MUSIC_INVALID_MUSIC_DATA);
    CHECK(context == NULL);
    CHECK(music_init(NULL, bytes, &data, &context) == MUSIC_INVALID_MUSIC_DATA);
    CHECK(music_init(memory, bytes, &data, NULL) == MUSIC_INVALID_MUSIC_DATA);
    data.bpm = 0;
    CHECK(music_init(memory, bytes, &data, &context) == MUSIC_INVALID_BPM);
    CHECK(context == NULL);
    free(memory);
    return 0;
}

static int test_pcm(void)
{
    CHECK(music_float_to_pcm16(NAN) == 0);
    CHECK(music_float_to_pcm16(INFINITY) == 0);
    CHECK(music_float_to_pcm16(-INFINITY) == 0);
    CHECK(music_float_to_pcm16(0.0f) == 0);
    CHECK(music_float_to_pcm16(1.0f) == 32767);
    CHECK(music_float_to_pcm16(-1.0f) == -32767);
    CHECK(music_float_to_pcm16(0.5f) == 16384);
    CHECK(music_float_to_pcm16(-0.5f) == -16384);
    CHECK(music_float_to_pcm16(2.0f) == 32767);
    CHECK(music_float_to_pcm16(-2.0f) == -32768);
    CHECK(music_float_to_pcm16(FLT_MAX) == 32767);
    CHECK(music_float_to_pcm16(-FLT_MAX) == -32768);
    return 0;
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static int test_wav(void)
{
    const char *path = "core-test-16.wav";
    const int16_t samples[16] = {0, 1, -1, 32767, -32768, 1234, -1234, 255,
                                 256, -256, 100, -100, 42, -42, 16384, -16384};
    CHECK(music_write_wav(path, samples, 16) == MUSIC_OK);
    FILE *file = NULL;
#ifdef _MSC_VER
    CHECK(fopen_s(&file, path, "rb") == 0);
#else
    file = fopen(path, "rb");
#endif
    CHECK(file != NULL);
    unsigned char bytes[76];
    CHECK(fread(bytes, 1, sizeof bytes, file) == sizeof bytes);
    CHECK(fgetc(file) == EOF);
    CHECK(!ferror(file));
    CHECK(fclose(file) == 0);
    CHECK(memcmp(bytes, "RIFF", 4) == 0);
    CHECK(read_u32(bytes + 4) == 68);
    CHECK(memcmp(bytes + 8, "WAVE", 4) == 0);
    CHECK(memcmp(bytes + 12, "fmt ", 4) == 0);
    CHECK(read_u32(bytes + 16) == 16);
    CHECK(read_u16(bytes + 20) == 1); /* Integer PCM. */
    CHECK(read_u16(bytes + 22) == 1); /* Mono. */
    CHECK(read_u32(bytes + 24) == 32000);
    CHECK(read_u32(bytes + 28) == 64000);
    CHECK(read_u16(bytes + 32) == 2);
    CHECK(read_u16(bytes + 34) == 16);
    CHECK(memcmp(bytes + 36, "data", 4) == 0);
    CHECK(read_u32(bytes + 40) == 32);
    for (size_t i = 0; i < 16; ++i) {
        CHECK(read_u16(bytes + 44 + i * 2) == (uint16_t)samples[i]);
    }
    CHECK(remove(path) == 0);
    CHECK(music_write_wav(NULL, samples, 16) == MUSIC_INVALID_MUSIC_DATA);
    CHECK(music_write_wav(path, NULL, 16) == MUSIC_INVALID_MUSIC_DATA);
    CHECK(music_write_wav(path, samples,
          (size_t)((UINT32_MAX - UINT32_C(36)) / 2) + 1) == MUSIC_INVALID_MUSIC_DATA);
    return 0;
}

int main(int argc, char **argv)
{
    const struct { const char *name; int (*run)(void); } cases[] = {
        {"ticks", test_ticks}, {"rng", test_rng}, {"valid", test_valid},
        {"bpm", test_bpm}, {"midi", test_midi}, {"time", test_time},
        {"polyphony", test_polyphony}, {"protocol", test_protocol},
        {"structure", test_structure}, {"chords", test_chords},
        {"context", test_context}, {"pcm", test_pcm}, {"wav", test_wav}
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            if (strcmp(argv[1], cases[i].name) == 0) {
                return cases[i].run();
            }
        }
    }
    fprintf(stderr, "Expected a registered CTest case name\n");
    return 1;
}
