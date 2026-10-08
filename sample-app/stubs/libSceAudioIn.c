/* Surround Sound Studio - import stub for libSceAudioIn (the DualSense mic).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The payload SDK ships no stub for this module. Like the SDK's own stubs this
 * is only a name list: the linker resolves the calls against it and the app
 * builder turns them into imports of libSceAudioIn.sprx. The bodies never run.
 */
int sceAudioInOpen(void) { return 0; }
int sceAudioInInput(void) { return 0; }
int sceAudioInGetSilentState(void) { return 0; }
int sceAudioInClose(void) { return 0; }
