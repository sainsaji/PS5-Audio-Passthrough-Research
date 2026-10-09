![](https://img.shields.io/badge/Release-v01.000.003-blueviolet?style=flat-square) ![](https://img.shields.io/badge/PS5%20Hardware-Verified-0070d1?style=flat-square&logo=playstation&logoColor=white) ![](https://img.shields.io/badge/Firmware-12.70-blue?style=flat-square)

**01.000.003:** the pauses of silence the app sends between format changes were four times too long on the TrueHD port, because its 768 kHz label is not the rate it really consumes frames at (192 kHz). They are now the intended length. The READMEs and the write-up also say plainly that Speaker Lab is EVO Player's Surround Sound Studio and that TrueHD works.

**01.000.002:** Dolby TrueHD now plays. It goes out as a MAT bitstream on a 768 kHz port, opened with the console's system audio calls (`sceAudioOutSysOpen` and `sceAudioOutSysConfigureOutput`), the route Sony's Blu-ray player uses. Hardware-tested on a PS5 Pro, firmware 12.70: it plays and the receiver shows "Dolby Audio".

**01.000.001:** a new icon, DTS-HD sent in the right console mode (it was going out as Atmos), AAC bursts that are valid for every frame length, and a settle time between format changes.

First release of **Surround Sound Studio**, a PS5 homebrew app for home-theatre sound. Please report problems.

**Passthrough page.** Plays test clips untouched over HDMI, so your soundbar or receiver decodes them:

- Dolby Digital, Dolby Digital Plus (including Atmos), DTS, AAC, Dolby TrueHD, DTS-HD, and Linear PCM (2ch and 6ch surround). TrueHD goes out as MAT through the console's system audio calls, the same route Sony's Blu-ray player uses; the receiver shows "Dolby Audio".
- Shows the formats your TV or receiver says it can decode, and every return code from the console.
- The bundled clips are channel-ID tones: each speaker beeps in turn, so a wrong channel map is easy to hear.
- Displays an "ATMOS" badge for Dolby Atmos clips (`local-atmos.eac3`).

**Speaker Lab page** (new, not yet tested on hardware). EVO Player's speaker test suite:

- A test tone on each speaker, and automatic 5.1 and 7.1 sequences.
- A 360 degree sweep, and a 3D sound field you can fly around the room.
- Calibration through the DualSense microphone: the level, distance and delay of each speaker, as a report.

Switch pages with **L1 / R1**; the tabs at the top show where you are.

**Install:** copy `PPSA99051.ffpfsc` to `/data/homebrew/` and start it with ShadowMount+, or unpack `PPSA99051.zip` into `/data/homebrew/`. Close the app before replacing it.

**Known limits:** While a bitstream plays, the console mutes the app's other sounds. After many format changes in a row the receiver can get stuck on its last format (a PCM clip labelled AAC, AAC silent); restarting the receiver clears it.
