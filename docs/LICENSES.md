# Licences

| Component | Licence | Notes |
|---|---|---|
| VoiceCraft Studio source | MIT | (c) 2026 VoiceCraft Studio contributors |
| Qt 5 (Core/Gui/Widgets) | GPL-3.0 / LGPL-3.0 exceptions | dynamically linked; user installable (`qtbase5-dev`) |
| libpulse (PulseAudio client) | LGPL-2.1+ | dynamically linked |
| libsamplerate | BSD-2-Clause | dynamically linked |
| libsndfile (WAV/FLAC I/O) | LGPL-2.1+ / FLAC BSD | dynamically linked |
| RNNoise | BSD-3-Clause (Xiph) | dynamically linked; built from source with the official pretrained model `rnnoise_data-0b50c45` (Xiph media server) |
| MP3 export | **not included** | patents/licence encumbrance risk; FLAC covers lossless compressed needs |

The .deb package (Phase 5) will declare runtime dependencies on the system
libraries above and ship none of them, per Ubuntu policy. RNNoise is the
exception: because distro builds may lack the pretrained model (see
TROUBLESHOOTING.md #1), the package either depends on a distro build
*verified to contain weights* or ships a private copy under a non-standard
path with RPATH, clearly marked in the copyright file.
