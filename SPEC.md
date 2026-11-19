# Xoxicoa Sampler — spec (v1.2.2)

## Loading and analysis (automatic)
- Drag and drop onto the window, or use **LOAD**. Accepts WAV, AIFF, MP3, FLAC, OGG, M4A/CAF (M4A on Mac only, through CoreAudio).
  Decoding and analysis run on a background thread, so the UI never freezes. Files are capped at 10 minutes.
  Audio stays at the file's own sample rate. Playback converts the rate (Hermite interpolation) or compensates inside the stretcher.
- **BPM:** spectral-flux onset envelope, then autocorrelation with a harmonic comb, refined over 8 beat multiples.
  Range is 70–180 BPM. **/2** and **x2** fix half-time or double-time readings; the override is saved with the project.
- **Key:** chromagram from 55 Hz to 2 kHz, correlated with the Krumhansl-Kessler major and minor profiles (24 keys).
- **Cues / chops:** flux-first onsets, with syllable-band peaks only when they sit more than 40 ms from a flux peak.
  Each hit is refined to the transient start (1 ms pre-roll), snapped to a zero-crossing, and has leading/trailing silence
  trimmed against the **THRESHOLD** (RMS + vocal-band energy, default 10%). Default load does not grid-snap mid-slice
  (that was putting cues in rests). Leftover onsets fill remaining slots, then a 16th-note grid. Silent or tiny files still get 16 grid cues.
- **GENERATE CHOPS:** press **GENERATE** for a new randomised set (COUNT 1–16) each time. Slices are onset/syllable-aware,
  snapped to the **PROJECT BPM** grid, include slow/long chops and half-time / time-stretch rates locked to that grid,
  and are mapped to pads 1…N. Empty pads are hidden. Threshold is re-applied when the knob moves.
- **Waveform:** black on white. Drag the numbered markers to move a cue; it snaps to the quietest sample within 2 ms (hold Shift for no snap).
  Wheel zooms, drag pans, double-click returns to the full view. The selected pad's slice is shaded. Playheads show while pads play.
  Silent pads are hidden. **RESET CUES** puts all 16 cues back to the detected, re-trimmed positions.

## Playing
- 16 pads (4×4). MIDI note = Pad Root (default 36) + pad − 1. Note 36 is labelled C1 here (middle C = C3). FL Studio's piano roll calls the same key C3, because FL labels middle C as C5.
  Computer keys 1 2 3 4 / Q W E R / A S D F / Z X C V (rows top to bottom). Mouse clicks on pads also play; clicking higher on a pad plays louder.
- Each pad plays monophonically (a retrigger chokes the previous hit with a 6 ms fade). 12 voices in total; when all are busy, the oldest releasing voice is stolen first.
- Velocity sets the level.

## Key and tempo (global)
- The header shows **SAMPLE BPM / SAMPLE KEY** (file analysis, plus any /2 x2 override) and **PROJECT BPM / PROJECT KEY** separately.
  PROJECT BPM is the host playhead tempo (`AudioPlayHead::getPosition()->getBpm()`), read on every `processBlock`
  (never cached in `prepareToPlay`, never taken from the sample). GENERATE and AUTO also read the playhead live, so they
  work before the first audio block. PROJECT KEY is the manual Key Target; FL Studio does not expose a project key.
  When Key Target is Off the header reads "Set key".
- **SAMPLE KEY override:** Auto (use detection) or any of 24 keys, next to the detected SAMPLE KEY in the header.
  When it is not Auto, PROJECT KEY snap uses this value instead of the analysis. Saved with the project.
- **Key Shift:** −12…+12 semitones, real time, including on notes already playing.
- **PROJECT KEY (Key Target snap):** pick a key and the sample is transposed from its detected key to the target (−6…+5 semitones, via the relative major/minor),
  plus Key Shift. Integer semitones only. The snap is applied to every voice: pads, GENERATE chops, and AUTO.
  The header shows the total pitch and the snap amount.
- **Host Sync:** time-stretches to PROJECT BPM ÷ SAMPLE BPM (clamped ×0.5…×2), with the pitch unchanged. Off = original tempo.
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

