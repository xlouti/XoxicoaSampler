# Changelog

## v1.2.2 (2026-11-19) - label overlap fixes
- KEY + TEMPO: KEY SHIFT / VOLUME labels no longer touch the panel title; **HOST SYNC** label and button moved below PAD ROOT NOTE (the label was hidden under the dropdown).
- PAD panel: **MODE** label no longer overlaps the "PAD n" title; MODE / CHOKE GROUP and the ATTACK / RELEASE / FILTER / GAIN knobs moved down to match.
- PADS: the slice time (e.g. "S 1:04.15") now has its own full-width line above the key chip, so it is no longer cut off to "S 1:...".
- GENERATE CHOPS: THRESHOLD label is inside the panel (it was drawn over the bottom of the waveform).
- VOCAL FX: SIZE label clears the panel title; second knob row spaced so its labels clear the first row's values.
- AUTO FLIP: the ACTIVE readout is fitted to stop before the CHAOS / DENSITY / SWING labels.
- Layout only: same 1240×760 window, 70–150% corner resize, white background with black controls. No DSP or parameter changes; saved projects load unchanged.
- First release built by GitHub Actions (macos-15-intel, universal x86_64+arm64, VST3 + AU, CMAKE_OSX_DEPLOYMENT_TARGET=10.15).

## v1.2.1 (2026-11-18) - compact window, resize, sample-key override
- Editor default size is **1240×760**, designed to fit a 13" MacBook Air (1440×900, ~800 px usable height, must stay inside 1400×780). The old 1120×1260 window was getting cut off.
- Layout: full-width waveform on top; then columns for pads (4×4), GENERATE + AUTO FLIP, and KEY+TEMPO / pad settings; VOCAL FX and GHOST VOICE along the bottom.
- The window is **resizable** from the bottom-right corner, locked to a fixed aspect ratio, and scales the UI 70–150% (content is laid out at the design size and scaled with an AffineTransform so the host can still apply its own setScaleFactor). The scale is saved with the project.
- **SAMPLE KEY** override (Auto + 24 keys) sits next to the detected sample key. Snap math uses the override when it is not Auto. Saved with the project as an automatable parameter.


## v1.2 (2026-11-18) - chops, project BPM/key, vocal FX, ghost voice
- **PROJECT vs SAMPLE readouts:** the header now shows SAMPLE BPM/KEY (file analysis) and PROJECT BPM/KEY (the FL / host project) separately, so the two are never confused. PROJECT KEY reads "Set key" until you pick one.
- **Project BPM** is read from the host `AudioPlayHead` on every `processBlock` (and live for GENERATE / AUTO before the first block). It is never cached in `prepareToPlay` and never taken from the sample's analysis.
- **PROJECT KEY** is the manual Key Target (FL does not expose key). Snap is nearest −6…+5 st via the relative major/minor, integer semitones, and is applied to pads, GENERATE chops and AUTO.
- **Chop detection rewrite:** flux-first, syllable-aware, zero-crossing snap, short start fades, silence trim. Default load no longer plants cues in rests by mid-slice grid-fill.
- **GENERATE CHOPS:** new random set each press, count 1–16, including slow/long and half-time chops synced to project BPM and keyed to PROJECT KEY.
- **THRESHOLD** knob (default 10%): RMS/vocal-band gate and trim, used by default cues, GENERATE and AUTO.
- **AUTO** only picks non-silent slices; event offsets are snapped out of trimmed silence. A last-resort loudest pad keeps continuous tones flip-able.
- **VOCAL FX:** reverb size/mix, tempo-synced delay (divisions + feedback + mix + ping-pong), pitch/octave, formant, drive, bipolar filter, host-synced stutter, chorus, width. Every knob is audible.
- **GHOST VOICE:** Amount, Swell, Slide, Tone, Mix — a wet-only background vocal pad that swells, slides in, and sits in the project key.
- UI remains white-on-black; editor grows to 1120×1260. macOS deployment target is 10.15 (Catalina).
- Linux offline tests and a knob audit of the new parameters are in TEST_RESULTS_LINUX.txt.


## v1.1.1 (2026-11-18) - Catalina
- Minimum macOS is 10.15. The 1.1.0 build required 11.0, so Catalina refused to load it (`_os_workgroup_join` was a strong import).

## v1.1 (2026-11-17) - AUTO
- **AUTO** button: analyses the sample's BPM, key, transients (the 16 cues) and phrase energy, then builds a chopped, playable flip
  conformed to the host BPM and to the project key (Key Target). Each press uses a new seed and gives a different flip.
- Six strategies: REORDER, HALF-TIME, STUTTER, OCTAVES, GAPS, CHOP. Each one combines phrase reordering (A A' B A''), half-time
  sections, rhythmic chop templates, reverses, pitch accents and octave jumps, gaps and stutter fills in different amounts.
- **Versions A-F**: six premade flips are visible together and switch instantly. Each has its own length
  (1/2, 1, 2, 4, 8 or 16 bars, exact to the host tempo), a re-roll and a lock.
- **UNDO and HISTORY**: the last 24 rolls are saved in the project.
- **Knobs**: CHAOS (variation), DENSITY (how many hits play) and SWING (16th off-beats).
- **Playback**: a latching AUTO PLAY button starts on the next host bar. MIDI C0 plays the version while held;
  C#0-F#0 select A-F (when the pad root note is above F#0).
- **Editable**: Auto events point at pads plus an offset, so moving a cue marker re-chops every version.
- **Export**: DRAG MIDI and DRAG WAV export the active version (click instead of dragging to write the file and reveal it).
- The window grows to 1120x920 to fit the AUTO FLIP panel.

## v1.0 (2026-11-17)
- First version: drag-and-drop sampler with automatic BPM, key and 16-cue detection, 16 pads (MIDI and computer keyboard),
  draggable cue markers, real-time key shift and key snap, host tempo sync (Signalsmith Stretch), One-Shot/Gate/Loop pads with
  attack, release, filter, gain and choke groups, and an inverted (white-first) UI.
