#pragma once
// FBX import (Milestone 8C, part 7, ADR-036) through ufbx (MIT): into the same ImportedModel as glTF, so everything
// that takes a model (placing it, skinning, clips, retargeting, the Animation panel) takes an FBX too.
//
// The file is converted on the way in to the engine's convention (right-handed, +Y up, metres), in its geometry,
// transforms, skins and animation alike. Polygons are triangulated; each material becomes a submesh. Skins: each
// cluster's bone is a joint and its geometry-to-bone matrix the inverse bind; the strongest four weights per vertex,
// renormalised (said, as glTF import says it). The rest pose is where the skin was bound: a skinned bone's cluster bind
// matrix, else the file's bind pose, else the node's own transform. Animation: each take (animation stack) is one clip,
// baked by ufbx into linear keys at 60 a second (FBX curves, tangents and Euler orders evaluated as FBX does), over the
// take's own span from 0; a node the take doesn't animate stays at rest; Blender's "Armature|Walk" is called "Walk".
// Textures embedded in the file are used; external ones where the path the file gives (or the same name next to the
// FBX) exists. Imported files are untrusted: the size is limited and ufbx checks every index. Research and the files
// tried: docs/animation/FBX_RESEARCH.md.
//
// Not imported: cameras and lights, blend shapes, constraints, and properties' animation other than node transforms.

#include "noor/asset/gltf_import.h"

#include <filesystem>

namespace noor::asset {

[[nodiscard]] Result<ImportedModel> import_fbx(const std::filesystem::path& path, std::uint64_t max_file_bytes = 512ull << 20);

} // namespace noor::asset
