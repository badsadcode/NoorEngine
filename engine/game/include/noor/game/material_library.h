#pragma once
// Materials for drawing (ADR-030): turns material files (.noormat, with instances), the materials
// inside GLB models, and primitives' own surface settings into GPU materials, once each. Reloads
// changed .noormat and texture files (refresh). Shared by noor_player and the editor viewport.
#include "noor/asset/gltf_import.h"
#include "noor/asset/material.h"
#include "noor/game/texture_library.h"
#include "noor/render/renderer.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::game {

class MaterialLibrary {
public:
    // `resolve`: asset ids (textures, parent materials) to files.
    MaterialLibrary(render::Renderer& renderer, TextureLibrary& textures, asset::AssetFileResolver resolve);
    ~MaterialLibrary();
    MaterialLibrary(const MaterialLibrary&) = delete;
    MaterialLibrary& operator=(const MaterialLibrary&) = delete;

    void set_resolver(asset::AssetFileResolver resolve) { resolve_ = std::move(resolve); }

    // A .noormat file. Errors are logged once and show a magenta checker material.
    [[nodiscard]] render::MaterialHandle file(const std::filesystem::path& noormat);

    // A material described in place (a primitive's own settings), shared by equal descriptions.
    [[nodiscard]] render::MaterialHandle described(const asset::Material& material);

    // A model's materials, in its order (ImportedModel::materials), with its embedded textures.
    [[nodiscard]] std::vector<render::MaterialHandle> model(const std::filesystem::path& glb, const asset::ImportedModel& model);

    // Reloads changed .noormat files (all file materials, as instances follow their parents) and
    // changed texture files. Returns true if anything was reloaded.
    bool refresh();

private:
    struct FileEntry {
        render::MaterialHandle handle;
        std::filesystem::file_time_type time{};
    };
    render::MaterialDesc desc(const asset::Material& m, const asset::ImportedModel* model);
    render::MaterialDesc load_file(const std::filesystem::path& noormat);

    render::Renderer& renderer_;
    TextureLibrary& textures_;
    asset::AssetFileResolver resolve_;
    std::unordered_map<std::string, FileEntry> files_;
    std::unordered_map<std::string, render::MaterialHandle> described_;
    std::unordered_map<std::string, std::vector<render::MaterialHandle>> models_;
};

} // namespace noor::game
