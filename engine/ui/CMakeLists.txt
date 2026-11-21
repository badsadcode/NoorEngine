# ui: game UI documents (gameplay foundations part 6, ADR-041). RmlUi and FreeType (static) behind noor::ui; draws through
# the renderer's UI pass (render::UiFrame). Games that never open a document pay for neither.
noor_add_module(ui
    SOURCES
        src/ui.cpp
    PUBLIC_DEPS
        noor::foundation
        noor::render
    PRIVATE_DEPS
        RmlUi::RmlUi
        noor::builtin_font
        noor::asset  # stb_image's implementation (asset/texture.cpp)
        stb::image
)
