#include "music_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

static music_data_t single_note(int midi, int velocity, uint32_t duration)
{
    music_data_t data = {0};
    data.ok = true;
    strcpy(data.protocol_version, MUSIC_PROTOCOL_VERSION);
    strcpy(data.request_id, "stage-3");
    data.source = MUSIC_RULES;
    data.seed = 1;
    data.bpm = 60;
    data.numerator = 4;
    data.denominator = 4;
    data.ppq = MUSIC_PPQ;
    data.bars = 1;
    data.root = 'C';
    data.mode = MUSIC_MAJOR;
    data.event_count = 1;
    data.events[0] = (music_event_t){"note", MUSIC_MELODY, midi, velocity,
                                    0, duration};
    return data;
}

/* Allocation belongs solely to the caller/test harness. */
static int render(const music_data_t *data, int16_t *pcm, size_t count,
                  size_t block)
{
    void *memory = malloc(music_context_bytes());
    CHECK(memory != NULL);
    music_context *context = NULL;
    CHECK(music_init(memory, music_context_bytes(), data, &context) == MUSIC_OK);
    size_t position = 0;
    while (position < count) {
        const size_t remaining = count - position;
        const size_t request = remaining < block ? remaining : block;
        CHECK(music_render_next_pcm16(context, pcm + position, request) == request);
        position += request;
    }
    free(memory);
    return 0;
}

static double rms(const int16_t *pcm, size_t count)
{
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += (double)pcm[i] * pcm[i];
    }
    return sqrt(sum / (double)count);
}

static double goertzel(const int16_t *pcm, size_t count, double frequency)
{
    const double pi = 3.14159265358979323846;
    const double coefficient = 2.0 * cos(2.0 * pi * frequency / MUSIC_SAMPLE_RATE);
    double previous = 0.0, before_previous = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double next = pcm[i] + coefficient * previous - before_previous;
        before_previous = previous;
        previous = next;
    }
    return previous * previous + before_previous * before_previous -
           coefficient * previous * before_previous;
}

static int test_pitch(void)
{
    const int notes[] = {52, 57, 64, 69};
    int16_t pcm[32000];
    int failed = 0;
    for (size_t i = 0; i < sizeof notes / sizeof notes[0]; ++i) {
        music_data_t data = single_note(notes[i], 100, 960);
        CHECK(render(&data, pcm, 32000, MUSIC_BLOCK_SIZE) == 0);
        const double target = 440.0 * pow(2.0, (notes[i] - 69) / 12.0);
        double best_power = -1.0, measured = 0.0;
        int best_cents = 0;
        /* SPEC: discard [0,1600), analyze [1600,32000), no extra taper.
         * Scan exactly -40..+40 cents with a one-cent step. */
        for (int cents = -40; cents <= 40; ++cents) {
            const double frequency = target * pow(2.0, cents / 1200.0);
            const double power = goertzel(pcm + 1600, 32000 - 1600, frequency);
            if (power > best_power) {
                best_power = power;
                best_cents = cents;
                measured = frequency;
            }
        }
        printf("MIDI %d: target=%.9f Hz measured=%.9f Hz cents=%+d\n",
               notes[i], target, measured, best_cents);
        if (abs(best_cents) > 20 || rms(pcm + 1600, 30400) < 1.0) {
            failed = 1;
        }
    }
    return failed;
}

static int test_velocity(void)
{
    int16_t quiet[32000], loud[32000];
    music_data_t data = single_note(57, 40, 960);
    CHECK(render(&data, quiet, 32000, 256) == 0);
    data.events[0].velocity = 100;
    CHECK(render(&data, loud, 32000, 256) == 0);
    const double low = rms(quiet + 1600, 30400);
    const double high = rms(loud + 1600, 30400);
    printf("velocity 40 RMS=%.6f; velocity 100 RMS=%.6f; ratio=%.6f\n",
           low, high, low / high);
    CHECK(low > 0.0 && low < high);
    CHECK(fabs(low / high - 0.4) < 0.002);
    /* First output is the prescribed noise mapping and fixed 0.12 gain. */
    music_rng_t seeds, note;
    music_rng_seed(&seeds, data.seed);
    music_rng_seed(&note, music_rng_next(&seeds));
    const float excitation = (float)(music_rng_next(&note) & 0xffffu) /
                             32768.0f - 1.0f;
    CHECK(loud[0] == music_float_to_pcm16(excitation * (float)(100.0 / 127 * 0.12)));
    return 0;
}

static int test_release(void)
{
    int16_t short_note[34000], held_note[34000];
    music_data_t data = single_note(57, 100, 480);
    CHECK(render(&data, short_note, 34000, 256) == 0);
    data.events[0].duration_tick = 960;
    CHECK(render(&data, held_note, 34000, 256) == 0);
    CHECK(memcmp(short_note, held_note, 32000 * sizeof *short_note) == 0);
    /* Compare against the same still-held KS signal, isolating release gain
     * from the string's own decay. Allow one PCM unit for quantization. */
    for (size_t i = 0; i < MUSIC_RELEASE_SAMPLES; ++i) {
        const double expected = held_note[32000 + i] *
                                (double)(MUSIC_RELEASE_SAMPLES - i) /
                                MUSIC_RELEASE_SAMPLES;
        CHECK(fabs(short_note[32000 + i] - expected) <= 1.0);
    }
    const double early = rms(short_note + 32000, 160);
    const double late = rms(short_note + 32480, 160);
    printf("release RMS first 160=%.6f; last 160=%.6f\n", early, late);
    CHECK(late < early);
    for (size_t i = 32640; i < 34000; ++i) CHECK(short_note[i] == 0);
    return 0;
}

