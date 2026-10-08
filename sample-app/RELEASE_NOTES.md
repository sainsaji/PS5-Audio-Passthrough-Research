First release of **Surround Sound Studio**, a PS5 homebrew app for home-theatre sound. Pre-release: please report problems.

**Passthrough page.** Plays test clips untouched over HDMI, so your soundbar or receiver decodes them:

- Dolby Digital, Dolby Digital Plus (including Atmos), DTS and AAC. All four were confirmed on a PS5 Pro (firmware 12.70) with a soundbar.
- Shows the formats your TV or receiver says it can decode, and every return code from the console.
- The bundled clips are channel-ID tones: each speaker beeps in turn, so a wrong channel map is easy to hear.

**Speaker Lab page** (new, not yet tested on hardware). EVO Player's speaker test suite:

- A test tone on each speaker, and automatic 5.1 and 7.1 sequences.
- A 360 degree sweep, and a 3D sound field you can fly around the room.
- Calibration through the DualSense microphone: the level, distance and delay of each speaker, as a report.

Switch pages with **L1 / R1**; the tabs at the top show where you are.

**Install:** copy `PPSA99051.ffpfsc` to `/data/homebrew/` and start it with ShadowMount+, or unpack `PPSA99051.zip` into `/data/homebrew/`. Close the app before replacing it.

**Known limits:** Dolby TrueHD and DTS-HD are not supported yet. While a bitstream plays, the console mutes the app's other sounds.
