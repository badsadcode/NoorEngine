# audio: miniaudio (ADR-011) as a runtime system (ADR-027).
noor_add_module(audio
    SOURCES
        src/audio.cpp
    PUBLIC_DEPS
        noor::runtime
    PRIVATE_DEPS
        miniaudio::miniaudio
)
