#include "music_json.h"
#include "cJSON.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static bool digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* cJSON accepts some non-JSON number spellings and control characters, and
 * cannot represent embedded NULs. Check lexical constraints before parsing;
 * cJSON still owns all JSON structure parsing and string unescaping. */
static bool valid_lexemes(const char *input, size_t length)
{
    const unsigned char *s = (const unsigned char *)input;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = s[i];
        if (c == '"') {
            bool closed = false;
            while (++i < length) {
                if (s[i] == '"') { closed = true; break; }
                if (s[i] < 0x20) return false;
                if (s[i] == '\\') {
                    if (++i >= length) return false;
                    if (s[i] == 'u' && length - i >= 5 &&
                        memcmp(s + i, "u0000", 5) == 0) return false;
                }
            }
            if (!closed) return false;
        } else if (c == '-' || digit(c)) {
            size_t j = i;
            if (s[j] == '-' && ++j == length) return false;
            if (s[j] == '0') {
                ++j;
                if (j < length && digit(s[j])) return false;
            } else {
                if (!digit(s[j])) return false;
                while (j < length && digit(s[j])) ++j;
            }
            if (j < length && s[j] == '.') {
                if (++j == length || !digit(s[j])) return false;
                while (j < length && digit(s[j])) ++j;
            }
            if (j < length && (s[j] == 'e' || s[j] == 'E')) {
                if (++j < length && (s[j] == '+' || s[j] == '-')) ++j;
                if (j == length || !digit(s[j])) return false;
                while (j < length && digit(s[j])) ++j;
            }
            if (j < length && s[j] != ',' && s[j] != ']' && s[j] != '}' &&
                s[j] != ' ' && s[j] != '\t' && s[j] != '\r' && s[j] != '\n')
                return false;
            i = j - 1;
        } else if (c < 0x20 && c != '\t' && c != '\r' && c != '\n') {
            return false;
        }
    }
    return true;
}

/* Reject invalid UTF-8, overlong encodings, surrogates and > U+10FFFF. */
static bool valid_utf8(const char *input, size_t length)
{
    const unsigned char *s = (const unsigned char *)input;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = s[i];
        if (c < 0x80) continue;
        size_t extra;
        uint32_t value, minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            extra = 1; value = c & 0x1fu; minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            extra = 2; value = c & 0x0fu; minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            extra = 3; value = c & 0x07u; minimum = 0x10000;
        } else return false;
        if (extra >= length - i) return false;
        for (size_t j = 0; j < extra; ++j) {
            c = s[++i];
            if ((c & 0xc0u) != 0x80u) return false;
            value = (value << 6) | (c & 0x3fu);
        }
        if (value < minimum || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

/* Exact keys enforce required fields, additionalProperties:false and reject
 * duplicates (cJSON's usual lookup would silently return the first match). */
static bool object_keys(const cJSON *object, const char *const *keys, size_t count)
{
    if (!cJSON_IsObject(object)) return false;
    size_t found = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, object) {
        size_t k = 0;
        while (k < count && strcmp(item->string, keys[k]) != 0) ++k;
        if (k == count) return false;
        for (const cJSON *prior = object->child; prior != item; prior = prior->next)
            if (strcmp(prior->string, item->string) == 0) return false;
        ++found;
    }
    return found == count;
}

static const cJSON *field(const cJSON *object, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

static bool text(const cJSON *item, char *output, size_t capacity)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
    const size_t length = strlen(item->valuestring);
    if (length == 0 || length >= capacity) return false;
    memcpy(output, item->valuestring, length + 1);
    return true;
}

static bool equal(const cJSON *item, const char *value)
{
    return cJSON_IsString(item) && strcmp(item->valuestring, value) == 0;
}

/* ASCII request_id contract shared with the backend and response schemas. */
static bool request_id(const cJSON *item, char *output, size_t capacity)
{
    if (!text(item, output, capacity)) return false;
    for (const unsigned char *p = (const unsigned char *)output; *p; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              digit(*p) || *p == '.' || *p == '_' || *p == ':' || *p == '-'))
            return false;
    }
    return true;
}

static bool number(const cJSON *item, uint32_t maximum, uint32_t *output)
{
    if (!cJSON_IsNumber(item)) return false;
    const double value = item->valuedouble;
    if (!isfinite(value) || value < 0 || value > maximum || floor(value) != value)
        return false;
    *output = (uint32_t)value;
    return true;
}

static bool integer(const cJSON *item, int *output)
{
    uint32_t value;
    if (!number(item, INT_MAX, &value)) return false;
    *output = (int)value;
    return true;
}

