# Sound GLB seam

This document defines one binary glTF 2.0 unit for one VtMB audio member — a `.wav` or `.mp3`
below `sound/` — together with the same-stem `.lip` phoneme document when one ships. Shared
rules are owned by `seam_map_unit_contract.md`; format facts by `docs/vtmb/audio_pipeline.md`
and the `.lip` section of `docs/vtmb/facial_animation.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> sound/<path>.wav
<VTMB>/Vampire           -> sound/<path>.mp3
  -> vtmb:sound:<path>.wav | vtmb:sound:<path>.mp3
  -> $ELYSIUM_EXPORT_V2_ROOT/sounds/<path>.<ext>.glb
```

The key is the normalized path below `sound/` **with** its extension, because seven stems ship as
both `.wav` and `.mp3` and the two are distinct members with distinct bytes. The namespace is the
one `seam_map_surface_property.md` already references
(`vtmb:sound:surfaces/concrete/stepleft1.wav`). The audio member resolves UP-first and selects
the unit; 5,550 `.wav` and 5,342 `.mp3` resolve in the merged install.

```text
uv run elysium export_v2 sound-glb sound/<path>.<ext>
uv run elysium export_v2 sounds-glb
```

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `sound/<path>.wav` or `.mp3` | unit-selecting audio | BIN payload; `codec`, `chunks[]` or `frames[]`, `tags` |
| `sound/<path>.lip` | optional phoneme companion | `lip` |

The `.lip` resolves UP-first independently of the audio (the patch ships 7,121 loose `.lip`
against 5,441 in the VPKs). 7,105 of the 7,136 `.lip` members pair with a same-stem audio
member; the 31 that pair with nothing are corpus-index rows (`orphan-companion`), not units. A
`.lip` beside a stem that ships as both `.wav` and `.mp3` is a member of both units.

`sound/**/*.sfk` (11) and `*.pk` (2) are Sound Forge peak caches, never audio members; the corpus
index owns them as residue.

## Resolution

VtMB's `speak` and `PlayDialogFile` paths are **mp3-first**: a reference authored as `.wav` plays
`<stem>.mp3` when it exists. A `.wav` unit whose stem also resolves as `.mp3` therefore records:

```json
{"resolution": {"rule": "mp3-first", "shadowedBy": "vtmb:sound:character/dlg/…/line191_col_e.mp3"}}
```

and the `.mp3` unit records `shadows`. The rule is a referrer's join; the unit only states the
fact so a reader can see which of the pair retail plays.

## Binary glTF layout

The unit is **scene-less**: glTF has no audio object, so the payload is a buffer view reached
through the extension and nothing else is declared.

The BIN chunk holds two different things and the distinction matters. First the **payload**: the
*decode* -- interleaved 16-bit PCM for a `.wav`, the bare MPEG frame stream for an `.mp3` -- which
is what `payload.accessor` reads and what `frames[]` partitions. After it come the **source
capsules** (`seam_map_unit_contract.md`, "Source capsule"): the audio member's own bytes and, when
the install ships one, the `.lip` companion's, each in its own `bufferView`, each named by its row
in `sourceResolution.members[]`. The payload keeps `bufferView` 0, so adopting the capsule
renumbered nothing.

A `.wav` payload is never its member (the RIFF headers are decoded away and ADPCM is expanded),
and an `.mp3` payload is its member only when the member carries no tag and no trailer, so before
schema 1.1.0 **no sound unit was guaranteed to carry the file the install holds**. The capsule is
what makes `uv run elysium import sound` able to deploy a byte-exact `.wav`/`.mp3`/`.lip`.

```text
sound.glb
|- JSON chunk
|  |- one buffer, one accessor, and one bufferView per region below
|  `- extensions.ELYSIUM_vtmb_sound
`- BIN chunk
   |- payload: PCM samples (WAV) or the MPEG frame stream (MP3)   <- bufferView 0
   |- capsule: the `.wav`/`.mp3` member, verbatim
   `- capsule: the `.lip` companion, verbatim (when the install ships one)
