`CoreLexend-Regular.ttf` is a 14,020-byte static ASCII subset of Lexend Regular
(weight 400). It retains the original glyph outlines, drops hinting and unused
layout features, and uses the private family name Core Lexend. It is embedded in
the executable and loaded only into this process; it is not installed system-wide.

Source: https://github.com/google/fonts/tree/main/ofl/lexend

Original `Lexend[wght].ttf` SHA-256:
`3add53e641fbc81da64da4bb254285e2831b52b029527bc0714e2b9610832ee6`

Subset SHA-256:
`2ab60ff333229b7a8749ec3d90a1278428ab7e79a8ef1b12f029ccec2fd36562`

License: [SIL Open Font License 1.1](OFL-Lexend.txt). Keep the license with releases.

To regenerate, install fontTools and run:

```
python tools/subset_font.py "Lexend[wght].ttf" assets/CoreLexend-Regular.ttf
```

The subset covers U+0020-007E. Characters outside this range depend on Windows font fallback. Localized key
names on non-English systems have not been tested.
If the private font cannot be loaded, the interface requests Segoe UI.
