# ui: game UI documents (gameplay foundations part 6, ADR-041). RmlUi and FreeType (static) behind noor::ui; draws through
# the renderer's UI pass (render::UiFrame). WorldUi connects documents to a running World and its scripts (part 6b).
# Games that never open a document pay for neither.
noor_add_module(ui
    SOURCES
        src/ui.cpp
        src/world_ui.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::asset
        noor::render
        noor::runtime
    PRIVATE_DEPS
        RmlUi::RmlUi
        noor::builtin_font
        stb::image
)
