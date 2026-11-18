# Xoxicoa Sampler — spec (v1.1)

## Loading and analysis (automatic)
- Drag and drop onto the window, or use **LOAD**. Accepts WAV, AIFF, MP3, FLAC, OGG, M4A/CAF (M4A on Mac only, through CoreAudio).
  Decoding and analysis run on a background thread, so the UI never freezes. Files are capped at 10 minutes.
  Audio stays at the file's own sample rate. Playback converts the rate (Hermite interpolation) or compensates inside the stretcher.
- **BPM:** spectral-flux onset envelope, then autocorrelation with a harmonic comb, refined over 8 beat multiples.
  Range is 70–180 BPM. **/2** and **x2** fix half-time or double-time readings; the override is saved with the project.
- **Key:** chromagram from 55 Hz to 2 kHz, correlated with the Krumhansl-Kessler major and minor profiles (24 keys).
- **16 cues:** onsets are picked from the flux peaks, then each is refined to the transient start (1 ms pre-roll) on a high-frequency emphasised envelope.
  The 16 strongest are kept with a minimum spacing (wider on long files, so cues spread across a whole song) and sorted by time.
  If a file has fewer than 16 transients, the gaps are filled on the 1/16 grid.
- **Waveform:** black on white. Drag the numbered markers to move a cue; it snaps to the quietest sample within 2 ms (hold Shift for no snap).
  Wheel zooms, drag pans, double-click returns to the full view. The selected pad's slice is shaded. Playheads show while pads play.
  **RESET CUES** puts all 16 cues back to the detected positions.

## Playing
- 16 pads (4×4). MIDI note = Pad Root (default 36) + pad − 1. Note 36 is labelled C1 here (middle C = C3). FL Studio's piano roll calls the same key C3, because FL labels middle C as C5.
  Computer keys 1 2 3 4 / Q W E R / A S D F / Z X C V (rows top to bottom). Mouse clicks on pads also play; clicking higher on a pad plays louder.
- Each pad plays monophonically (a retrigger chokes the previous hit with a 6 ms fade). 12 voices in total; when all are busy, the oldest releasing voice is stolen first.
- Velocity sets the level.

## Key and tempo (global)
- **Key Shift:** −12…+12 semitones, real time, including on notes already playing.
- **Key Target (snap):** pick a key and the sample is transposed from its detected key to the target (−6…+5 semitones, via the relative major/minor),
  plus Key Shift. The header shows the total pitch and the snap amount.
- **Host Sync:** time-stretches to host BPM ÷ sample BPM (clamped ×0.5…×2), with the pitch unchanged. Off = original tempo.
- **Volume:** −36…+6 dB.
- Engine: Signalsmith Stretch (MIT), default preset (120 ms block, 30 ms hop), one stereo instance per voice.
  With key 0 and sync off, a clean direct playback path is used instead (no stretching artefacts).
  A trigger pre-rolls the stretcher (outputSeek), so the pad starts exactly on the note with no latency.

## Per-pad (select a pad by clicking, playing or dragging its marker)
- **Mode:** One-Shot (plays its slice to the next cue, ignores note-off) · Gate (plays from the cue while held, past later cues) ·
  Loop (loops its slice while held; 3 ms crossfade at the loop seam).
- **Attack:** 0.5–500 ms. **Release:** 3 ms–2 s. In Gate and Loop, release is the fade after note-off.
  In One-Shot, it is the tail: the fade starts at the slice end and lets the following audio ring out.
- **Filter:** bipolar DJ filter. Left = low-pass (20 kHz down to 150 Hz), centre = open (bypassed), right = high-pass (20 Hz up to 6 kHz). 12 dB/oct.
- **Gain:** −24…+6 dB. **Choke Group:** Off or 1–4. A pad cuts the other pads in the same group (6 ms fade).
- All continuous pad parameters glide in 20 ms (no zipper noise).

## AUTO FLIP (v1.1)
- **AUTO** re-rolls every unlocked version from a fresh seed. Generator inputs:
  - the detected BPM and key,
  - the 16 cues, which are transients,
  - per-pad energy, used to make phrase sections and pick a motif.
