# Xoxicoa Sampler
Real-time performance sampler in the style of Serato Sample (AU / VST3 / Standalone, instrument). Version 1.2.2.

Drop a WAV, AIFF or MP3 (FLAC, OGG and M4A also load). The sampler then:
- detects the **sample BPM** and **sample key**,
- finds cue points on onsets (syllable-aware, silence-trimmed, zero-crossing snapped) and maps them to **16 pads**,
- lets you play the pads from MIDI (C1 = note 36 and up) or the computer keyboard (1-4 / Q-R / A-F / Z-V),
- shifts the key ±12 semitones, or snaps it to a chosen **PROJECT KEY**, in real time (all playback: pads, GENERATE chops, AUTO),
- syncs the sample to the **PROJECT BPM** from the host playhead (AudioPlayHead, every block), independently of the key (Signalsmith Stretch),
- **GENERATE** (v1.2): one press builds a new randomised set of 1–16 chops, including slow/long and half-time chops, snapped to the project BPM grid and keyed to PROJECT KEY. A **THRESHOLD** knob rejects silent slices and trims leading/trailing silence,
- **AUTO** (v1.1+): one press builds six different chopped flips (versions A-F) of the sample, in the project's tempo and key, using only non-silent slices.
  Each flip has its own length (1/2-16 bars), re-roll and lock. It also has undo/history, Chaos/Density/Swing, bar-synced AUTO PLAY,
  MIDI C0 trigger, and drag-out MIDI/WAV,
- **VOCAL FX** (v1.2): reverb, tempo-synced delay (with ping-pong), pitch/octave, formant, saturation, HP/LP filter, host-synced stutter, chorus and width,
- **GHOST VOICE** (v1.2): a background vocal pad with Amount, Swell, Slide, Tone and Mix, snapped to PROJECT KEY.

UI is the inverse of Xoxicoa FX: white is the main colour, black is secondary. It uses the same responsive knobs.
The header shows SAMPLE BPM/KEY and PROJECT BPM/KEY separately, so the file's analysis is never mistaken for the FL project.
A SAMPLE KEY override (Auto + 24 keys) sits next to the detected key and is used for snap when set.
The editor is 1240×760 by default so it fits a 13" MacBook Air; drag the corner to scale it 70–150% (saved with the project).

Read SPEC.md for every control and its honest limits, FEATURE_LIST.txt for the v1.2 intended feature list,
and TEST_RESULTS_LINUX.txt for the offline test and knob audit.
`.github/workflows/build-mac.yml` builds universal (arm64 + x86_64) AU/VST3/Standalone on GitHub's macos-15-intel runner,
with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15` (Catalina) as the source of truth. Do not pass a different target on the cmake command line.

C++17 / CMake ≥ 3.22 / JUCE 8.0.4 (fetched on configure). Third-party code: ThirdParty/ (MIT, see VERSIONS.txt).
No repository has been pushed from this fix (patches are local format-patch files only). JUCE licence review is required before distribution.
