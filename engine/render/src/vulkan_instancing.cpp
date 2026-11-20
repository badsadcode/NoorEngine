// Instanced drawing (Milestone 7, part 6, ADR-032). Before a frame is recorded, plan_frame turns
// the frame's draw items into batches:
//
//   - items outside the camera's view are skipped (bounding sphere against the view frustum);
//   - opaque items that share a mesh range, material and pipeline become one instanced draw;
//   - see-through items stay one draw each, sorted back to front;
//   - gizmos stay one draw each, in their order;
//   - per sun cascade, the casters that can reach it, batched the same way.
//
// Every draw reads its object's matrix (and tint) from a per-frame instance buffer (vertex binding
// 1, one 64-byte record per instance), so a forest of identical trees costs a handful of draw calls
// instead of one per tree part per tree. The batching is invisible to callers: they still hand the
// renderer one DrawItem per object.
#include "vulkan_impl.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace noor::render {
namespace {

// What makes draws batchable: the same pipeline, mesh range, material and splat settings.
struct BatchKey {
    std::uintptr_t pipeline = 0;
    std::uint64_t skin = 0;          // skinned (8C): the mesh's skin data, and
    std::uint32_t palette = 0;       // its palette's first row + 1 (each skinned draw is its own batch)
    std::uint32_t mesh = 0, first = 0, count = 0;
    std::uint32_t ids[4]{};
    std::uint32_t tiling[4]{};  // the splat tiling's bits
    auto operator<=>(const BatchKey&) const = default;
};

struct Candidate {
    BatchKey key;
    GpuInstance instance;
};

GpuInstance instance_of(const DrawItem& item) {
    const math::Mat4& w = item.world_from_object;
    return {{w.m[0].x, w.m[1].x, w.m[2].x, w.m[3].x},
            {w.m[0].y, w.m[1].y, w.m[2].y, w.m[3].y},
            {w.m[0].z, w.m[1].z, w.m[2].z, w.m[3].z},
            item.tint};
}

float max_axis_scale(const math::Mat4& w) {
    return std::max({math::length(math::Vec3{w.m[0].x, w.m[0].y, w.m[0].z}), math::length(math::Vec3{w.m[1].x, w.m[1].y, w.m[1].z}),
                     math::length(math::Vec3{w.m[2].x, w.m[2].y, w.m[2].z})});
}

// The camera's view volume as planes in view space (the camera looks along -Z): four sides and the
// near plane; the far plane is at infinity (reverse-Z, ADR-022). A sphere is outside when it's fully
// on the outer side of any plane.
struct Frustum {
    math::Mat4 view;
    math::Vec3 side[4];  // outward normals through the eye
    float near_plane = 0.1f;
    // A parallel view (9A part 1): a box, half `half_w` wide and `half_h` high, from the near to the far plane.
    bool ortho = false;
    float half_w = 0, half_h = 0, far_plane = 0;

    Frustum(const Camera& camera, float aspect) : view(camera.view), near_plane(camera.near_plane) {
        const float ty = std::tan(camera.fov_y * 0.5f);
        const float tx = ty * aspect;
        side[0] = math::normalize(math::Vec3{1, 0, tx});
        side[1] = math::normalize(math::Vec3{-1, 0, tx});
        side[2] = math::normalize(math::Vec3{0, 1, ty});
        side[3] = math::normalize(math::Vec3{0, -1, ty});
        ortho = camera.orthographic();
        half_h = 0.5f * camera.ortho_height;
        half_w = half_h * aspect;
        far_plane = camera.far_plane;
    }
    [[nodiscard]] bool visible(math::Vec3 world_centre, float radius) const {
        const math::Vec3 c = view.transform_point(world_centre);
        if (c.z - radius > -near_plane) return false;  // wholly behind the near plane
        if (ortho) return c.z + radius >= -far_plane && std::abs(c.x) <= half_w + radius && std::abs(c.y) <= half_h + radius;
        for (const auto& n : side)
            if (math::dot(n, c) > radius) return false;
        return true;
    }
};

// Sorts candidates by key and turns each run of equal keys into one batch.
void group(std::vector<Candidate>& candidates, std::vector<GpuInstance>& instances, std::vector<DrawBatch>& out) {
    std::ranges::stable_sort(candidates, {}, &Candidate::key);
    for (std::size_t i = 0; i < candidates.size();) {
        std::size_t j = i;
        DrawBatch b;
        const BatchKey& k = candidates[i].key;
        b.pipeline = reinterpret_cast<VkPipeline>(k.pipeline);
        b.mesh = k.mesh;
        b.first = k.first;
        b.count = k.count;
        std::memcpy(b.ids, k.ids, sizeof(b.ids));
        b.splat_tiling = {std::bit_cast<float>(k.tiling[0]), std::bit_cast<float>(k.tiling[1]), std::bit_cast<float>(k.tiling[2]),
                          std::bit_cast<float>(k.tiling[3])};
        b.skin_address = k.skin;
        b.palette_joint = k.palette > 0 ? k.palette - 1 : 0;
        b.instance_offset = static_cast<std::uint32_t>(instances.size());
        while (j < candidates.size() && candidates[j].key == k) instances.push_back(candidates[j++].instance);
        b.instance_count = static_cast<std::uint32_t>(j - i);
        out.push_back(b);
        i = j;
    }
}

} // namespace