- **Strategies**: REORDER, HALF-TIME, STUTTER, OCTAVES, GAPS, CHOP.
  - Each strategy has its own rhythm templates and its own odds of reverse, pitch, octave, gap, stutter and half-time.
  - Within one roll the six versions always get six different strategies.
  - No version keeps its previous strategy on the next roll.
- **Versions A-F**:
  - Click a tile to switch instantly; the next event already comes from the new version.
  - A tile shows its letter, its strategy and a mini pattern that follows Chaos/Density, plus a playhead.
  - Each tile also has its length (1/2, 1, 2, 4, 8 or 16 bars), ROLL and LOCK. A locked version keeps its strategy and seed through AUTO, and ROLL is disabled on it.
- **UNDO / HISTORY**: the last 24 rolls, saved in the project together with the seeds, strategies and locks.
- **CHAOS (0-100 %)**: every event has two random thresholds.
  - Above the first threshold the event uses its alternative chop.
  - Above the second it applies its effect: reverse, pitch or octave accent, half-time, gap or stutter.
  - So 0 % is the straight motif, and higher values add more variation step by step.
- **DENSITY (0-100 %)**: an event plays only when Density reaches its rank. Downbeats rank lowest, so they stay.
- **SWING (0-100 %)**: delays 16th off-beats by up to 1/32 note. The 32nd after them moves half as much.
- **Timing**:
  - Events sit on a 1/32-note grid and are scheduled sample-accurately from the host PPQ, so loops stay locked to the bar.
  - When the host is stopped, an internal clock at the host tempo (or the sample tempo) takes over.
- **Conform**:
  - Auto voices are always time-stretched to the host tempo, even when HOST SYNC is off.
  - Pitch = Key Shift + Key Target snap + the event's accent.
  - Pitch accents play as classic varispeed chops: exact pitch, and the grid sets the length.
- **Playback**:
  - AUTO PLAY latches and starts on the next host bar.
  - MIDI C0 (note 24) plays while held, starting on the note.
  - C#0-F#0 select A-F. These notes are reserved only when the pad root is above F#0; the default is C1.
  - The Auto line is monophonic (each hit chokes the previous one with a 6 ms fade) and uses each pad's release and filter.
- **Editable**: events store pad + offset, not sample positions. Moving a cue marker re-chops every version.
  Pads still play on top.
- **DRAG MIDI** exports the active version as notes on the pads (root note + pad), with Chaos, Density and Swing applied.
  Reverse, pitch and half-time are NOT in the MIDI.
- **DRAG WAV** renders the active version offline at the host tempo (24-bit, 48 kHz).

## Output and safety
- Stereo out (mono is supported). NaN guard. Soft clip above −0.9 dBFS that never exceeds 0 dBFS (transparent below that).
- The audio thread never allocates or blocks: it uses a try-lock to swap samples, and stretchers and scratch buffers are allocated and warmed up in prepareToPlay.
- The project saves the parameters, the sample's file path, the 16 cue positions and the BPM override. The audio itself is NOT embedded:
  if the file moves, the UI says "Missing sample" and you drop it again.

## Limits / honest notes
- Analysis is tuned on synthetic and simple loops. Dense mixes, swing, rubato or tempo changes can confuse BPM, key and cue picks.
  Use /2 and x2, Key Target, and drag the markers to fix them.
- Stretching above about ±7 semitones or ×1.5 audibly smears transients (normal for phase-vocoder stretching).
- A stretched trigger costs about 4 ms of CPU in one block on this box. At very small buffers (64 samples) a burst of triggers could crackle; use 256+ in FL.
- The sample tempo is constant (no warp markers). Cues are not quantised to the host grid.
- Keyboard pads only work when the plugin window has focus. FL's typing keyboard can grab the keys first.
- AUTO is generative: rolls are random by design, so the demo files and test strategies change on every run.
  The audit uses a fixed roll.
- Linux-built and tested offline only. The Mac build (CI) and FL Studio hosting have not been run yet.
