# reference/

`evo_pt_probe.c` is the probe that produced every result in this repo, copied unchanged from EVO Player (GPL-3.0). It isn't a standalone program: it uses EVO's `evo_bt()` logging macro and FFmpeg's libavformat to read AC-3 frames, and it's started from EVO's dev remote with `ptprobe <command>`.

The part to copy into your own project is small:

- `resolve()`: finds the functions in `libSceAudioOut.sprx` at runtime
- `pack_ac3_burst()`: wraps one AC-3 frame in an IEC 61937 burst
- `do_stream()` with `use_pt == 2`: opens the port with `sceAudioOutExOpen`, switches with `sceAudioOutExConfigureOutput`, writes bursts, closes
- the `sony` branch in `probe_thread()`: the exact sequence that worked on hardware

The other commands (`stream pt`, `sweep`) are the failed attempts, kept so the results can be reproduced.
