# AGENTS.md: machine-readable summary

Audience: AI coding assistants adding HDMI audio bitstream passthrough to PS5 homebrew. Human explanation: README.md. Evidence: docs/FINDINGS.md.

## Status

- VERIFIED on hardware (PS5 Pro, FW 12.70, 2026-10-08), from an ordinary SANDBOXED app (sample-app/, no sandbox escape): AC-3 (mode 0), AAC/ADTS (mode 1), DTS core (mode 2), E-AC-3 (mode 3), E-AC-3 with Atmos/JOC (mode 3, plays; the receiver's Atmos indicator not yet checked). The receiver showed the format and played the decoded audio.
- UNVERIFIED (derived from disassembly): modes 4, 9, 10. Dolby TrueHD and DTS-HD are not solved.
- DEAD END: `sceAudioOutExPtOpen` / `sceAudioOutPtOpen`. Silent in every tested variant; no Sony module imports them. Do not use.
- Not in the SDK headers, but exported by the PS5 payload SDK's `libSceAudioOut` stub: declare them `extern "C"` and link. Do NOT use `sceKernelDlsym`: it is refused in a sandboxed app.
- Reference implementation: sample-app/src/passthrough/iec61937.cpp (packing) and sample-app/src/platform/ps5/bitstream_out.cpp (console calls).

## Functions (libSceAudioOut, FW 12.70)

| name | NID | prototype (as used) |
|---|---|---|
| sceAudioOutExOpen | `6X6dp+07h4U` | `int32_t (int32_t userId /*0xFF*/, int32_t mode)` -> port handle (>=0) or SCE error (<0) |
| sceAudioOutExConfigureOutput | `VcE+gXSwFXI` | `int32_t (int32_t zero /*must be 0*/, uint32_t flags /*0*/, int32_t mode, int32_t target /*1 (Sony); 1..3 or 0xFF accepted*/, uint64_t opt /*0*/)` -> 0 on success |
| sceAudioOutExClose | `0TfjSulCV2A` | `int32_t (int32_t handle)` |
| sceAudioOutOutput | `QOQtbeDqsT4` | `int32_t (int32_t handle, const void *buf)`; buf = NULL waits for the queue to drain |
| sceAudioOutSysGetHdmiMonitorInfo | `Tf9-yOJwF-A` | `int32_t (int32_t type /*1 = HDMI*/, void *out, uint32_t size /*must be 0x180*/)` |

Linking: plain `extern "C"` declarations resolve against the SDK stub `target/lib/libSceAudioOut.so`. (`sceKernelLoadStartModule` + `sceKernelDlsym` only worked in a process that had escaped its sandbox.) `sceAudioOutSysGetHdmiMonitorInfo` works sandboxed too.

## Required call order

```
h = sceAudioOutExOpen(0xFF, MODE)                   // 1. open first
rc = sceAudioOutExConfigureOutput(0, 0, MODE, 1, 0) // 2. then switch HDMI to bitstream
loop: sceAudioOutOutput(h, grain)                   // 3. IEC 61937 data, S16 stereo
sceAudioOutOutput(h, NULL)                          // 4. drain
sceAudioOutExClose(h)
sceAudioOutExConfigureOutput(0, 0, 0xFF, 0xFF, 0)   // 5. restore PCM output
```

While bitstream mode is on, all PCM ports of the process are muted (the process holds HDMI audio exclusively). Always restore with mode 0xFF, including on error and exit paths.

## Modes

Valid for ExOpen: {0,1,2,3,4,9,10} (bitmask 0x61F; other values -> 0x80260015). ExOpen opens internal port type 6, data format 1 (S16 stereo).

| mode | HDMI coding | ch | grain (samples per Output) | rate | status |
|---|---|---|---|---|---|
| 0 | AC-3 (CEA code 2) | 6 | 256 | 48000 | VERIFIED |
| 1 | AAC (6) | 6 | 256 | 48000 | VERIFIED (ADTS) |
| 2 | DTS (7) | 6 | 256 | 48000 | VERIFIED (core, 512-sample frames) |
| 3 | E-AC-3 (10) | 8 | 1024 | 192000 | VERIFIED (5.1, and Atmos/JOC plays) |
| 4 | Sony 0xF0 (TrueHD?) | 8 | 1024 | 192000 | unverified |
| 9 | Sony 0x16 | 6 | 256 | 48000 | unverified |
| 10 | Sony 0xF3 (DTS-HD?) | 8 | 1024 | 192000 | unverified |
| 0xFF | reset to default | - | - | - | VERIFIED (ConfigureOutput only) |

Sony's own table (citroncore.elf, va 0x1458e0) pairs mode with target: default {0xFF,0xFF}, AAC {1,1}, AC-3 {0,1}, E-AC-3 {3,1}.

## IEC 61937 bursts (all verified on hardware)

| codec | Pc | Pd unit | burst bytes | grouping |
|---|---|---|---|---|
| AC-3 | `0x0001 \| (bsmod << 8)` | bits | 6144 | one frame (1536 samples) |
| E-AC-3 | `0x0015` | bytes | 24576 | frames until 6 audio blocks; dependent substream frames go in the same burst |
| DTS core | `0x000B` / `0x000C` / `0x000D` for 512 / 1024 / 2048 samples | bits | samples x 4 | one frame; frame + 8 must fit |
| AAC (ADTS) | `0x0007` | bits | 4096 | one 1024-sample ADTS frame, header included |

Frame sizes: AC-3 from fscod/frmsizecod table; E-AC-3 `(((b[2]&7)<<8)|b[3])+1` words, blocks from numblkscod {1,2,3,6}, dependent if strmtyp (b[2]>>6) == 1; DTS core FSIZE `(((b[5]&3)<<12)|(b[6]<<4)|(b[7]>>4))+1` bytes, samples `(NBLKS+1)*32`, NBLKS `((b[4]&1)<<6)|(b[5]>>2)`; ADTS length `((b[3]&3)<<11)|(b[4]<<3)|(b[5]>>5)`.

### AC-3 in detail

- One AC-3 sync frame (starts `0x0B 0x77`) covers 1536 PCM frames -> burst = 1536 x 2 ch x 2 bytes = 6144 bytes = 6 Output calls of 256 frames.
- Words (uint16, stored little-endian as S16 samples): `w[0]=0xF872; w[1]=0x4E1F; w[2]=0x0001 | ((frame[5] & 7) << 8); w[3]=frame_size_bytes * 8;` then `w[4+i/2] = (frame[i] << 8) | frame[i+1]` for the payload; zero-fill the rest.
- Expected first bytes in memory: `72 f8 1f 4e 01 00 <Pd lo> <Pd hi> 77 0b ...`.
- Byte-swapping the whole burst is NOT needed (the swapped variant was tested on the dead-end port only).

```c
static int pack_ac3_burst(const uint8_t *f, int size, uint16_t *w /* 3072 words */)
{
    if (size < 6 || size + 8 > 6144 || f[0] != 0x0B || f[1] != 0x77) return -1;
    memset(w, 0, 6144);
    w[0] = 0xF872; w[1] = 0x4E1F;
    w[2] = (uint16_t)(0x0001 | ((f[5] & 7) << 8));
    w[3] = (uint16_t)(size * 8);
    for (int i = 0; i < size; i += 2)
        w[4 + i / 2] = (uint16_t)((f[i] << 8) | (i + 1 < size ? f[i + 1] : 0));
    return 0;
}
```

## How to verify

- Kernel log (klogsrv, port 3232) shows `[AvControl] audio: port:HDMI src:I2S_BS 48k 5.1 bs fmt:BITSTREAM AC3` and `AudioOut: exclusive (pid=...)` after step 2. `fmt:ENCODE_DOLBY` instead means system re-encode, not passthrough.
- The receiver's format display should name the format (Dolby Digital, Dolby Digital Plus, DTS, AAC/multichannel).

## Pitfalls

- Opening the port AFTER the switch, using target 0xFF, and using ExPtOpen were all present in failed runs. Follow the exact order above.
- Sending IEC 61937 to a normal PCM port without the switch produces full-scale noise on the receiver. Keep volume low while testing.
- Read the sink's capabilities first with sceAudioOutSysGetHdmiMonitorInfo; short audio descriptors start at offset 0x90 (layout in docs/FINDINGS.md). Fall back to PCM if the codec is not listed.