static int test_limits(void)
{
    int16_t pcm[34000];
    music_data_t data = single_note(52, 127, 480);
    data.event_count = 8;
    for (size_t i = 0; i < 8; ++i) {
        data.events[i] = data.events[0];
        (void)snprintf(data.events[i].id, sizeof data.events[i].id, "n%u", (unsigned)i);
        data.events[i].midi = 52 + (int)i;
    }
    CHECK(music_validate(&data) == MUSIC_OK);
    CHECK(render(&data, pcm, 34000, 256) == 0);
    const int limit = music_float_to_pcm16(0.98f);
    int peak = 0;
    for (size_t i = 0; i < 34000; ++i) {
        CHECK(abs((int)pcm[i]) <= limit);
        if (abs((int)pcm[i]) > peak) peak = abs((int)pcm[i]);
    }
    printf("eight-note peak=%d; PCM limit=%d\n", peak, limit);
    CHECK(peak > 0);
    data.events[8] = data.events[0];
    strcpy(data.events[8].id, "n8");
    data.event_count = 9;
    CHECK(music_validate(&data) == MUSIC_INVALID_MUSIC_DATA);
    void *memory = malloc(music_context_bytes());
    CHECK(memory != NULL);
    music_context *context = NULL;
    CHECK(music_init(memory, music_context_bytes(), &data, &context) ==
          MUSIC_INVALID_MUSIC_DATA);
    CHECK(context == NULL);
    free(memory);
    return 0;
}

static music_data_t sequence(void)
{
    music_data_t data = single_note(40, 100, 480);
    data.bpm = 220;
    data.event_count = 24;
    /* Unsorted events: three consecutive groups of eight, with simultaneous
     * note-offs/ons and release tails overlapping the next full chord. */
    for (size_t i = 0; i < data.event_count; ++i) {
        data.events[i] = (music_event_t){"", MUSIC_MELODY,
            40 + (int)(i * 7 % 45), 40 + (int)(i % 88),
            (uint32_t)(2 - i / 8) * 480, 480};
        (void)snprintf(data.events[i].id, sizeof data.events[i].id, "s%u", (unsigned)i);
    }
    return data;
}

static int test_blocks(void)
{
    music_data_t data = sequence();
    const size_t count = (size_t)music_tick_to_sample(1920, 220) + 640;
    int16_t *whole = malloc(count * sizeof *whole);
    int16_t *blocks = malloc(count * sizeof *blocks);
    CHECK(whole != NULL && blocks != NULL);
    CHECK(render(&data, whole, count, count) == 0);
    CHECK(render(&data, blocks, count, 256) == 0);
    for (size_t i = 0; i < count; ++i) CHECK(whole[i] == blocks[i]);
    CHECK(render(&data, blocks, count, 1) == 0);
    CHECK(memcmp(whole, blocks, count * sizeof *whole) == 0);
    printf("whole/256/1: %zu samples identical, including releases and EOF\n", count);
    free(whole);
    free(blocks);
    return 0;
}

static int test_seed_reset(void)
{
    music_data_t data = sequence();
    const size_t count = (size_t)music_tick_to_sample(1920, 220) + 640;
    int16_t *first = malloc(count * sizeof *first);
    int16_t *second = malloc(count * sizeof *second);
    void *memory = malloc(music_context_bytes());
    CHECK(first != NULL && second != NULL && memory != NULL);
    const uint32_t seeds[] = {0, 1, UINT32_MAX};
    for (size_t i = 0; i < sizeof seeds / sizeof seeds[0]; ++i) {
        data.seed = seeds[i];
        CHECK(render(&data, first, count, 256) == 0);
        CHECK(render(&data, second, count, 256) == 0);
        CHECK(memcmp(first, second, count * sizeof *first) == 0);
        music_context *context = NULL;
        CHECK(music_init(memory, music_context_bytes(), &data, &context) == MUSIC_OK);
        CHECK(music_render_next_pcm16(context, second, 0) == 0);
        CHECK(music_render_next_pcm16(context, NULL, 256) == 0);
        CHECK(music_render_next_pcm16(context, second, 777) == 777);
        (void)music_random_next(context);
        CHECK(music_render_reset(context) == MUSIC_OK);
        CHECK(music_render_next_pcm16(context, second, count) == count);
        CHECK(memcmp(first, second, count * sizeof *first) == 0);
        second[0] = 123;
        CHECK(music_render_next_pcm16(context, second, 1) == 0);
        CHECK(second[0] == 123);
        printf("seed %u: two renders and partial-playback reset identical\n",
               (unsigned)seeds[i]);
    }
    data.seed = 2;
    CHECK(render(&data, second, count, 256) == 0);
    CHECK(memcmp(first, second, count * sizeof *first) != 0);
    CHECK(music_render_reset(NULL) == MUSIC_INVALID_MUSIC_DATA);
    CHECK(music_render_next_pcm16(NULL, second, 1) == 0);
    free(first);
    free(second);
    free(memory);
    return 0;
}

int main(int argc, char **argv)
{
    const struct { const char *name; int (*run)(void); } cases[] = {
        {"pitch", test_pitch}, {"velocity", test_velocity},
        {"release", test_release}, {"limits", test_limits},
        {"blocks", test_blocks}, {"seed_reset", test_seed_reset}
    };
    if (argc == 2) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            if (strcmp(argv[1], cases[i].name) == 0) return cases[i].run();
        }
    }
    fprintf(stderr, "Expected a registered KS test name\n");
    return 1;
}
