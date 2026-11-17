#pragma once
// Materials (ADR-030): how a surface looks. A `.noormat` file is either a full material or an
// instance of another one:
//
//   { "noor_material": 1, "base_color": [1, 1, 1, 1], "base_color_map": "<texture id>",
//     "metallic": 0, "roughness": 0.5, "metallic_roughness_map": "<id>"   // glTF packing: G rough, B metal
//     "roughness_map": "<id>", "metallic_map": "<id>",                    // single-channel maps (red)
//     "normal_map": "<id>", "normal_scale": 1, "occlusion_map": "<id>", "occlusion_strength": 1,
//     "emissive": [0, 0, 0], "emissive_map": "<id>", "emissive_strength": 1,
//     "alpha": "opaque" | "cutout" | "blend", "alpha_cutoff": 0.5, "double_sided": false,
//     "uv_scale": [1, 1], "uv_offset": [0, 0] }
//
//   { "noor_material": 1, "parent": "<material id>", "overrides": { "base_color": [0.5, 0.5, 0.5, 1] } }
//
// Channels follow glTF 2.0's metallic-roughness model; colours are linear. Missing fields take the
// defaults above. Texture ids are asset GUIDs (texture assets, ADR-029); ORM maps (R occlusion,
// G roughness, B metallic) go in both occlusion_map and metallic_roughness_map.
#include "noor/asset/json.h"
#include "noor/foundation/error.h"
#include "noor/foundation/guid.h"
#include "noor/foundation/math.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace noor::asset {

inline constexpr int kMaterialFormat = 1;
inline constexpr std::string_view kMaterialExtension = ".noormat";

// Where a texture comes from: a texture file, or an image inside a GLB file (`embedded_image`).
struct TextureRef {
    std::filesystem::path file;
    int embedded_image = -1;  // with `file` = the GLB
    [[nodiscard]] bool empty() const noexcept { return file.empty(); }
    friend bool operator==(const TextureRef&, const TextureRef&) = default;
};

enum class AlphaMode : std::uint8_t { Opaque, Cutout, Blend };

struct Material {
    math::Vec4 base_color{1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    math::Vec3 emissive{0, 0, 0};
    float emissive_strength = 1.0f;
    AlphaMode alpha = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    math::Vec2 uv_scale{1, 1};
    math::Vec2 uv_offset{0, 0};
    TextureRef base_color_map, metallic_roughness_map, roughness_map, metallic_map, normal_map, occlusion_map, emissive_map;
    friend bool operator==(const Material&, const Material&) = default;
};

[[nodiscard]] std::string_view alpha_name(AlphaMode mode) noexcept;

// Finds an asset's file from its GUID (textures, parent materials); nullopt if unknown.
using AssetFileResolver = std::function<std::optional<std::filesystem::path>(const Guid&)>;

// Loads a .noormat, following `parent` chains (instances) up to 16 deep. Unknown texture ids are
// reported in `warnings` and left empty; a cycle or missing parent is an error.
[[nodiscard]] Result<Material> load_material(const std::filesystem::path& file, const AssetFileResolver& resolve,
                                             std::vector<std::string>* warnings = nullptr);

// Applies material JSON fields (a full material or an instance's overrides) onto `m`.
void apply_material_fields(const Json& fields, Material& m, const AssetFileResolver& resolve, std::vector<std::string>* warnings);

// ---- Texture sets -> materials ----

// The role a texture file plays in a texture set, from its name (or none).
enum class TextureRole : std::uint8_t { None, BaseColor, Normal, Roughness, Metallic, Occlusion, Orm, MetallicRoughness, Emissive, Height };
[[nodiscard]] TextureRole texture_role(const std::filesystem::path& file);
[[nodiscard]] std::string_view role_name(TextureRole role) noexcept;

// The set's name and its maps by role, found next to `any_map`: files in the same folder sharing
// its name before the role word ("bricks_albedo.png", "bricks_normal.png" -> "bricks"). Previews
// and thumbnails are skipped. (NoorMaterial and the PBR library name their maps this way.)
struct TextureSet {
    std::string name;
    std::vector<std::pair<TextureRole, std::filesystem::path>> maps;
    [[nodiscard]] std::optional<std::filesystem::path> find(TextureRole role) const;
};
[[nodiscard]] TextureSet find_texture_set(const std::filesystem::path& any_map);

// A material's JSON for a texture set; `id_of` gives each map's texture asset id.
[[nodiscard]] Json material_json_for(const TextureSet& set, const std::function<std::optional<Guid>(const std::filesystem::path&)>& id_of);

} // namespace noor::asset
