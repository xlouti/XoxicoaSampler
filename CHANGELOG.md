# Changelog

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
