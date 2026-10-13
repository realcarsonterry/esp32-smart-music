# Vendored cJSON

Unmodified upstream cJSON **v1.7.19**, from
[DaveGamble/cJSON](https://github.com/DaveGamble/cJSON/tree/v1.7.19).
Source download base: `https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/`.
The upstream MIT license is preserved in LICENSE; upstream README.md describes
the parser API and limitations. No network fetch is needed during builds.

SHA256 of downloaded files:

```text
cJSON.c  298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d
cJSON.h  25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee
LICENSE  a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c
```

Only simulator targets link this library. music_core has no cJSON dependency.