Status Renderer::Impl::plan_frame(const FrameDesc& frame) {
    FramePlan& p = plan;
    p.instances.clear();
    p.opaque.clear();
    p.transparent.clear();
    p.gizmos.clear();
    for (auto& s : p.shadows) s.clear();
    p.particle_vertices.clear();
    p.particles.clear();
    p.see_through.clear();
    p.weather.clear();
    p.palette_rows.clear();
    stats.instances = 0;
    stats.culled = 0;
    stats.particle_draws = 0;
    stats.particles = 0;

    const float aspect = static_cast<float>(extent.width) / static_cast<float>(std::max(1u, extent.height));
    const Frustum frustum(frame.camera, aspect);
    const math::Vec3 eye = math::inverse(frame.camera.view).transform_point({0, 0, 0});
    const bool shadows_on = frame.lighting.sun_shadows && frame.lighting.sun_illuminance > 0.0f && shadow_size > 0;

    std::vector<Candidate> opaque;
    std::array<std::vector<Candidate>, kShadowCascades> shadow;
    struct Transparent {
        float distance;
        Candidate candidate;
    };
    std::vector<Transparent> transparent;
    opaque.reserve(frame.draws.size());

    for (const DrawItem& item : frame.draws) {
        if (!item.mesh.valid() || item.mesh.id > meshes.size() || !meshes[item.mesh.id - 1].alive) continue;
        const GpuMesh& mesh = meshes[item.mesh.id - 1];
        // A submesh range, clamped to the mesh (draw items may come from imported files).
        const std::uint32_t first = std::min(item.first_index, mesh.index_count);
        const std::uint32_t count = item.index_count == 0 ? mesh.index_count - first : std::min(item.index_count, mesh.index_count - first);
        if (count == 0) continue;
        const std::uint32_t material = material_slot(item);
        // Skinned (8C): the joints place it in the world, so its own matrix is identity; its palette goes into this
        // frame's palette buffer, and its bounds come from where its joints took its bind-pose sphere.
        const bool skinned = mesh.skin_address != 0 && !item.skin_palette.empty();
        std::uint32_t palette = 0;
        math::Vec3 skinned_centre{0, 0, 0};
        float skinned_radius = 0.0f;
        if (skinned) {
            palette = static_cast<std::uint32_t>(p.palette_rows.size() / 3) + 1;
            math::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            float scale = 0.0f;
            for (const math::Mat4& m : item.skin_palette) {
                p.palette_rows.push_back({m.m[0].x, m.m[1].x, m.m[2].x, m.m[3].x});
                p.palette_rows.push_back({m.m[0].y, m.m[1].y, m.m[2].y, m.m[3].y});
                p.palette_rows.push_back({m.m[0].z, m.m[1].z, m.m[2].z, m.m[3].z});
                const math::Vec3 c = m.transform_point(mesh.center);
                lo = {std::min(lo.x, c.x), std::min(lo.y, c.y), std::min(lo.z, c.z)};
                hi = {std::max(hi.x, c.x), std::max(hi.y, c.y), std::max(hi.z, c.z)};
                scale = std::max(scale, max_axis_scale(m));
            }
            skinned_centre = (lo + hi) * 0.5f;
            skinned_radius = math::length(hi - lo) * 0.5f + mesh.radius * scale;
        }
        DrawItem placed = item;
        if (skinned) placed.world_from_object = math::Mat4::identity();
        const GpuInstance inst = instance_of(placed);

        if (item.overlay) {  // gizmos: in order, one each, never culled (they're small and few)
            Batch b;
            b.pipeline = gizmo_pipeline;
            b.mesh = item.mesh.id;
            b.first = first;
            b.count = count;
            b.ids[0] = material;
            b.splat_tiling = item.splat_tiling;
            b.instance_offset = static_cast<std::uint32_t>(p.instances.size());
            b.instance_count = 1;
            p.instances.push_back(inst);
            p.gizmos.push_back(b);
            continue;
        }

        const float radius = skinned ? skinned_radius : mesh.radius * max_axis_scale(item.world_from_object);
        const math::Vec3 centre = skinned ? skinned_centre : item.world_from_object.transform_point(mesh.center);
        const bool blend = materials[material].alpha == AlphaMode::Blend;
        const bool two_sided = materials[material].double_sided;
        if (frustum.visible(centre, radius)) {
            ++stats.instances;
            BatchKey key;
            key.pipeline = reinterpret_cast<std::uintptr_t>(blend ? (two_sided ? blend_double_sided_pipeline : blend_pipeline)
                                                                  : (two_sided ? double_sided_pipeline : mesh_pipeline));
            key.mesh = item.mesh.id;
            key.first = first;
            key.count = count;
            key.skin = skinned ? mesh.skin_address : 0;
            key.palette = palette;
            key.ids[0] = material;
            key.ids[1] = item.splat ? 0x80000000u | material_slot(item.splat_layers[0]) : 0u;
            key.ids[2] = item.splat ? material_slot(item.splat_layers[1]) : 0u;
            key.ids[3] = item.splat ? material_slot(item.splat_layers[2]) : 0u;
            key.tiling[0] = std::bit_cast<std::uint32_t>(item.splat_tiling.x);
            key.tiling[1] = std::bit_cast<std::uint32_t>(item.splat_tiling.y);
            key.tiling[2] = std::bit_cast<std::uint32_t>(item.splat_tiling.z);
            key.tiling[3] = std::bit_cast<std::uint32_t>(item.splat_tiling.w);
            if (blend) {
                transparent.push_back({math::length(centre - eye), {key, inst}});
            } else {
                opaque.push_back({key, inst});
            }
        } else {
            ++stats.culled;
        }

        if (shadows_on && item.cast_shadow) {
            BatchKey key;
            key.pipeline = reinterpret_cast<std::uintptr_t>(shadow_pipeline);
            key.mesh = item.mesh.id;
            key.first = first;
            key.count = count;
            key.skin = skinned ? mesh.skin_address : 0;
            key.palette = palette;
            key.ids[0] = material;
            for (std::uint32_t c = 0; c < kShadowCascades; ++c) {
                if (!sphere_touches_cascade(cascades[c], centre, radius)) {
                    ++stats.shadow_draws_culled;
                    continue;
                }
                key.ids[1] = c;
                shadow[c].push_back({key, inst});
            }
        }
    }

    group(opaque, p.instances, p.opaque);
    // The see-through pass: see-through meshes and particle emitters (Milestone 8B) in one order, back to front by
    // distance, so smoke behind a window is drawn before the window and smoke in front of it after.
    struct SeeThroughItem {
        float distance;
        bool particles;
        std::size_t index;  // into `transparent` or frame.particles
    };
    std::vector<SeeThroughItem> order;
    order.reserve(transparent.size() + frame.particles.size());
    for (std::size_t i = 0; i < transparent.size(); ++i) order.push_back({transparent[i].distance, false, i});
    for (std::size_t i = 0; i < frame.particles.size(); ++i) {
        const ParticleDraw& d = frame.particles[i];
        if (d.particles.empty() || !frustum.visible(d.center, std::max(0.0f, d.radius))) continue;
        order.push_back({math::length(d.center - eye), true, i});
    }
    std::ranges::stable_sort(order, [](const SeeThroughItem& a, const SeeThroughItem& b) { return a.distance > b.distance; });
    const float exposure = frame.lighting.exposure();
    for (const auto& item : order) {
        if (!item.particles) {
            std::vector<Candidate> one{transparent[item.index].candidate};
            group(one, p.instances, p.transparent);
            p.see_through.push_back({false, static_cast<std::uint32_t>(p.transparent.size() - 1)});
            continue;
        }
        const ParticleDraw& d = frame.particles[item.index];
        ParticleBatch b;
        b.first = static_cast<std::uint32_t>(p.particle_vertices.size());
        b.count = static_cast<std::uint32_t>(d.particles.size());
        p.particle_vertices.insert(p.particle_vertices.end(), d.particles.begin(), d.particles.end());
        // particle.hlsl's push constants: texture, flipbook grid, flags; emission (exposed), stretch.
        b.push.ids[0] = d.texture.valid() ? d.texture.id : 0u;
        b.push.ids[1] = std::max(1u, d.frames_x);
        b.push.ids[2] = std::max(1u, d.frames_y);
        b.push.ids[3] = (d.facing == ParticleFacing::Velocity ? 1u : 0u) | (d.lit ? 2u : 0u) | (d.blend == ParticleBlend::Additive ? 4u : 0u) |
                        (d.blend == ParticleBlend::Premultiplied ? 8u : 0u);
        b.push.splat_tiling = {std::max(0.0f, d.emission) * exposure, std::max(0.0f, d.stretch), 0.0f, 0.0f};
        p.particles.push_back(b);
        p.see_through.push_back({true, static_cast<std::uint32_t>(p.particles.size() - 1)});
        ++stats.particle_draws;
        stats.particles += b.count;
    }
    stats.transparent_draws = static_cast<std::uint32_t>(p.transparent.size());
    // Rain, snow and dust (8B part 3): one draw each, as many drops as the intensity asks for (the most: 200,000 rain,
    // 60,000 snow, 20,000 dust; weather.hlsl's boxes are sized for these).
    stats.weather_drops = 0;
    {
        const auto& w = frame.lighting.weather;
        const float most[3] = {200000.0f, 60000.0f, 20000.0f};
        const float amount[3] = {w.rain, w.snow, w.dust};
        for (std::uint32_t k = 0; k < 3; ++k) {
            const float drops = std::clamp(amount[k], 0.0f, 1.0f) * most[k];
            if (drops <= 0.0f) continue;
            FramePlan::WeatherDraw d;
            d.count = static_cast<std::uint32_t>(std::ceil(drops));
            d.push.ids[0] = k;
            d.push.ids[1] = d.count;
            d.push.ids[2] = w.seed;
            d.push.splat_tiling = {frame.lighting.time, drops, w.wind.x, w.wind.z};
            p.weather.push_back(d);
            stats.weather_drops += d.count;
        }
    }
    for (std::uint32_t c = 0; c < kShadowCascades; ++c) group(shadow[c], p.instances, p.shadows[c]);

    // This frame's particles into its slot's particle buffer (grown as needed, like the instances below).
    if (!p.particle_vertices.empty()) {
        GpuBuffer& pbuf = particle_buffers[frame_index];
        std::size_t& pcap = particle_capacity[frame_index];
        if (p.particle_vertices.size() > pcap) {
            if (pbuf.buffer != VK_NULL_HANDLE) destroy_buffer(pbuf);
            pcap = std::max<std::size_t>(p.particle_vertices.size() + p.particle_vertices.size() / 2, 4096);
            auto created = create_buffer(pcap * sizeof(ParticleVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, false);
            if (!created) {
                pcap = 0;
                p = {};
                return std::unexpected(created.error());
            }
            pbuf = *created;
            set_name(VK_OBJECT_TYPE_BUFFER, pbuf.buffer, std::format("Particles (frame slot {}, {} particles)", frame_index, pcap));
        }
        std::memcpy(pbuf.mapped, p.particle_vertices.data(), p.particle_vertices.size() * sizeof(ParticleVertex));
        vmaFlushAllocation(allocator, pbuf.allocation, 0, p.particle_vertices.size() * sizeof(ParticleVertex));
    }

    // Skinned draws' palettes into this slot's palette buffer (grown as needed), read by address.
    if (!p.palette_rows.empty()) {
        GpuBuffer& buf = palette_buffers[frame_index];
        std::size_t& cap = palette_capacity[frame_index];
        if (p.palette_rows.size() > cap) {
            if (buf.buffer != VK_NULL_HANDLE) destroy_buffer(buf);
            cap = std::max<std::size_t>(p.palette_rows.size() + p.palette_rows.size() / 2, 3 * 1024);
            auto created = create_buffer(cap * sizeof(math::Vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true, false);
            if (!created) {
                cap = 0;
                p = {};
                return std::unexpected(created.error());
            }
            buf = *created;
            set_name(VK_OBJECT_TYPE_BUFFER, buf.buffer, std::format("Joint palettes (frame slot {}, {} joints)", frame_index, cap / 3));
            VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            address.buffer = buf.buffer;
            palette_address[frame_index] = vkGetBufferDeviceAddress(device, &address);
        }
        std::memcpy(buf.mapped, p.palette_rows.data(), p.palette_rows.size() * sizeof(math::Vec4));
        vmaFlushAllocation(allocator, buf.allocation, 0, p.palette_rows.size() * sizeof(math::Vec4));
    }

    // Into this frame slot's instance buffer (its previous frame finished: its fence was waited).
    if (p.instances.empty()) return {};
    GpuBuffer& buffer = instance_buffers[frame_index];
    std::size_t& capacity = instance_capacity[frame_index];
    if (p.instances.size() > capacity) {
        if (buffer.buffer != VK_NULL_HANDLE) destroy_buffer(buffer);
        capacity = std::max<std::size_t>(p.instances.size() + p.instances.size() / 2, 1024);
        auto created = create_buffer(capacity * sizeof(GpuInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, false);
        if (!created) {
            capacity = 0;
            p = {};
            return std::unexpected(created.error());
        }
        buffer = *created;
        set_name(VK_OBJECT_TYPE_BUFFER, buffer.buffer, std::format("Instances (frame slot {}, {} records)", frame_index, capacity));
    }
    std::memcpy(buffer.mapped, p.instances.data(), p.instances.size() * sizeof(GpuInstance));
    vmaFlushAllocation(allocator, buffer.allocation, 0, p.instances.size() * sizeof(GpuInstance));
    return {};
}

void Renderer::Impl::record_batches(VkCommandBuffer cmd, std::span<const Batch> batches, bool shadow) {
    if (batches.empty()) return;
    const VkBuffer instances = instance_buffers[frame_index].buffer;
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 1, 1, &instances, &zero);
    VkPipeline bound = VK_NULL_HANDLE;
    std::uint32_t bound_mesh = 0;
    for (const Batch& b : batches) {
        if (b.pipeline != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b.pipeline);
            bound = b.pipeline;
        }
        const GpuMesh& mesh = meshes[b.mesh - 1];
        if (b.mesh != bound_mesh) {
            vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &zero);
            vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            bound_mesh = b.mesh;
        }
        MeshPushConstants push{};
        std::memcpy(push.ids, b.ids, sizeof(push.ids));
        push.splat_tiling = b.splat_tiling;
        if (b.skin_address != 0) {  // skinned (8C): where its skin data and its palette are
            // Its first joint: three rows (48 bytes) a joint.
            const VkDeviceAddress palette = palette_address[frame_index] + static_cast<VkDeviceAddress>(b.palette_joint) * 3 * sizeof(math::Vec4);
            push.skin[0] = static_cast<std::uint32_t>(b.skin_address);
            push.skin[1] = static_cast<std::uint32_t>(b.skin_address >> 32);
            push.skin[2] = static_cast<std::uint32_t>(palette);
            push.skin[3] = static_cast<std::uint32_t>(palette >> 32);
        }
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, b.count, b.instance_count, b.first, 0, b.instance_offset);
        ++stats.draw_calls;
        if (shadow) {
            ++stats.shadow_draw_calls;
        } else {
            stats.triangles += static_cast<std::uint64_t>(b.count / 3) * b.instance_count;
        }
    }
}

void Renderer::Impl::destroy_instance_buffers() {
    for (auto& b : instance_buffers)
        if (b.buffer != VK_NULL_HANDLE) destroy_buffer(b);
    instance_capacity = {};
    for (auto& b : particle_buffers)
        if (b.buffer != VK_NULL_HANDLE) destroy_buffer(b);
    particle_capacity = {};
    for (auto& b : palette_buffers)
        if (b.buffer != VK_NULL_HANDLE) destroy_buffer(b);
    palette_capacity = {};
    palette_address = {};
}

} // namespace noor::render