static bool decode(const cJSON *root, music_data_t *data)
{
    static const char *const top[] = { "ok", "protocol_version", "request_id",
        "mode", "seed", "bpm", "time_signature", "ppq", "bars", "key",
        "chords", "events", "transcript" };
    static const char *const signature_keys[] = { "numerator", "denominator" };
    static const char *const key_keys[] = { "root", "mode" };
    static const char *const chord_keys[] = { "start_tick", "duration_tick",
        "symbol", "midi_notes" };
    static const char *const event_keys[] = { "id", "part", "midi", "velocity",
        "start_tick", "duration_tick" };
    const cJSON *transcript = field(root, "transcript");
    if (!object_keys(root, top, sizeof top / sizeof *top - (transcript == NULL ? 1 : 0)) ||
        !cJSON_IsTrue(field(root, "ok"))) return false;
    /* Optional metadata: validate but do not store or use in synthesis.
     * valid_utf8 already checked the document; count Unicode code points. */
    if (transcript != NULL) {
        if (!cJSON_IsString(transcript) || transcript->valuestring == NULL) return false;
        size_t characters = 0;
        for (const unsigned char *p = (const unsigned char *)transcript->valuestring; *p; ++p)
            if ((*p & 0xc0u) != 0x80u) ++characters;
        if (characters == 0 || characters > 500) return false;
    }
    data->ok = true;
    if (!text(field(root, "protocol_version"), data->protocol_version,
              sizeof data->protocol_version) ||
        !request_id(field(root, "request_id"), data->request_id, sizeof data->request_id) ||
        !number(field(root, "seed"), UINT32_MAX, &data->seed) ||
        !integer(field(root, "bpm"), &data->bpm) ||
        !integer(field(root, "ppq"), &data->ppq) ||
        !integer(field(root, "bars"), &data->bars)) return false;
    const cJSON *mode = field(root, "mode");
    if (equal(mode, "rules")) data->source = MUSIC_RULES;
    else if (equal(mode, "ai")) data->source = MUSIC_AI;
    else if (equal(mode, "mock")) data->source = MUSIC_MOCK;
    else return false;
    const cJSON *signature = field(root, "time_signature");
    const cJSON *key = field(root, "key");
    char root_name[2];
    if (!object_keys(signature, signature_keys, 2) ||
        !integer(field(signature, "numerator"), &data->numerator) ||
        !integer(field(signature, "denominator"), &data->denominator) ||
        !object_keys(key, key_keys, 2) ||
        !text(field(key, "root"), root_name, sizeof root_name)) return false;
    data->root = root_name[0];
    mode = field(key, "mode");
    if (equal(mode, "major")) data->mode = MUSIC_MAJOR;
    else if (equal(mode, "minor")) data->mode = MUSIC_MINOR;
    else return false;

    const cJSON *chords = field(root, "chords"), *item;
    if (!cJSON_IsArray(chords) || cJSON_GetArraySize(chords) > MUSIC_MAX_CHORDS)
        return false;
    cJSON_ArrayForEach(item, chords) {
        music_chord_t *chord = &data->chords[data->chord_count++];
        if (!object_keys(item, chord_keys, 4) ||
            !number(field(item, "start_tick"), UINT32_MAX, &chord->start_tick) ||
            !number(field(item, "duration_tick"), UINT32_MAX, &chord->duration_tick) ||
            !text(field(item, "symbol"), chord->symbol, sizeof chord->symbol))
            return false;
        const cJSON *notes = field(item, "midi_notes"), *note;
        if (!cJSON_IsArray(notes) || cJSON_GetArraySize(notes) < 1 ||
            cJSON_GetArraySize(notes) > MUSIC_MAX_CHORD_NOTES) return false;
        cJSON_ArrayForEach(note, notes) {
            if (!integer(note, &chord->midi_notes[chord->note_count++])) return false;
        }
    }
    const cJSON *events = field(root, "events");
    if (!cJSON_IsArray(events) || cJSON_GetArraySize(events) > MUSIC_MAX_EVENTS)
        return false;
    cJSON_ArrayForEach(item, events) {
        music_event_t *event = &data->events[data->event_count++];
        if (!object_keys(item, event_keys, 6) ||
            !text(field(item, "id"), event->id, sizeof event->id) ||
            !integer(field(item, "midi"), &event->midi) ||
            !integer(field(item, "velocity"), &event->velocity) ||
            !number(field(item, "start_tick"), UINT32_MAX, &event->start_tick) ||
            !number(field(item, "duration_tick"), UINT32_MAX, &event->duration_tick))
            return false;
        const cJSON *part = field(item, "part");
        if (equal(part, "accompaniment")) event->part = MUSIC_ACCOMPANIMENT;
        else if (equal(part, "melody")) event->part = MUSIC_MELODY;
        else return false;
    }
    return true;
}

music_status_t music_json_parse(const char *input, size_t length, music_data_t *data)
{
    if (input == NULL || data == NULL || length == SIZE_MAX ||
        !valid_utf8(input, length) || !valid_lexemes(input, length))
        return MUSIC_INVALID_MUSIC_DATA;
    cJSON *root = cJSON_ParseWithLengthOpts(input, length + 1, NULL, true);
    if (root == NULL) return MUSIC_INVALID_MUSIC_DATA;
    memset(data, 0, sizeof *data);
    const bool decoded = decode(root, data);
    cJSON_Delete(root);
    return decoded ? music_validate(data) : MUSIC_INVALID_MUSIC_DATA;
}