## AUTO FLIP (v1.1, silence-gated in v1.2)
- **AUTO** re-rolls every unlocked version from a fresh seed. Generator inputs:
  - the sample BPM (and host/project BPM for playback),
  - PROJECT KEY (Key Target) plus Key Shift,
  - the current cues and trimmed slice ends,
  - per-pad energy after the silence threshold — silent slices are not eligible.
    If nothing passes the gate, the single loudest pad is used as a last resort (so a continuous tone still flips).
    A truly silent file produces no events.
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

## VOCAL FX (v1.2)
Bus effects after the voices, before the master clip. Every knob is audible (see the knob audit).
- **Reverb:** Size, Mix (`juce::Reverb`).
- **Delay:** host-synced divisions 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D (default), 1/4T, 1/4, 1/4D, 1/2.
  Feedback, Mix, Ping-Pong.
- **Pitch:** −12…+12 st (4-grain ~40 ms shifter, low latency — Signalsmith is not used here because its default block is 120 ms).
- **Formant:** −12…+12 st, independent of pitch (stereo IIR/EQ shift).
- **Drive:** saturation. **Filter:** bipolar DJ HP/LP + cutoff. **Stutter:** host-synced gate (1/8, 1/16, 1/32 by amount).
- **Chorus** and **Width** (0 = mono, 50% = natural, 100% = wide).
- When every mix/offset is at its off or neutral position the section is skipped (idle early-out).

## GHOST VOICE (v1.2)
A wet-only background vocal pad, snapped to PROJECT KEY, mixed in parallel.
- **Amount:** layer density (unison + octave + fifth).
- **Swell:** BPM-synced fade in and out.
- **Slide:** pitch slide up from below as the swell opens.
- **Tone:** low-pass brightness. **Mix:** wet level.
- Built as a pitch-shifted unison layer into a long diffuse reverb.

## Output and safety
- Stereo out (mono is supported). NaN guard. Soft clip above −0.9 dBFS that never exceeds 0 dBFS (transparent below that).
- The audio thread never allocates or blocks: it uses a try-lock to swap samples, and stretchers and scratch buffers are allocated and warmed up in prepareToPlay.
- The project saves the parameters, the sample's file path, the 16 cue positions and slice ends, pad rates, and the BPM override. The audio itself is NOT embedded:
  if the file moves, the UI says "Missing sample" and you drop it again.
- The editor defaults to 1240×760 (fits a 13" Air; stays inside 1400×780). A corner resizer scales it 70–150% at a fixed aspect ratio; the scale is saved with the project.

## Limits / honest notes
- Analysis is tuned on synthetic and simple loops. Dense mixes, sparse/ambient vocals, swing, rubato or tempo changes can confuse SAMPLE BPM, SAMPLE KEY and cue picks.
  The Mazzy Star "Into Dust" vocal, labelled 86 BPM D maj, can be read as a different BPM/key. Use /2, x2 or the BPM override, set PROJECT KEY by ear, and drag the markers.
  There is no sample-key override yet.
- FL Studio does not expose project key. PROJECT KEY is always the manual selector.
- Snap is integer semitones only (relative major/minor, wrap −6…+5). A min and C maj share a centre, so that snap is 0 st by design. No cents fine-tune.
- Stretching above about ±7 semitones or ×1.5 audibly smears transients (normal for phase-vocoder stretching).
- A stretched trigger costs about 4 ms of CPU in one block on this box. At very small buffers (64 samples) a burst of triggers could crackle; use 256+ in FL.
- The sample tempo is constant (no warp markers). Default cues are not forced onto the host grid; GENERATE chops are.
- Keyboard pads only work when the plugin window has focus. FL's typing keyboard can grab the keys first.
- AUTO and GENERATE are generative: rolls are random by design, so the demo files and test strategies change on every run.
  The audit uses a fixed roll where it matters.
- Linux-built and tested offline. The Mac build (CI, 10.15 target) and FL Studio hosting have not been run for v1.2 yet.
