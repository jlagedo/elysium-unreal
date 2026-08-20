# Kenney Input Prompts

This directory contains the selected 128 px `Double` PNG sources used by Elysium's CommonInput
button-glyph policy. The complete archive is not vendored because the runtime needs only the
keyboard, Xbox and DualSense controls represented here.

- Source: [Kenney Input Prompts](https://kenney.nl/assets/input-prompts)
- Archive: `kenney_input-prompts_1.5.zip`
- Archive SHA-256: `AC2FCF599080B0F3BA2D174C9474DB6DF1A0E96FF0662580E2DA79A122AB78A1`
- Pack license: Creative Commons Zero 1.0; the pack's `License.txt` is included unchanged.

CC0 covers the artwork, not third-party trademarks. PlayStation and Xbox names and controller
symbols remain the property of their respective owners and are used only to identify compatible
controls.

`pipeline/unreal/make_input_glyphs.py` imports these sources into ignored, regenerable textures
under `/Game/Input/Glyphs/Kenney/**`.
