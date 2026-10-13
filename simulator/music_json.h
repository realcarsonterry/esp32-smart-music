#ifndef MUSIC_JSON_H
#define MUSIC_JSON_H

#include "music_core.h"

/* Input includes length bytes followed by a NUL terminator. JSON stays outside
 * core; the returned fixed-array structure is validated by music_validate. */
music_status_t music_json_parse(const char *input, size_t length,
                                music_data_t *data);

#endif
