# Regenerating the MSDF text atlas (with accents)

The app draws text from a **pre-baked glyph atlas**, not from the font at
runtime: `app/src/main/assets/fonts/font.msdf` (metrics) + `atlas.rgba` (raw
RGBA image), loaded by `MsdfFont::load` in `vulkan_font_engine/.../msdf.cc`.
The originally-shipped atlas only contained ASCII, so accented Latin letters
(`á é í ó ú ñ ü ¿ ä ö ß` …) had no glyph and rendered blank — even after the
UTF-8 decoding fix in the C++ code.

`atlas_gen` (this folder) bakes a new atlas **including accents**. It's a small
self-contained Rust program: it uses the pure-Rust `fontdue` crate only to turn
each glyph outline into a coverage bitmap; all the signed-distance-field math,
atlas packing and the bespoke `font.msdf` binary format are implemented in
`atlas_gen/src/main.rs`. No C++ compiler, Python, vcpkg or Skia required.

## Run it

```
cd tools/regen_atlas/atlas_gen
cargo build --release
target\release\atlas_gen ^
    ..\..\..\app\src\main\assets\fonts\font.otf ^
    ..\..\..\app\src\main\assets\fonts\font.msdf ^
    ..\..\..\app\src\main\assets\fonts\atlas.rgba
```

Then rebuild the APK (`.\gradlew.bat assembleDebug`) — Gradle re-copies
`app/src/main/assets/**` into the APK, so the new atlas is picked up.

`font.msdf.ascii.bak` / `atlas.rgba.ascii.bak` next to the assets are the
original ASCII-only files, kept as a backup.

## What to change

- **Charset:** edit the `chars` list in `main.rs` (currently printable ASCII +
  Latin-1 Supplement `0xA1..0xFF` + `Œ œ Ÿ €`). Glyphs not present in the font
  are skipped automatically.
- **Output format:** it emits a single-channel SDF copied into R=G=B. The shader
  (`msdf_frag.slang`) takes `median(r,g,b)`, edge at 0.5, field spanning `RANGE`
  (4) texels — the standard decode, so plain SDF renders correctly. True
  multi-channel MSDF keeps sharp corners crisper at very large sizes; at UI text
  sizes the difference is not visible.

## Binary format (`font.msdf`, little-endian)

```
u32 magic 0x4644534D ('MSDF')
u32 atlasW, u32 atlasH
f32 distanceRange, f32 sizePxEm, f32 lineHeight, f32 ascender, f32 descender
u32 glyphCount
per glyph: u32 codepoint, f32 advance, u32 hasGlyph,
           f32 planeL,planeT,planeR,planeB,   (em units, y-down: top negative)
           f32 atlasL,atlasT,atlasR,atlasB    (atlas pixels, y from top)
```

`atlas.rgba` is `atlasW * atlasH * 4` bytes, rows top-first.
