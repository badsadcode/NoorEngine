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

## Implementation notes (Milestone 4, part 5)
- Implemented: assets (.wav/.flac/.mp3), `audio_source`, `audio_listener`, the five buses (master is
  the engine volume), inverse-distance attenuation, one-shots, bus volume and the script API.
  Not yet: pause, fades, mute flag, voice limits, device switching, `.ogg`.
- "Null backend" became **no device at all**: `ma_engine` with `noDevice`, mixed by reading frames
  (`AudioSystem::mix`). Tests measure the actual samples (near louder than far, muted bus silent).
  Without a device, play mode advances the mix by the frame time so sounds still end.
- Doppler is off (no velocities yet). miniaudio objects are owned by deleters that `uninit` before
  freeing; forgetting that crashed teardown once.

## Reviewed by
Sage.
