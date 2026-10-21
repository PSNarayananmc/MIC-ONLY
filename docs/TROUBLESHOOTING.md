# Troubleshooting Guide

Real-world problems, ranked by how often they bite. Every entry was
actually encountered while building and testing this application.

---

## 1. RNNoise does nothing (noise floor unchanged)

**Symptom** — the noise-suppression amount slider has no audible effect;
automated test reports “noise-only floor drops 1.4 dB” instead of ≈16 dB.

**Cause** — several distribution packages of `librnnoise0` (Debian 13
`0.2+ds-1`, and any build of the GitHub v0.2 tarball without the model)
ship **without the pretrained neural network weights**. `rnnoise_create(nullptr)`
then constructs an untrained network that passes audio through almost
unchanged. This is a packaging problem, not a code problem, and it is
silent: the API works, the model is empty.

**Fix** — build RNNoise with the official model:

```
git clone --depth 1 -b v0.2 https://github.com/xiph/rnnoise.git
cd rnnoise
wget https://media.xiph.org/rnnoise/models/rnnoise_data-0b50c45.tar.gz
tar xzf rnnoise_data-0b50c45.tar.gz        # provides src/rnnoise_data.c/.h
gcc -O2 -fPIC -DNDEBUG -Iinclude -Isrc -c \
    src/denoise.c src/rnn.c src/pitch.c src/kiss_fft.c src/celt_lpc.c \
    src/nnet.c src/nnet_default.c src/parse_lpcnet_weights.c \
    src/rnnoise_data.c src/rnnoise_tables.c
gcc -shared -o librnnoise.so.0 -Wl,-soname,librnnoise.so.0 *.o -lm
```

Note that `parse_lpcnet_weights.c` **must** be included — it defines the
`linear_init`/`gru_init` helpers the model loader calls (a frequent
“undefined reference to rnn_linear_init” comes from excluding it).

**Verify**: feed white noise through `rnnoise_process_frame` at int16 scale
(see next item) — expect ≈30 dB suppression on noise-only input.

## 2. RNNoise suppresses nothing although the model is fine

**Cause** — RNNoise expects samples at **raw int16 scale (±32768)**, not
normalized floats. Its own demo feeds `short` values. Feeding [−1, 1]
floats makes everything look like digital silence to the network and it
passes audio through.

**Fix** — scale in and out around `rnnoise_process_frame`
(VoiceCraft's `NoiseSuppression` wrapper does this internally).

## 3. “VoiceCraft Studio Microphone” does not appear in OBS

Checklist, in order:

1. Engine running? (Studio tab shows `Running — N ch @ 48000 Hz`.)
2. `pactl list short sources | grep VoiceCraft` — must list
   `VoiceCraft_Studio_Microphone`. If not:
   `pactl list short modules | grep VoiceCraft` — if empty, the modules are
   not loaded; press REPAIR.
3. OBS older than 24? It still lists PulseAudio sources, but restart OBS to
   refresh the device list.

## 4. Module creation fails with “Module initialization failed”

**Cause** — PulseAudio module arguments with **spaces in property values
must be quoted**. The canonical working form is:

```
sink_properties="device.description='VoiceCraft Studio Microphone'"
```

Without the outer double quotes the parser treats `Studio` and
`Microphone` as stray arguments and `module-null-sink` refuses to load.
(The same applies when scripting `pactl load-module` by hand.)

## 5. SCHED_FIFO not granted (warning in the log)

**Symptom** — `WARN SCHED_FIFO not granted (rtprio limits); running with
normal scheduling`.

**Impact** — none for typical use; the DSP load is a few percent of a core.
For extra safety under load spikes:

```
sudo setcap cap_net_admin+ep /usr/bin/... # not needed; instead:
```

add yourself to the `rtprio` limits (`/etc/security/limits.d/`):

```
@audio   -  rtprio  95
@audio   -  memlock unlimited
```

VoiceCraft never runs as root and never requires it.

## 6. Latency is higher than expected

Measured deterministic DSP latency:

| Configuration | Latency |
|---|---|
| Full chain (NS on) | 25.06 ms |
| Noise suppression off | 5.06 ms |

RNNoise 0.2 inherently delays two 480-sample frames (20 ms @ 48 kHz) —
verified by impulse test (`out` exactly at `+960`, unity peak). This is a
property of the pretrained model, not of our pipeline. OBS recordings are
unaffected (timestamps are taken at capture); only live monitoring through
the chain would “feel” it.

## 7. Restarting the app leaves devices behind

It should not — STOP verifies removal and the five-cycle test proves it —
but after a hard crash leftovers are possible:

```
pactl list short modules | grep VoiceCraft     # find indices
pactl unload-module <idx>                       # unload each
```

or simply press REPAIR on the next launch — the audit unloads strays
automatically.

## 8. No audio, meter flat, but engine “Running”

* Wrong input device chosen → Studio tab → pick the correct microphone.
* Device muted at the ALSA level → `alsamixer`, un-capture-mute.
* PulseAudio suspended the source → `pactl list sources` shows
  `Suspended: yes`; opening any recording client (VoiceCraft does) usually
  resumes it.

## 9. GUI does not start: Qt platform errors

Headless/SSH boxes need a platform plugin:

```
QT_QPA_PLATFORM=offscreen voicecraft-studio   # no display
```

On a normal desktop session this is never needed.

## 10. Build errors

* `rnnoise.h: No such file` → install `librnnoise-dev` (or point
  `pkg-config` to your self-built prefix).
* `-lpulse` not found during linking although `pkg-config` finds it —
  CMake strips `-L` from imported pkg-config targets when the path is not
  a standard libdir; export `LIBRARY_PATH=/your/prefix/lib` or install the
  library into a standard location.
* Qt5 with `-reduce-relocations` requires position-independent code: build
  with `-fPIC`/`-fPIE` (our CMake sets PIC on all targets).