```

```json
{
  "asset": {"version": "2.0", "generator": "Elysium Sound GLB Exporter"},
  "extensionsUsed": ["ELYSIUM_vtmb_sound"],
  "extensionsRequired": ["ELYSIUM_vtmb_sound"],
  "extensions": {
    "ELYSIUM_vtmb_sound": {
      "schemaVersion": "1.1.0",
      "identity": {},
      "sourceResolution": {},
      "payload": {},
      "codec": {},
      "chunks": [],
      "frames": [],
      "tags": {},
      "lip": null,
      "resolution": {},
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

### WAV payload

The accessor is `SCALAR` `SHORT`, interleaved by channel, `count = frames × channels`:

| Source encoding | `fmt ` tag | Corpus | Payload rule | `data` ledger state |
|---|---:|---:|---|---|
| Microsoft ADPCM, 4-bit | 2 | 5,306 | decoded block by block with the coefficient pairs the `fmt ` extension declares; the decode is integer arithmetic the format specifies exactly | `derived` |
| PCM 16-bit | 1 | 214 | copied | `mapped` |
| PCM 8-bit unsigned | 1 | 19 | widened `(s − 128) << 8` | `derived` |

Rates are 11025, 22050 and 44100 Hz; channels 1 or 2. Any other `fmt ` tag is `unsupported` and
fails the unit. The block count decoded is what the `data` chunk holds; the `fact` chunk's sample
count is carried and compared, not trusted.

### MP3 payload

The accessor is `SCALAR` `UNSIGNED_BYTE` holding every MPEG audio frame's bytes contiguously in
source order — header, optional CRC, side information and main data — with no byte added or
removed inside a frame. The bitstream is the authored datum: MP3 decoding is not bit-exact
across decoders, so the payload is the frames, and a consumer decodes with its own codec.
`frames[i]` locates each frame in both the source and the payload, so a payload byte and a
ledger range name the same frame.

Every member is MPEG-1 Layer III at 48, 64, 80 or 128 kbit/s, mono or joint stereo, with and
without CRC protection; 1,574 open with a LAME/Info tag frame, 53 with Xing, 116 carry ID3v2 and
87 ID3v1.

## Extension reference

| Key | Contents |
|---|---|
| `payload` | `accessor`, `sampleFormat` (`int16-interleaved` or `mpeg-frames`), `byteLength`, `sha256` |
| `codec` | `container` (`riff-wave` or `mpeg-audio`), `tag`, `channels`, `sampleRate`, `bitsPerSample` or `bitrate`, `blockAlign`, `samplesPerBlock`, `coefficients[]` (ADPCM), `durationSamples`, `durationSeconds`, `variableBitrate` |
| `chunks` | one row per RIFF chunk in file order: `id`, `offset`, `length`, `padded`, and `body` where the id is decoded |
| `frames` | one row per MPEG frame: `offset`, `length`, `payloadOffset`, `version`, `layer`, `bitrate`, `sampleRate`, `padding`, `private`, `mode`, `modeExtension`, `copyright`, `original`, `emphasis`, `crcPresent`, `crcValid`, `mainDataBegin` |
| `tags` | `id3v2` (header, extended header, `frames[]` of id, size, flags and decoded text where the frame is a text frame), `id3v1` (title, artist, album, year, comment, track, genre), `xing`/`info` (flags, frame count, byte count, TOC, quality) and `lame` (encoder string, tag revision, VBR method, lowpass, replay gain, encoder delay and padding) |
| `lip` | the decoded companion, or `null` |
| `resolution` | the mp3-first pair, when one exists |

### `chunks[].body`

| Chunk | Body |
|---|---|
| `fmt ` | every field including the extension bytes; for tag 2 the `samplesPerBlock`, coefficient count and coefficient pairs |
| `data` | `sampleFrames`, `blocks`, `payloadRange` |
| `fact` | `sampleLength` |
| `cue ` | cue points (id, position, chunk id, chunk start, block start, sample offset) |
| `smpl` | manufacturer, product, sample period, MIDI note and pitch, SMPTE, loops (id, type, start, end, fraction, count) |
| `LIST` | the list type and its sub-chunks; `INFO` sub-chunks decoded as text |
| `bext` | the Broadcast Wave description, originator, reference, date, time, time reference, version, UMID |

`ovwf`, `regn`, `minf`, `umid`, `elm1`, `elmo`, `_PMX` and `DISP` are Sound Forge session,
region, overview and metadata chunks and the Windows clipboard `DISP`. Their lengths and
digests are carried as `typedUnidentified` rows keyed by chunk id and offset; a decode of one of
them is a schema addition, not a change to the ledger, because the range is already claimed.

### `lip`

| Field | Meaning |
|---|---|
| `version` | `1.0`, `1.1` or `1.2` |
| `plaintext` | the caption text between the `PLAINTEXT` braces |
| `words[]` | `text`, `start`, `end`, `phonemes[]` of `code`, `text`, `start`, `end`, `volume`, `flag` (absent on five-field rows) |
| `emphasis[]` | empty on every shipped file, carried as a section |
| `closeCaption` | one language block: `language`, `phrases[]` of `kind`, `count`, `text`, `start`, `end` |
| `options` | `voiceDuck`, `speakerName` |

The phoneme `code` is the key — the code point the expression table's class column carries — and
`text` is authoring residue that names a different row under the same code across the corpus.
Words are split on whitespace alone: a word's text may begin or end with `"` and may contain the
CP-1252 ellipsis `0x85`, and both are part of the word. The one shipped row whose word carries a
real space (`WORD Come on 0.048 0.400`) is an `anomalies[] malformed-word-row` with the raw line.
The unit declares no dependency for a phoneme: the code resolves against whichever expression
table the actor's model selects, which is that model's join.

## Anomalies and omissions

| Row | Evidence |
|---|---|
| `anomalies[] stale-riff-length` | the RIFF size field plus 8 differs from the member length (196 members) |
| `anomalies[] riff-envelope-excess` | bytes follow the RIFF envelope: a `smpl` chunk written past the declared length, or the 18-byte trailer of ten zero bytes and `W3DI` (189 members) |
| `anomalies[] odd-chunk-unpadded` | an odd-length chunk with no pad byte |
| `anomalies[] fact-samples-mismatch` | `fact.sampleLength` disagrees with the decoded frame count |
| `anomalies[] leading-zero-padding` | zero bytes precede the first frame sync (`line341_col_f.mp3`) |
| `anomalies[] crc-mismatch` | a protected frame whose CRC does not verify |
| `anomalies[] bitrate-change` | frame bitrates differ within one stream |
| `anomalies[] malformed-word-row` | a `.lip` word row that does not parse as `WORD <text> <start> <end>` |
| `omissions[] empty-member` | the member is zero bytes (11 `sound/area/santa_monica/clinic/*.wav`) |
| `omissions[] sound-forge-trailer` | the `W3DI` trailer, with digest |
| `omissions[] non-frame-bytes` | MP3 bytes that are neither a frame nor a recognised tag, with offset, length and digest |

A member with the `W3DI` trailer is decoded by walking chunks to the RIFF envelope's end; the
excess is claimed as its own range rather than parsed as a chunk, because its first four bytes
are not a chunk id. An empty member publishes with a warning, a zero-length payload and no
`chunks` or `frames`.

## Byte ledger owners

| Owner | Member | Range |
|---|---|---|
| `riff.header` | WAV | the 12-byte `RIFF`/size/`WAVE` header |
| `riff.chunks[i].header` | WAV | one chunk's id and size |
| `riff.chunks[i].body` | WAV | the chunk body (`derived` for ADPCM `data`, `mapped` otherwise) |
| `riff.chunks[i].pad` | WAV | the odd-length pad byte (`padding-zero`) |
| `riff.excess` | WAV | bytes beyond the RIFF envelope |
| `mp3.leading` | MP3 | bytes before the first tag or frame |
| `mp3.tags.id3v2` | MP3 | the ID3v2 block |
| `mp3.frames[i]` | MP3 | one MPEG frame |
| `mp3.tags.id3v1` | MP3 | the trailing 128-byte tag |
| `mp3.trailing` | MP3 | bytes after the last frame that are not ID3v1 |
| `lip.version`, `lip.plaintext`, `lip.words[i]`, `lip.words[i].phonemes[j]`, `lip.emphasis`, `lip.closeCaption`, `lip.options` | LIP | the text region of each section (`mapped-text`) |
| `lip.whitespace` | LIP | line terminators and blank lines |

## Coverage and validation

A complete sound unit has zero `unresolved` and zero `unsupported` rows. Export-time validation
re-walks the RIFF chunks or the MPEG frame sequence independently of the writer, re-decodes the
ADPCM blocks and compares every sample against the payload, verifies each frame's header fields,
length and CRC against the payload bytes, re-parses the `.lip` and compares every word and
phoneme row, and checks the ledger against the source bytes. The standalone validator verifies
the scene-less core, the single accessor's extent and digest, and that `frames[]` lengths sum to
the payload length.

## Import (2026-09-06)

`uv run elysium import sound` deploys the whole sound family out of the published units and
nothing else (`pipeline/src/elysium_pipeline/importers/sound.py`, on the shared
`importers/corpus_deploy.py`). Each unit's two capsules are lifted and written to

```text
Content/ElysiumCorpus/sound/<rel>.wav        the audio member, verbatim
Content/ElysiumCorpus/sound/<rel>.lip        the `.lip` companion, beside its audio
Content/ElysiumCorpus/lip/<rel>.lip          the same bytes, under the legacy `lip/` key
```

**Why the `.lip` lands twice.** The runtime reads the two members through two accessors that root
at two different directories: `SoundFile(Rel)` is `Root()/sound/<Rel>` and `LipFile(Rel)` is
`Root()/lip/<Rel>`, where `Rel` is the *same* string — `ElysiumLip::NormalizeLipRel` is
`ElysiumScene::NormalizeSceneRel` with the extension swapped, so a `.lip` is keyed by its audio's
own path below `sound/`, lower-cased and forward-slashed. The dialogue plan (DC) asks for the
`.lip` "beside its audio, so `SoundDir()` has one root"; the legacy mirror and today's `LipDir()`
ask for `lip/<rel>.lip`. Both spellings are the same bytes from the same capsule, so whichever of
the two the C++ flip settles on — `LipDir()` → `CorpusRoot()/lip`, or `CorpusRoot()/sound` — the
file it opens is the install's, with no re-import. The duplication costs ~31 MB across 7,136
documents against ~1 GB of audio. Retiring one spelling is a `RECIPE_VERSION` bump and one edit to
`importers/sound.py`'s `target_of`, which prunes the other tree on the next run.

**Case.** Paths are the units' own keys, which are folded to lower case. The legacy `sound/`
mirror kept the install's mixed case (`sound/Area/Chinatown/Asian_Chimes1.wav`); the corpus does
not, matching every other corpus family and the fold every runtime reader already applies. Windows
is case-insensitive, so a raw `ambient_generic` `message` value resolves against either spelling.

The lane's properties — recipe stamps (`Content/ElysiumCorpus/_import/sound/recipes.json`),
per-unit failure isolation, byte-equality verification against the capsule, pruning of `sound/`
and `lip/`, and `import_report.json` — are the same ones listed in `seam_map_dialogue.md` →
"Import".

### Measured against the legacy mirror (2026-09-06)

| Tree | Legacy | Corpus | Only legacy | Only corpus | Byte differences |
|---|---|---|---|---|---|
| `sound/**.wav` | 5,550 | 5,550 | 0 | 0 | **1** (below) |
| `sound/**.mp3` | 5,342 | 5,342 | 0 | 0 | 0 |
| `lip/**.lip` | 7,136 | 7,105 | **31** (below) | 0 | 0 |
| `sound/**.lip` | — | 7,105 | — | — | 0 against `lip/**` |

**The one byte difference is a stale legacy export, not a lane defect.**
`sound/interface/infobar/need_more_blood.wav` is 61,274 bytes in
`Unofficial_Patch/sound/interface/infobar/`, and the corpus carries exactly those bytes with
`origin.root = "Unofficial_Patch"`. The legacy mirror carries 31,066 bytes, byte-equal to that
directory's `botch.wav` — what the patch shipped for that name when the legacy tree was written
(2026-08-29). The install's copy changed on 2026-09-04. Retail has no loose or packed member for
this path at all, so UP-first has one candidate and the corpus is the install.

**Named gap: 31 `.lip` files with no sibling audio do not deploy.** A `.lip` reaches a unit only as
the companion of an audio member (`formats/sound_glb/source.py`), so the 31 loose `.lip` documents
whose stem ships no `.wav` and no `.mp3` — mostly `character/dlg/main characters/beckett/**`, plus
`hollywood/andrei/line1_col_e` and `downtown la/hannahs_message/line1_col_e` — belong to no sound
unit and this lane cannot deploy them. The runtime already treats a `.lip` miss as ordinary
(`ElysiumLip::Load` warns on neither branch, naming this exact count), and a line with no audio is
never voiced, so nothing regresses in play — but the corpus tree is a strict subset of the legacy
one until an orphan-`.lip` unit exists. That is a seam addition, not an import change.

Reader flip: `SoundDir`/`SoundFile`/`LipDir`/`LipFile` move from `FElysiumContentPaths::Root()` to
`CorpusRoot()` in the C++ half of this slice.
