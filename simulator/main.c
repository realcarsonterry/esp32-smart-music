#include "music_json.h"

#include <stdio.h>
#include <stdlib.h>

enum { EXIT_JSON = 2, EXIT_RENDER = 3, EXIT_ENVIRONMENT = 5 };

static char *read_input(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    char *input = NULL;
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    const long size = ftell(file);
    if (size < 0 || (uintmax_t)size >= SIZE_MAX ||
        fseek(file, 0, SEEK_SET) != 0) goto done;
    *length = (size_t)size;
    input = malloc(*length + 1);
    if (input == NULL) goto done;
    if (fread(input, 1, *length, file) != *length || ferror(file)) {
        free(input);
        input = NULL;
    } else input[*length] = '\0';
done:
    if (fclose(file) != 0) { free(input); input = NULL; }
    return input;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: simulator <input.json> <output.wav>\n");
        return EXIT_ENVIRONMENT;
    }
    size_t length = 0;
    char *input = read_input(argv[1], &length);
    if (input == NULL) {
        fprintf(stderr, "Cannot read input JSON\n");
        return EXIT_ENVIRONMENT;
    }
    music_data_t data;
    music_status_t status = music_json_parse(input, length, &data);
    free(input);
    if (status != MUSIC_OK) {
        fprintf(stderr, "%s\n", music_status_code(status));
        return EXIT_JSON;
    }
    const uint64_t total = music_tick_to_sample(
        (uint32_t)data.bars * MUSIC_TICKS_PER_BAR, (uint32_t)data.bpm);
    if (total == 0 || total > SIZE_MAX / sizeof(int16_t)) return EXIT_RENDER;
    const size_t count = (size_t)total;
    void *memory = malloc(music_context_bytes());
    int16_t *pcm = malloc(count * sizeof *pcm);
    music_context *context = NULL;
    int result = EXIT_RENDER;
    if (memory == NULL || pcm == NULL ||
        music_init(memory, music_context_bytes(), &data, &context) != MUSIC_OK)
        goto done;
    /* The CLI writes exactly bars' duration. The core's additional release
     * tail remains available to other callers and its API is unchanged. */
    for (size_t offset = 0; offset < count;) {
        const size_t remaining = count - offset;
        const size_t block = remaining < MUSIC_BLOCK_SIZE ? remaining : MUSIC_BLOCK_SIZE;
        if (music_render_next_pcm16(context, pcm + offset, block) != block) goto done;
        offset += block;
    }
    if (music_write_wav(argv[2], pcm, count) != MUSIC_OK) goto done;
    printf("samples=%zu rate=%d channels=1 bits=16\n", count, MUSIC_SAMPLE_RATE);
    result = 0;
done:
    free(pcm);
    free(memory);
    if (result != 0) fprintf(stderr, "Render or WAV write failed\n");
    return result;
}
