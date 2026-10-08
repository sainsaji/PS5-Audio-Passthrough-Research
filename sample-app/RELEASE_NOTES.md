**01.000.001:** a new icon, DTS-HD sent in the right console mode (it was going out as Atmos), AAC bursts that are valid for every frame length, and a settle time between format changes. TrueHD is removed.

First release of **Surround Sound Studio**, a PS5 homebrew app for home-theatre sound. Pre-release: please report problems.

**Passthrough page.** Plays test clips untouched over HDMI, so your soundbar or receiver decodes them:

- Dolby Digital, Dolby Digital Plus (including Atmos), DTS, AAC, DTS-HD, and Linear PCM (2ch and 6ch surround). Dolby TrueHD is not possible: the console has no bitstream mode for it.
- Shows the formats your TV or receiver says it can decode, and every return code from the console.
- The bundled clips are channel-ID tones: each speaker beeps in turn, so a wrong channel map is easy to hear.
- Displays an "ATMOS" badge for Dolby Atmos clips (`local-atmos.eac3`).

**Speaker Lab page** (new, not yet tested on hardware). EVO Player's speaker test suite:

- A test tone on each speaker, and automatic 5.1 and 7.1 sequences.
- A 360 degree sweep, and a 3D sound field you can fly around the room.
- Calibration through the DualSense microphone: the level, distance and delay of each speaker, as a report.

Switch pages with **L1 / R1**; the tabs at the top show where you are.

**Install:** copy `PPSA99051.ffpfsc` to `/data/homebrew/` and start it with ShadowMount+, or unpack `PPSA99051.zip` into `/data/homebrew/`. Close the app before replacing it.

**Known limits:** While a bitstream plays, the console mutes the app's other sounds. After many format changes in a row the receiver can get stuck on its last format (a PCM clip labelled AAC, AAC silent); restarting the receiver clears it. Dolby TrueHD is not possible, because the console has no bitstream mode for it.
