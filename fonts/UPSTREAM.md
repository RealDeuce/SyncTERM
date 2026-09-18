# UNSCII font data

The generated assets come from [UNSCII](https://github.com/viznut/unscii),
commit `257b4dfeb50651510fe8b9cf4f151a148a4490e4`.  The project describes
UNSCII as public domain or CC0.  Only `fontfiles/unscii-8.hex` and
`fontfiles/unscii-16.hex` are used; `unscii-16-full` is deliberately excluded.

Exact source URLs and SHA-256 digests are recorded in `update_fonts.py`.

`st_builtin_font_data.c` retains SyncTERM's complete legacy built-in font
catalog: selectors 0 through 45, every available 8x8, 8x14, 8x16, and 12x20
bitmap, and the corresponding Unicode mappings. This is project-owned data
carried forward from the legacy `conio/allfonts.c` and
`conio/utf8_codepages.c`, not generated external data.
