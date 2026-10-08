# PS5 audio passthrough (bitstream over HDMI) from homebrew

A homebrew app on a jailbroken PS5 can send a **Dolby Digital (AC-3) bitstream** over HDMI to a soundbar or AV receiver. The receiver decodes it, not the console. This is the same mode the PS5's own media apps use, and different from the *Settings → Audio Format → Dolby* option, which only re-encodes the console's mixed sound.

It was confirmed on real hardware on **2026-10-08**: the soundbar's display switched to **"Dolby Digital"** while the test ran.

This repo has the recipe, how it was found, what failed on the way, and the tools used, so other homebrew projects can add passthrough.

- **For people:** read this file, then [docs/FINDINGS.md](docs/FINDINGS.md) for the full reverse-engineering notes.
- **For AI assistants and code generators:** [AGENTS.md](AGENTS.md) has the exact signatures, constants and call order in one place.

---

## Test setup

| | |
|---|---|
| Console | PS5 Pro, firmware **12.70**, jailbroken |
| Program | EVO Player, running as an app module (`.ffpfsc`, title `PPSA99039`), which had already escaped its sandbox (`uid 0`) |
| Audio chain | PS5 → HDMI → LG UltraGear monitor → soundbar with Dolby/DTS decoding |
| Test file | a 5.1 AC-3 track in an `.mp4`, demuxed with FFmpeg |

Only **AC-3** has been confirmed so far. The other formats below come from reading Sony's code and are **not tested yet**.

---

## The recipe (AC-3)

Do these in this order. The order matters: Sony's own player opens the port **before** it switches the output.

1. **Open the bitstream port**

   ```c
   int h = sceAudioOutExOpen(0xFF /* system user */, 0 /* mode: AC-3 */);
   ```

   This opens a special port that takes **S16 stereo, 48 kHz, 256 frames per call**.

2. **Switch HDMI audio to bitstream mode**

   ```c
   sceAudioOutExConfigureOutput(0, 0, 0 /* mode: AC-3 */, 1 /* target */, 0);
   ```

   From here on, ordinary PCM from your app is muted: the console gives your process exclusive use of the HDMI audio.

3. **Send the audio as IEC 61937 bursts**

   Wrap each AC-3 frame in an IEC 61937 burst (the standard S/PDIF and HDMI way to carry compressed audio inside PCM samples), and write it with the normal `sceAudioOutOutput(h, buf)`. One AC-3 frame covers 1536 samples, so one burst is 1536 stereo S16 samples (6144 bytes). That's six calls of 256 samples each.

   | 16-bit word | value |
   |---|---|
   | Pa | `0xF872` |
   | Pb | `0x4E1F` |
   | Pc | `0x0001` (AC-3), plus `bsmod << 8` (`bsmod` = low 3 bits of byte 5 of the frame) |
   | Pd | frame length **in bits** |
   | payload | the AC-3 frame as **big-endian** 16-bit words, then zeros to the end of the burst |

   Each word is stored as one little-endian S16 sample, so in memory the first bytes are `72 F8 1F 4E 01 00 ...`.

4. **Close and restore**

   ```c
   sceAudioOutOutput(h, NULL);                      // wait for the last block
   sceAudioOutExClose(h);
   sceAudioOutExConfigureOutput(0, 0, 0xFF, 0xFF, 0); // back to normal PCM output
   ```

None of these functions are in the public SDK headers. Resolve them at runtime with `sceKernelLoadStartModule("libSceAudioOut.sprx", ...)` and `sceKernelDlsym`, by name or by NID. The NIDs are in [AGENTS.md](AGENTS.md).

A working, hardware-tested reference is [reference/evo_pt_probe.c](reference/evo_pt_probe.c) (its `sony` command).

---

## Other formats (from Sony's code, not yet tested)

`sceAudioOutExOpen` and `sceAudioOutExConfigureOutput` share one `mode` number:

| mode | what it sets on HDMI | port the mode opens | status |
|---|---|---|---|
| 0 | AC-3 (Dolby Digital), 5.1 | 256 samples @ 48 kHz | **works** |
| 1 | AAC, 5.1 | 256 @ 48 kHz | untested |
| 2 | DTS, 5.1 | 256 @ 48 kHz | untested |
| 3 | E-AC-3 (Dolby Digital Plus), 7.1 | 1024 @ 192 kHz | untested |
| 4 | Sony code `0xF0`, 7.1 (maybe Dolby TrueHD) | 1024 @ 192 kHz | untested |
| 9 | Sony code `0x16`, 5.1 | 256 @ 48 kHz | untested |
| 10 | Sony code `0xF3`, 7.1 (maybe DTS-HD) | 1024 @ 192 kHz | untested |
| 5–8 | plain multichannel PCM | — (not accepted by `ExOpen`) | — |
| 0xFF | reset to normal | — | works |

E-AC-3 at 192 kHz matches the IEC 61937 standard for that format (a 4× sample rate), which is a good sign. TrueHD and DTS-HD normally need the HDMI "high bitrate" mode (8 channels at 192 kHz); whether modes 4 and 10 give that is unknown.

---

## What did *not* work (so you don't repeat it)

| Attempt | Result |
|---|---|
| IEC 61937 bursts on a normal PCM port, no mode switch | the receiver plays it as PCM: loud ticking/noise |
| `sceAudioOutExConfigureOutput` mode 0 first, then a normal PCM port | silence: the console mutes PCM once bitstream mode is on |
| The "passthrough" port `sceAudioOutExPtOpen` / `sceAudioOutPtOpen`, with format codes 14 and 12, types 0 and 1, both byte orders | silence, even though the console logged that HDMI was in bitstream mode. **No Sony module uses this port**, so treat it as a dead end. |
| Settings → Sound → Audio Format → Dolby / DTS | that's re-encoding (`ENCODE_DOLBY`, `ENCODE_DTS`), not passthrough |

An earlier write-up in the EVO Player project concluded passthrough was impossible for apps because the output-mode switch was permission-gated. That was wrong: the switch had never been tested with its return code logged. It returns 0 and works.

---

## How it was found, in short

1. AnyPS5's NID tool pointed to a public PS5 symbol list. That showed `libSceAudioOut` has more output-mode functions than had been tried.
2. Reading the decrypted 12.70 `libSceAudioOut` showed the "Ex" and "Sys" configure calls end in the same place: `sceAvControlChangeOutputMode` in `libSceAvSetting`.
3. A probe in EVO Player called the switch and logged `rc=0`. The kernel log (`[AvControl]`) confirmed `fmt:BITSTREAM AC3` and gave EVO exclusive use of the audio, but every port tried stayed silent.
5. `citroncore` uses `sceAudioOutExOpen`, not the passthrough port, opens it before switching, and uses target 1. Copying that exactly gave Dolby Digital on the soundbar.

The details, offsets and log lines are in [docs/FINDINGS.md](docs/FINDINGS.md).

## Tools

- [tools/plt2.py](tools/plt2.py): maps a PS5 ELF's import stubs to NIDs and lists every call site of a given NID.

No Sony binaries are included in this repo, only notes, offsets and our own code.

## Open questions

- Does it work without escaping the sandbox (a normal, unjailbroken-process homebrew)?
- E-AC-3, DTS, and modes 4/9/10: real names and whether they work.
- Did the receiver decode actual sound in the AC-3 test, or only lock onto the format? The display showed "Dolby Digital"; listening for the test tones is the next check.
- A/V sync: the receiver adds its own decode delay.
