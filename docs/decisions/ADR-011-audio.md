# ADR-011: Audio stack

**Status:** Accepted · **Date:** 2026-11-15 · **Milestone:** 4

## Context
Spec 20.1: playback and streaming, buses and mixing, 3D spatial sound with a listener and
attenuation, volume/pitch/fades, voice limits, device switching. Milestone 4 needs "plays a sound"
and spatial footsteps/gunshots for the FPS sample; the rest grows later.

## Options
- **A. miniaudio** (public domain or MIT-0, single file). Device I/O on every desktop OS (WASAPI,
  CoreAudio, ALSA/PulseAudio/PipeWire), WAV/FLAC/MP3 decoding, a node graph engine with groups
  (buses), spatialization, resampling, streaming.
- **B. SoLoud** (zlib). Pleasant API, less active.
- **C. OpenAL Soft** (LGPL). Good 3D audio, but LGPL complicates static linking and distribution.
- **D. FMOD / Wwise.** Industry standard, proprietary licences: excluded by ADR-019's open-source goal.

## Decision
**A, miniaudio** (pinned), as the `audio` runtime system:
- Assets: `.wav`, `.flac`, `.mp3` (and `.ogg` via stb_vorbis, public domain, when needed) become
  `audio` assets in the registry.
- Components: `audio_source` (clip, volume, pitch, loop, spatial, min/max distance, bus, play on
  start), `audio_listener` (normally on the camera).
- Buses: master, music, sfx, ui, voice (miniaudio sound groups), with volume and mute.
- Script API: play/stop/pause, one-shot play at a position, volume/pitch, bus volumes.
- Occlusion, reverb zones and HRTF (e.g. Steam Audio, Apache-2.0) are later, optional.

## Consequences
- Tests use miniaudio's null backend so CI (no sound card) can run audio logic headlessly.

## Reviewed by
Sage.
