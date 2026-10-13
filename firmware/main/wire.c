#include "wire.h"
#include <string.h>

bool device_transition_allowed(device_state_t from, device_state_t to)
{
    if (from < IDLE || from > PLAYING || to < IDLE || to > PLAYING) return false;
    return to == IDLE || (from != PLAYING && to == from + 1);
}

/* Only a base origin is accepted: no credentials, paths, queries or fragments.
 * Numeric RFC1918 HTTP prevents DNS rebinding/redirects to public cleartext. */
bool backend_url_allowed(const char *url, bool allow_lan_http)
{
    if (!url) return false;
    bool secure = strncmp(url, "https://", 8) == 0;
    if (!secure && (!allow_lan_http || strncmp(url, "http://", 7) != 0)) return false;
    const char *host = url + (secure ? 8 : 7), *p = host;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '.' || *p == '-') ++p;
    if (p == host || *host == '.' || *host == '-' || p[-1] == '.' || p[-1] == '-')
        return false;
    const char *end = p;
    if (*p == ':') {
        unsigned port = 0;
        const char *start = ++p;
        while (*p >= '0' && *p <= '9') {
            port = port * 10u + (unsigned)(*p++ - '0');
            if (port > 65535u) return false;
        }
        if (p == start || port == 0) return false;
    }
    if (*p != '\0') return false;
    if (secure) return true;
    unsigned octets[4];
    p = host;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned value = 0;
        const char *start = p;
        while (p < end && *p >= '0' && *p <= '9') {
            value = value * 10u + (unsigned)(*p++ - '0');
            if (value > 255 || p - start > 3) return false;
        }
        if (p == start || (p - start > 1 && *start == '0')) return false;
        octets[i] = value;
        if (i < 3 && (p == end || *p++ != '.')) return false;
    }
    if (p != end) return false;
    return octets[0] == 10 || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) ||
           (octets[0] == 192 && octets[1] == 168);
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void le32(uint8_t *p, uint32_t v)
{
    le16(p, (uint16_t)v); le16(p + 2, (uint16_t)(v >> 16));
}
void record_wav_header(uint8_t p[WAV_HEADER_BYTES], uint32_t samples)
{
    memcpy(p, "RIFF", 4); le32(p + 4, 36u + samples * 2u);
    memcpy(p + 8, "WAVEfmt ", 8); le32(p + 16, 16);
    le16(p + 20, 1); le16(p + 22, 1); le32(p + 24, RECORD_RATE);
    le32(p + 28, RECORD_RATE * 2u); le16(p + 32, 2); le16(p + 34, 16);
    memcpy(p + 36, "data", 4); le32(p + 40, samples * 2u);
}
int16_t microphone_pcm16(uint32_t slot)
{
    /* INMP441: signed 24-bit MSB first in a 32-clock Philips slot, bits 31:8.
     * Drop padding (8 bits) and low precision (8 bits), without signed shifts.
     * No extra gain; explicit sign extension avoids implementation-defined casts. */
    int32_t high = (int32_t)(slot >> 16);
    if (high >= 32768) high -= 65536;
    return (int16_t)high;
}
