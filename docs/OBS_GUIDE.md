# OBS Studio Integration Guide

VoiceCraft Studio creates a real PulseAudio source that OBS lists as an
ordinary microphone. This guide covers first-time setup, verification, and
the “Repair Audio Connection” flow.

## 1. How the device is built

On START, the application loads two PulseAudio modules through the native
API (no `pactl`, no shell):

| Module | Name | Purpose |
|---|---|---|
| `module-null-sink` | `VoiceCraft_Output` | receives the processed audio |
| `module-virtual-source` | `VoiceCraft_Studio_Microphone` | uplinks the sink's monitor as a *microphone-type* source |

The virtual source carries `device.description = "VoiceCraft Studio
Microphone"` and `device.icon_name = audio-input-microphone`, so OBS shows
it exactly like a hardware mic. It is **not** flagged as a monitor —
client-facing metadata is indistinguishable from a real input device.

On STOP both modules are unloaded and the removal is *verified* — the audit
step checks the server's module list and source list for leftovers. The
automated test `test_pulse_e2e` runs five start/stop cycles and asserts zero
residue.

## 2. First-time setup

1. Launch **VoiceCraft Studio**.
2. **Studio** tab → *Input device*: pick your real microphone
   (mono/stereo is detected automatically, capped at 2 channels).
3. Press **START**. The status line shows
   `Running — N ch @ 48000 Hz`.
4. In **OBS Studio**: Settings → Audio → *Mic/Auxiliary Input Audio* →
   select **VoiceCraft Studio Microphone**.
5. Speak — OBS's mic meter must move. Everything OBS receives has already
   passed the full processing chain.

> If OBS was open while you pressed START, it picks the new device up within
> a second (PulseAudio hotplug event). If not, re-open the audio settings
> dropdown once.

## 3. Verifying what OBS hears

* OBS → Edit → Advanced Audio Properties → the VoiceCraft device reports
  48 kHz, the channel count chosen in Studio.
* The **Mixer** tab inside VoiceCraft Studio mirrors every processing stage;
  what you see there is what OBS records.
* For an objective check, record 30 s in OBS and inspect the file: silence
  should be silent (gate + RNNoise), speech level should sit near
  −16 LUFS (podcast target).

## 4. Repair Audio Connection

The **REPAIR** button on the Studio tab performs, in order:

1. PulseAudio connection check (reconnect if the daemon restarted).
2. Module audit — duplicates removed, missing modules rebuilt.
3. Capture stream health — rebinds if the source vanished or moved
   (e.g. USB mic replug changed its name).
4. Playback stream health — rebuilds the stream into our sink.
5. DSP thread restart — if the realtime thread died, the engine is
   restarted once.

Repair is idempotent: if nothing is wrong it reports
`audio connection verified, no repair needed`.

Typical scenarios handled automatically:

| Scenario | What happens |
|---|---|
| OBS started before Studio | device appears on next hotplug poll |
| USB microphone unplugged | `sourceLost` → status turns red, capture rebinds automatically when the device returns; REPAIR forces an immediate rebind |
| PulseAudio daemon restart | modules gone; REPAIR (or START) recreates everything |
| leftovers from a crash | audit unloads stray modules with our signature before rebuilding |

## 5. Recommended OBS settings

* Sample rate 48 kHz (matches the engine; avoids hidden resampling).
* Channels: Stereo (or Mono on the low-CPU profile).
* Monitor **off** unless you wear headphones — the engine does not add
  feedback protection for speaker monitoring.

## 6. Latency notes

Measured deterministic latency of the processing chain is 25.06 ms
(20 ms of it is RNNoise's internal frame pipeline, 5 ms the limiter
lookahead). With noise suppression switched off the chain adds only
5.06 ms. This is *processing* latency OBS hears; it does not affect
lip-sync of recordings (OBS timestamps at capture), only live in-ear
monitoring through the chain, which this application intentionally does
not provide by default.
