# Findings: reverse-engineering notes and hardware log

All offsets are for **firmware 12.70**. Virtual addresses are module-relative. "Text offset" is the address inside the first (executable) segment; in `libSceAudioOut.sprx` that segment starts at file offset `0x4000`.

## 1. Starting point

EVO Player decoded every audio format to PCM. An earlier research note claimed bitstream passthrough was impossible for apps: the HDMI output-mode switch (`sceAudioOutSysConfigureOutputMode`) was said to be gated by Sony capability checks. That claim had never been measured: no return code was logged.

## 2. Symbol survey

The public PS5 symbol list that AnyPS5's `tools/nid_names.py` uses (`zecoxao/sce_symbols`, `aerolib.csv`) lists about 170 `sceAudioOut*` names. The relevant ones exported by the 12.70 `libSceAudioOut.sprx`:

| function | NID | exported in 12.70 |
|---|---|---|
| sceAudioOutExOpen | `6X6dp+07h4U` | yes |
| sceAudioOutExClose | `0TfjSulCV2A` | yes |
| sceAudioOutExConfigureOutput | `VcE+gXSwFXI` | yes |
| sceAudioOutExConfigureOutputMode | `r1V9IFEE+Ts` | yes |
| sceAudioOutSysConfigureOutputMode | `oRJZnXxok-M` | yes |
| sceAudioOutSysConfigureOutput | `ktdp5iauPQc` | yes |
| sceAudioOutSysGetHdmiMonitorInfo | `Tf9-yOJwF-A` | yes |
| sceAudioOutSysHdmiMonitorInfoIsSupportedAudioOutMode | `YV+bnMvMfYg` | yes |
| sceAudioOutExPtOpen / ExPtClose | `4UlW3CSuCa4` / `xjjhT5uw08o` | yes (dead end) |
| sceAudioOutPtOpen / PtClose | `xyT8IUCL3CI` / `MapHTgeogbk` | yes (dead end) |
| sceAudioOutSetMainOutput | `KI9cl22to7E` | yes, but it is a stub: `xor eax,eax; ret` |
| sceAudioOutPassthroughOpenPort | `YiiThLreMU8` | **no** |
| sceAudioOutPtOpenEx | `iSybA782aFw` | **no** |

## 3. The output-mode call chain (libSceAudioOut)

- `sceAudioOutExConfigureOutputMode` (text `0x3c30`, 54 bytes) calls internal `0x1b910`.
- `sceAudioOutSysConfigureOutputMode` (text `0x4190`, 54 bytes) calls internal `0x1b470`.
- `sceAudioOutExConfigureOutput` (text `0x3c70`, 314 bytes) builds a 0x28-byte descriptor from `mode` and calls `0x1b910`.
- `0x1b910` requires its first argument to be 0 (else `0x80268009`), reads the current state, converts the descriptor, then calls `0x1b470(1, 0, desc)`. So the Ex and Sys paths end in the same routine.
- `0x1b470` calls imports from `libSceAvSetting`, resolved through the PLT:

| PLT slot | NID | name |
|---|---|---|
| 0x74 | `s9knR+WWOyI` | sceAvControlInit |
| 0x75 | `bTE6q+IwNKU` | sceAvControlChangeOutputMode |
| 0x76 | `eINK6ismSX0` | sceMbusAcquireControl |
| 0x77 | `n8+7l03wVdE` | sceMbusReleaseControl |
| 0x78 | `GpuFjTMZsis` | sceAvControlGetMonitorInfo |

`sceAvControlChangeOutputMode` is called with `0x7000` (type 1) or `0x7201` (type 0x10).

### Mode descriptor built by ExConfigureOutput

Jump table at va `0x60e8c`. Descriptor bytes 4..7 = channels, a mask (0x0F or 0x01), codec index, a channel class; dword at 0xC = 0x0F for bitstream modes.

| mode | ch | codec index | notes |
|---|---|---|---|
| 0 | 6 | 1 | |
| 1 | 6 | 2 | |
| 2 | 6 | 3 | |
| 3 | 8 | 4 | |
| 4 | 8 | 5 | |
| 5 | 2 | 0 | LPCM |
| 6 | 6 | 0 | LPCM |
| 7, 8 | 8 | 0 | LPCM |
| 9 | 6 | 6 | |
| 10 | 8 | 7 | |
| 0xFF | all fields 0xFF | | the routine's reset path |

Codec index → HDMI coding type (jump table at va `0x62e8c`, inside `0x1b910`): 0→1 (LPCM), 1→2 (AC-3), 2→6 (AAC), 3→7 (DTS), 4→0x0A (E-AC-3), 5→0xF0, 6→0x16, 7→0xF3. Codes 1, 2, 6, 7 and 10 are the CEA-861 audio coding types. 0xF0, 0x16 and 0xF3 are Sony-specific.

