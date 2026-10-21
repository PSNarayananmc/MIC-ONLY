# VoiceCraft Studio — User Guide

## The five tabs

### Studio
The operational home screen.

* **Input device** — dropdown of every PulseAudio source. Monitors are
  hidden unless nothing else exists; your real microphone is preselected.
* **START / STOP** — builds or tears down the whole audio path
  (capture → processing → virtual microphone). STOP verifies that no
  devices remain on the system.
* **REPAIR** — audits and rebuilds the audio connection (see
  OBS_GUIDE.md §4). Use it whenever something on the audio system changed
  underneath the app: daemon restart, USB mic replug, OBS quirks.
* **Level meters** — input (pre-DSP) and output (what OBS hears), plus
  live loudness (LUFS), true peak (dBTP), gate/compressor activity and
  the measured CPU load of the DSP thread.

### Recording
Records exactly the processed stream (the same audio OBS receives) to WAV
or FLAC. Waveform view with selection, trim, delete, split, insert
silence — every operation undoable (24-level snapshot stack). Export via
**File → Export** to WAV (24-bit) or FLAC (level 5); MP3 export is
intentionally not included (licence situation, see LICENSES.md).

### Mixer
Every DSP stage in signal order with an enable switch per stage:

| # | Stage | Controls |
|---|---|---|
| 1 | High-pass | cutoff 20–300 Hz |
| 2 | RNNoise denoise | amount 0–100 % |
| 3 | Noise gate | threshold, range, attack, hold, release |
| 4 | Parametric EQ | warm shelf, mud cut (freq/gain), presence, clarity, air |
| 5 | De-esser | frequency, amount |
| 6 | Compressor | threshold, ratio, knee, attack, release, makeup |
| 7 | Saturation | drive |
| 8 | Loudness | target −24…−12 LUFS, max gain |
| 9 | Limiter | ceiling −12…0 dBTP, release |
| 10 | Output | trim −24…+24 dB |

Slider changes are smoothed at block rate inside the engine — switching
presets or turning knobs while live never produces clicks.

### Presets
Six built-ins: **Podcast Studio** (default), **YouTube Narrator**,
**Gaming Streamer**, **Deep Warm Voice**, **Clean Interview**,
**Custom**. Switching publishes a new parameter snapshot; the DSP smooths
the transition. User presets save as JSON under
`~/.config/VoiceCraftStudio/presets/`.

### Settings
* Audio: device, sample rate (48 kHz default), block size (10 ms default),
  low-CPU mode, realtime thread priority.
* Behaviour: start engine with the app, remember last preset.
* Recording: folder, format, mono/stereo.
* About: version, log folder shortcut.

## AUTO STUDIO VOICE
The “Auto Voice” dialog records a 6-second sample of *your* voice, runs a
local analysis (spectral tilt, loudness distribution, noise-floor
estimate) and derives conservative starting parameters for the chain.
Processing is entirely offline and local — the sample never leaves the
machine. After applying, use the preset A/B compare to judge the result.

## Privacy & resources
* Fully offline: no network code exists in the process.
* No root required; never touches the system default source/sink.
* Steady-state RAM ≈ 40–60 MB; DSP load ≈ 4 % of one core (mono, 48 kHz).
