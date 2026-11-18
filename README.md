# Xoxicoa Sampler
Real-time performance sampler in the style of Serato Sample (AU / VST3 / Standalone, instrument).

Drop a WAV, AIFF or MP3 (FLAC, OGG and M4A also load). The sampler then:
- detects the **BPM** and **key**,
- finds **16 cue points** on the strongest transients and maps them to **16 pads**,
- lets you play the pads from MIDI (C1 = note 36 and up) or the computer keyboard (1-4 / Q-R / A-F / Z-V),
- shifts the key ±12 semitones, or snaps it to a chosen key, in real time,
- syncs the sample to the host tempo, independently of the key (Signalsmith Stretch),
- **AUTO** (v1.1): one press builds six different chopped flips (versions A-F) of the sample, in your project's tempo and key.
  Each flip has its own length (1/2-16 bars), re-roll and lock. It also has undo/history, Chaos/Density/Swing, bar-synced AUTO PLAY,
  MIDI C0 trigger, and drag-out MIDI/WAV.

UI is the inverse of Xoxicoa FX: white is the main colour, black is secondary. It uses the same responsive knobs.

Read SPEC.md for every control and its honest limits, and TEST_RESULTS_LINUX.txt for the offline test and knob audit.
`.github/workflows/build-mac.yml` builds universal (arm64 + x86_64) AU/VST3/Standalone on GitHub's macos-15-intel runner.

C++17 / CMake ≥ 3.22 / JUCE 8.0.4 (fetched on configure). Third-party code: ThirdParty/ (MIT, see VERSIONS.txt).
No repository has been pushed or created. JUCE licence review is required before distribution.