`target` (the 4th argument, stored at descriptor+0x18) must be 1..3 or 0xFF.

### sceAudioOutExOpen (text `0x2ea0`)

```
if mode > 10 or !(0x61F >> mode & 1): return 0x80260015
internal_open(user, type=6, index=0, 0, len=LEN[mode], freq=FREQ[mode], param=1, -1, -1)
```

LEN (va `0x60de8`) / FREQ (va `0x60e14`): modes 0,1,2,9 → 256 @ 48000; modes 3,4,10 → 1024 @ 192000; 5–8 → 0.

`param=1` is the S16-stereo format code. `ExPtOpen`, by contrast, uses the same internal port type 6 (or 7) but passes the caller's format code straight through. The values 14 and 12 tried from an older note have bits set in `0xF8`, so the internal open skips its PCM sample-size table for them. They aren't PCM formats, which probably explains the silence.

### sceAudioOutSysGetHdmiMonitorInfo (text `0x4590` → `0x1bdc0`)

`(int type, void *out, uint32 size)`. `out` must be non-NULL and `size` must be exactly `0x180`. `type` is 1 or 0x10.

## 4. Hardware log (chronological)

Probe commands from `reference/evo_pt_probe.c`, sent through EVO's dev remote.

1. **`info`**: `GetHdmiMonitorInfo(1)` rc=0. The sink name was "LG ULTRAGEAR". There's a short-audio-descriptor list at offset 0x90: a header `01 09 4f 00 09 00 00 00` (count 9), then 8-byte entries `[coding type, channels, rate mask, 0, extra, 0, 0, 0]`: LPCM 2ch, LPCM 6ch, TrueHD/MAT (0x0C) 8ch, E-AC-3 (0x0A) 8ch, AC-3 (0x02) 6ch, AAC (0x06) 6ch, DTS (0x07) 6ch, DTS-HD (0x0B) 8ch ×2.
2. **`mode 0`** (`ExConfigureOutput(0,0,0,0xFF,0)`): rc=0, returned in about 1 ms. Kernel log:
   ```
   [AvControl] AudioOut: exclusive (pid=0xc8)
   [AvControl] audio: port:HDMI src:I2S_BS 48k 5.1  bs fmt:BITSTREAM AC3 once sp:AVR 5.1
   ```
3. **`stream pt`** (`ExPtOpen(0xFF,0,0,256,48000,14)` gave handle `0x2006001f`; 15 s of AC-3 bursts with no Output errors) under mode 0: silence, nothing on the soundbar display.
4. **`stream main`** (normal S16 stereo port) under mode 0: silence (PCM muted).
5. **Control:** `stream main` with no mode switch gave loud ticking. The bursts were played as PCM, so the data path itself was fine.
6. **Reset** (mode 0xFF): rc=0, about 110 ms; the log went back to `src:I2S 48k 5.1 24bit fmt:LPCM`.
7. **Settings menu, for comparison.** Switching Audio Format logged:
   ```
   Dolby Digital : src:I2S_BS 48k 2.0  bs fmt:ENCODE_DOLBY AC3
   DTS           : src:I2S_BS 48k 2.0  bs fmt:ENCODE_DTS DTS
   Dolby Atmos   : src:I2S_BS 768k 7.1 bs fmt:ENCODE_DOLBY_ATMOS DOLBY_ATMOS
   Linear PCM    : src:I2S 48k 5.1  24bit fmt:LPCM LPCM
   ```
   The Settings menu only uses ENCODE modes. Our call got BITSTREAM.
8. **Sweep**, all under mode 0: ExPtOpen type 0/1 × format 14/12 × byte order, 6 variants, 8 s each. All silent; HDMI stayed in `BITSTREAM AC3` throughout.
9. **System module scan.** All 552 modules under `/system/common/lib`, `/system/priv/lib`, `/system_ex/common_ex/lib`, `/system_ex/priv_ex/lib` and `/system/vsh` were searched for imports of the output-mode functions:
   - `citroncore.elf`: ExConfigureOutput, ExOpen, ExClose, ExGetMonitorInfo, ExGetOutputInfo, Open, Output, Close, SetVolume
   - `becore_ext.elf`, `becore_ext_esvm.elf`: ExConfigureOutput
   - `libSceSysBridge.sprx`: sceAvControlChangeOutputMode, sceAudioOutSysConfigureOutput
   - `SceShellCore.elf`: sceAvSettingSetAvOutputMode

   Nothing imports ExPtOpen or PtOpen.
