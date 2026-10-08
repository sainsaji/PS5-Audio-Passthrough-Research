# PS5 audio passthrough (bitstream over HDMI) from homebrew

PS5 homebrew can send **Dolby Digital, Dolby Digital Plus (including Atmos), DTS and AAC** over HDMI untouched, so a soundbar or AV receiver does the decoding instead of the console. This is the same mode the PS5's own media apps use. It's different from *Settings → Sound → Audio Format → Dolby*, which only re-encodes the console's mixed sound.

It works from an **ordinary sandboxed homebrew app**: no sandbox escape and no kernel access. Confirmed on real hardware on **2026-10-08**: the receiver showed the format and played the decoded audio.

This repo has the recipe, a working sample app, how it was found, what failed on the way, and the tools used.

- **For people:** this file, then [sample-app/](sample-app/) for working code and [docs/FINDINGS.md](docs/FINDINGS.md) for the reverse-engineering notes.
- **For AI assistants and code generators:** [AGENTS.md](AGENTS.md) has the exact signatures, constants and call order in one place.

---

## What works

| Format | Console mode | Port | Result |
|---|---|---|---|
| Dolby Digital (AC-3) 5.1 | 0 | 256 samples @ 48 kHz | **works** |
| AAC 5.1 (ADTS) | 1 | 256 @ 48 kHz | **works** |
| DTS 5.1 (core) | 2 | 256 @ 48 kHz | **works** |
| Dolby Digital Plus (E-AC-3) 5.1 | 3 | 1024 @ 192 kHz | **works** |
| Dolby Digital Plus with Atmos | 3 | 1024 @ 192 kHz | **plays** (the receiver's Atmos indicator not yet checked) |
| Dolby TrueHD 5.1 | Sys 5 | 1024 @ 768 kHz, 8ch | **plays** (MAT framing, console log `BITSTREAM MAT`; the receiver shows "Dolby Audio") |
| DTS-HD 5.1 | 4 | 1024 @ 192 kHz | sent as DTS-HD (Type IV framing, 8192-byte bursts); the console labels mode 4 "DTS-HD HR" |
| Linear PCM 2ch (stereo) | 5 | 256 @ 48 kHz | **works** (S16 stereo) |
| Linear PCM 6ch (5.1 surround) | 6 | 256 @ 48 kHz | **works** (mapped to S16 8-channel port) |

Test setup: PS5 Pro, firmware **12.70**, jailbroken (homebrew launched through ShadowMount+). Audio chain: PS5 → HDMI → LG UltraGear monitor → soundbar with Dolby/DTS decoding. The first test ran inside EVO Player after it had escaped its sandbox. Every later test ran in [the sample app](sample-app/), which is a normal sandboxed app.

---

## The recipe

Do these in this order. The order matters: Sony's own player opens the port **before** it switches the output.

1. **Open the bitstream port for the format**

   ```c
   int h = sceAudioOutExOpen(0xFF /* system user */, mode);
   ```

   The port always takes **S16 stereo**. Its grain and rate come from the mode: 256 frames at 48 kHz for modes 0–2, 1024 frames at 192 kHz for mode 3.

2. **Switch HDMI audio to bitstream mode**

   ```c
   sceAudioOutExConfigureOutput(0, 0, mode, 1 /* target */, 0);
   ```

   From here on, ordinary PCM from your app is muted: the console gives your process exclusive use of the HDMI audio.

3. **Send the audio as IEC 61937 bursts** with the normal `sceAudioOutOutput(h, buf)`

   IEC 61937 is the standard S/PDIF and HDMI way to carry compressed audio inside PCM samples. Each coded frame becomes one burst: four header words, the frame as **big-endian** 16-bit words, then zeros. A burst lasts exactly as long as the audio it carries.

   | Format | Pc (data type) | Pd (length) | Burst size |
   |---|---|---|---|
   | AC-3 | `0x0001 \| bsmod << 8` | bits | 6144 bytes (1536 samples) |
   | E-AC-3 | `0x0015` | **bytes** | 24576 bytes (6 audio blocks, at 192 kHz) |
   | DTS, 512 / 1024 / 2048 samples | `0x000B` / `0x000C` / `0x000D` | bits | samples × 4 bytes |
   | AAC (ADTS frame, header included) | `0x0007` | bits | 4096 bytes (1024 samples) |
   | DTS-HD | `0x0211` | **bytes** | 8192 bytes (Type IV preamble, 192 kHz) |

   Pa = `0xF872`, Pb = `0x4E1F`. Each word is stored as one little-endian sample, so a burst starts `72 F8 1F 4E ...` in memory.

4. **Close and restore**

   ```c
   sceAudioOutOutput(h, NULL);                        // wait for the last block
   sceAudioOutExClose(h);
   sceAudioOutExConfigureOutput(0, 0, 0xFF, 0xFF, 0); // back to normal PCM output
   ```

**Link these functions; don't look them up at runtime.** They aren't in the SDK headers, but the PS5 payload SDK's `libSceAudioOut` stub exports them, so `extern "C"` declarations are enough. `sceKernelDlsym` is refused in a sandboxed app (it only worked in EVO after its sandbox escape). Signatures and NIDs are in [AGENTS.md](AGENTS.md).

`sceAudioOutSysGetHdmiMonitorInfo(1, buf, 0x180)` also works sandboxed. It returns the TV's or receiver's EDID audio list, so an app can check that the receiver supports a format before switching to it.

Working code: [sample-app/src/passthrough/iec61937.cpp](sample-app/src/passthrough/iec61937.cpp) (packing) and [sample-app/src/platform/ps5/bitstream_out.cpp](sample-app/src/platform/ps5/bitstream_out.cpp) (console calls).

---

## The sample app

[sample-app/](sample-app/) is **Surround Sound Studio**, an app on the ps5-homebrew-ui kit with two pages. **Passthrough** bundles royalty-free 5.1 channel-ID clips (Dolby Digital, Dolby Digital Plus, DTS, AAC), shows your receiver's supported formats, and plays any clip as a bitstream, with every return code on screen. **Speaker Lab** is EVO Player's speaker test suite: tones per speaker, 5.1 / 7.1 walks, a 3D sound field and DualSense microphone calibration. Build and install steps are in its README.

---

## Other Modes and Implementation Notes

From the disassembly of `libSceAudioOut` (FW 12.70):

| mode | what it sets on HDMI | port | notes |
|---|---|---|---|
| 4 | Sony code `0xF0`, 7.1 | 1024 @ 192 kHz | DTS-HD (console log: `BITSTREAM DTS_HD_HR`; Type IV framing, 8192 bytes) |
| 5 | LPCM, 2.0 | 256 @ 48 kHz | Linear PCM stereo (`sceAudioOutOpen` format 1) |
| 6 | LPCM, 5.1 | 256 @ 48 kHz | Linear PCM surround (`sceAudioOutOpen` format 2, 8ch layout) |
| 7–8 | LPCM, 7.1 | 256 @ 48 kHz | Linear PCM 8ch |
| 9 | Sony code `0x16`, 5.1 | 256 @ 48 kHz | unverified |
| 10 | Sony code `0xF3`, 7.1 | 1024 @ 192 kHz | Dolby Atmos (console log: `BITSTREAM DDPLUS_JOC`) |
| 0xFF | reset to normal | | works |

DTS-HD (mode 4) uses Type IV preambles (subtype 2, 2048 repetition period, 8192-byte bursts) over the 1024-grain @ 192 kHz carrier. Dolby TrueHD is not on the Ex modes (mode 9 logs `BITSTREAM LPCM`, 4 and 10 are DTS-HD and Atmos); it uses `sceAudioOutSysOpen(0xFF, 5)` (a 768 kHz port) and `sceAudioOutSysConfigureOutput(1, 0, 5, 1, 0)`, which Sony's Blu-ray player uses. Linear PCM modes 5 and 6 are configured with `sceAudioOutExConfigureOutput` while streaming uncompressed audio via standard ports.

---

## What did *not* work (so you don't repeat it)

| Attempt | Result |
|---|---|
| IEC 61937 bursts on a normal PCM port, no mode switch | the receiver plays it as PCM: loud ticking/noise |
| `sceAudioOutExConfigureOutput` first, then a normal PCM port | silence: the console mutes PCM once bitstream mode is on |
| The "passthrough" port `sceAudioOutExPtOpen` / `sceAudioOutPtOpen` (format codes 14 and 12, types 0 and 1, both byte orders) | silence, though HDMI was in bitstream mode. **No Sony module uses this port**: a dead end. |
| Looking the functions up with `sceKernelDlsym` in a sandboxed app | refused: link them instead |
| Settings → Sound → Audio Format → Dolby / DTS | re-encoding (`ENCODE_DOLBY`, `ENCODE_DTS`), not passthrough |

An earlier write-up in the EVO Player project concluded passthrough was impossible for apps because the output-mode switch was permission-gated. That was wrong: the switch had never been tested with its return code logged.

---

## How it was found, in short

1. AnyPS5's NID tool pointed to a public PS5 symbol list. That showed `libSceAudioOut` has more output-mode functions than had been tried.
2. Reading the decrypted 12.70 `libSceAudioOut` showed the "Ex" and "Sys" configure calls end in the same place: `sceAvControlChangeOutputMode` in `libSceAvSetting`.
3. A probe in EVO Player called the switch and logged `rc=0`. The kernel log (`[AvControl]`) confirmed `fmt:BITSTREAM AC3`, but every port tried stayed silent.
4. Scanning all 552 system modules for the output-mode function found **`citroncore.elf`** (Sony's streaming media core) using it.
5. `citroncore` uses `sceAudioOutExOpen`, opens it before switching, and uses target 1. Copying that exactly gave Dolby Digital on the soundbar, and the sample app then confirmed the other formats from a sandboxed app.

The details, offsets and log lines are in [docs/FINDINGS.md](docs/FINDINGS.md).

## Tools

- [tools/make-clips.sh](tools/make-clips.sh): generates the sample app's channel-ID test clips with ffmpeg (and, optionally, a local-only clip cut from an Atmos file you own).
- [tools/plt2.py](tools/plt2.py): maps a PS5 ELF's import stubs to NIDs and lists every call site of a given NID.

No Sony binaries or Dolby/DTS sample content are included, only notes, offsets, our own code and our own generated tones.

## Open questions

- Dolby TrueHD: why the receiver shows "Dolby Audio" instead of "Dolby TrueHD" (the MAT framing is simplified), and the Atmos path (mode 10 is E-AC-3 JOC; MAT Atmos is untried).
- Does the receiver light its Atmos indicator for the Dolby Digital Plus Atmos clip?
- A/V sync: the receiver adds its own decode delay, which a video player has to account for.
