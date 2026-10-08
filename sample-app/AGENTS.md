# AGENTS.md: Surround Sound Studio (sample app)

Audience: AI coding assistants. The app is a ps5-homebrew-ui app (title PPSA99051) with two pages switched by L1 / R1: **Passthrough** (elementary audio streams as HDMI bitstream; facts in ../AGENTS.md) and **Speaker Lab** (EVO Player's surround test suite on an 8-channel PCM port). The shell's always-on page tabs are in `src/app/shell.cpp` (`draw_chrome`).

## Map

Speaker Lab (ported from EVO Player, namespaces `evo` / `evo::spatial` / `evo::kit` kept):

- `src/concepts/speaker_lab.cpp`: the page (input, sweeps, orb motion, calibration flow).
- `src/surround/`: room model and DBAP panning (`spatial_field.hpp`), the field music, the view (`surround_view.cpp`, kit draw lists) and its parameter block, the service headers.
- `src/platform/ps5/surround_test_service.cpp`: tones / field / one-shot PCM on `sceAudioOutOpen(0xFF, 0, 0, 512, 48000, 2 /*S16_8CH*/)`.
- `src/platform/ps5/speaker_calibration.cpp`: DualSense mic capture (`sceAudioIn*`, 16 kHz S16 mono), log sweeps, matched filter.
- `stubs/libSceAudioIn.c`: import stub the Makefile builds (`build/stubs/libSceAudioIn.so`); the payload SDK has none.
- `src/concepts/studio_audio.{hpp,cpp}`: entering a page stops the other page's audio (bitstream mode mutes all PCM ports).
- `host/surround_host.cpp`: silent stand-ins for the PC preview.

Passthrough:

- `src/passthrough/iec61937.{hpp,cpp}`: codec detection, frame parsing, IEC 61937 burst packing (AC-3 type 0x01, E-AC-3 0x15, DTS 0x0B/0x0C/0x0D, AAC 0x07). Portable; no PS5 calls.
- `src/passthrough/bitstream.{hpp,cpp}`: `pt::Player` interface, `pt::Sink`, CEA coding-type helpers.
- `src/platform/ps5/bitstream_out.{hpp,cpp}`: the console implementation (`hui::ps5::BitstreamPlayer`, `pt::make_player()`).
- `host/bitstream_host.cpp`: fake player for the PC preview (`tools/host-snapshots.sh`).
- `src/concepts/passthrough.cpp`: the screen (the only design; registry in `src/concepts/registry.cpp`).
- `assets/clips/`: bundled clips plus `index.txt`; `index.local.txt` and `local-*` are git-ignored.

## Rules

- Call the Ex audio functions as linked imports (`extern "C"`). Never `sceKernelDlsym`: it fails in a sandboxed app.
- Keep the order: `sceAudioOutExOpen(0xFF, mode)`, then `sceAudioOutExConfigureOutput(0, 0, mode, 1, 0)`, then output, then drain, `ExClose`, then `ExConfigureOutput(0, 0, 0xFF, 0xFF, 0)`. Restore on every exit path.
- Grain and rate come from the mode: modes 0, 1, 2 → 256 frames at 48 kHz; mode 3 → 1024 at 192 kHz. Output buffers are S16 stereo (4 bytes per frame).
- Every burst must last exactly as long as the audio it carries: AC-3 6144 bytes; E-AC-3 24576 bytes (6 blocks, 192 kHz); DTS samples×4; AAC 4096 bytes.
- Clips are read from `/app0/assets/clips` through the index files; `/app0` can't be listed on the console.
- Build: `make ffpfsc` (Linux, clang 18) or inside a PS5 toolchain Docker image. Never commit `dist/`, `build/`, `.deps/` or `local-*` clips.
- Verify on hardware through the app log (`/mnt/sandbox/PPSA99051_000/download0/hui/dev/app.log`, `[PT]` lines) and the receiver's display.

The ps5-homebrew-ui kit's own agent guide is `docs/KIT_AGENTS.md`. Its references to designs and tests that aren't here (they were removed) can be ignored.