10. **citroncore's sequence** (text `0x30eb2` onward): close any old port → `ExOpen(0xFF, TABLE[k].mode)` (call at va `0x31baa`) → `ExConfigureOutput(0, 0, TABLE[k].mode, TABLE[k].target, 0)` (call at va `0x30f3f`). TABLE at va `0x1458e0`, 8 bytes per row: `{0xFF,0xFF}`, `{1,1}`, `{0,1}`, `{3,1}`.
11. **`sony`** (that exact sequence for AC-3): ExOpen gave handle `0x2006001f`, ExConfigureOutput(0,0,0,1,0) rc=0, 468 bursts (15 s), close rc=0, reset rc=0. **The soundbar showed "Dolby Digital" and played the test file's tones.** This was the only run where the soundbar decoded anything: the sweep was silent, and the control run only gave ticking.

12. **Sample app, sandboxed** (Passthrough Lab, now the Passthrough page of Surround Sound Studio, `PPSA99051`, no sandbox escape):
    - The first build looked the functions up with `sceKernelLoadStartModule` + `sceKernelDlsym`. The module loaded (`module=0x69`), but every lookup failed, by name and by NID. A sandboxed process isn't allowed runtime symbol lookup.
    - Declaring the four functions `extern "C"` and linking against the payload SDK's `libSceAudioOut.so` stub, which already exports them, fixed it.
    - `sceAudioOutSysGetHdmiMonitorInfo` returned 0 and the same nine formats.
    - The receiver decoded **AC-3** (mode 0), **DTS core 768 kbps** (mode 2, IEC type 0x0B), **E-AC-3 640 kbps** (mode 3, 192 kHz, type 0x15), **AAC 5.1 ADTS** (mode 1, type 0x07), and a 30-second **E-AC-3 Atmos (JOC)** track cut from a Dolby test file.
13. **High-bitrate and Linear PCM formats (TrueHD, DTS-HD, PCM 2ch, PCM 6ch)**:
    - **DTS-HD**: Type IV preambles (`0x0211`, 2048 repetition period, 8192-byte bursts) over S16 stereo at 192 kHz. See item 14 for the mode.
    - **Modes 5 & 6 (Linear PCM)**: `sceAudioOutExOpen` returns `0x80260015` because it restricts to bitmask `0x61F`. However, `sceAudioOutExConfigureOutput(0, 0, mode, 1, 0)` switches HDMI to LPCM, while standard ports (`sceAudioOutOpen`) stream raw PCM (format 1 for stereo, format 2 for 8-channel with 6ch surround mapped).

## 5. What changed between the silent runs and the working one

Three things changed at once, so the single deciding factor isn't isolated:

- the port: `ExOpen` (format 1, S16 stereo) instead of `ExPtOpen` (format 14 or 12);
- the order: port opened before the switch, instead of after;
- the target: 1 instead of 0xFF.

The format code is the most likely cause, since 14 and 12 aren't PCM formats in the internal open. Copying Sony's sequence exactly is the safe choice.

14. **What each mode really is (kernel log, 2026-10-09).** `[AvControl] audio:` names the format the console set up:
    - mode 0 `BITSTREAM AC3`, 1 `BITSTREAM AAC`, 2 `BITSTREAM DTS`, 3 `BITSTREAM DDPLUS` (192 kHz), 4 `BITSTREAM DTS_HD_HR` (192 kHz), 9 `BITSTREAM LPCM` (48 kHz 5.1), 10 `BITSTREAM DDPLUS_JOC` (192 kHz), 5 and 6 `LPCM`.
    - An earlier version sent TrueHD MAT through mode 4 and DTS-HD through mode 10. The receiver showed nothing and the soundbar made clapping noises; the log explained it, since mode 4 is DTS-HD and mode 10 is Atmos. DTS-HD now uses mode 4.
    - No `ExOpen` mode is TrueHD, so the app does not play it. The Settings menu reaches Atmos through console-side encoding (`ENCODE_DOLBY_ATMOS`, 768 kHz 7.1), which an app cannot feed.
15. **The receiver follows format changes slowly.** Switching formats back to back left the receiver stuck on the previous one: a PCM clip labelled "AAC", and AAC silent until the receiver was restarted. The console log was correct each time (`LPCM` after the reset), so the receiver was the one lagging. The player now sends 400 ms of IEC 61937 null bursts after each switch and 500 ms before closing, waits 250 ms before the reset and 400 ms after it, and holds a new switch for 1 s after the last reset.
16. **AAC burst length.** Pd is in bits and must cover a whole 16-bit word, so an odd-length ADTS frame needs its length rounded up first. ffmpeg's spdif reader rejects the odd value ("Packet not ending at a 16-bit boundary").
