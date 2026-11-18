#include "noor/terrain_edit/brush.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace noor::terrain_edit {

std::optional<Tool> tool_from_name(std::string_view name) {
    if (name == "raise") return Tool::Raise;
    if (name == "lower") return Tool::Lower;
    if (name == "smooth") return Tool::Smooth;
    if (name == "flatten") return Tool::Flatten;
    return std::nullopt;
}

std::string_view tool_name(Tool tool) {
    switch (tool) {
    case Tool::Raise: return "raise";
    case Tool::Lower: return "lower";
    case Tool::Smooth: return "smooth";
    case Tool::Flatten: return "flatten";
    }
    return "raise";
}

float brush_weight(const Brush& brush, float distance) {
    if (brush.radius <= 0.0f || distance >= brush.radius) return 0.0f;
    const float inner = brush.radius * (1.0f - std::clamp(brush.falloff, 0.0f, 1.0f));
    if (distance <= inner) return 1.0f;
    const float t = (distance - inner) / (brush.radius - inner);  // 0 at the inner edge .. 1 at the rim
    return 1.0f - t * t * (3.0f - 2.0f * t);                     // smoothstep down
}

std::optional<terrain::SampleRect> apply(const terrain::Heights& current, terrain::SculptLayer& layer, const Brush& brush,
                                         math::Vec2 centre, float seconds) {
    if (seconds <= 0.0f || brush.radius <= 0.0f) return std::nullopt;
    const float cell = current.cell_size();
    const math::Vec2 c = current.to_samples(centre.x, centre.y);
    const float r = brush.radius / cell;  // in samples
    const float max_x = static_cast<float>(current.samples_x() - 1), max_z = static_cast<float>(current.samples_z() - 1);
    if (c.x + r < 0.0f || c.y + r < 0.0f || c.x - r > max_x || c.y - r > max_z) return std::nullopt;
    terrain::SampleRect rect{static_cast<std::uint32_t>(std::clamp(std::ceil(c.x - r), 0.0f, max_x)),
                             static_cast<std::uint32_t>(std::clamp(std::ceil(c.y - r), 0.0f, max_z)),
                             static_cast<std::uint32_t>(std::clamp(std::floor(c.x + r), 0.0f, max_x)),
                             static_cast<std::uint32_t>(std::clamp(std::floor(c.y + r), 0.0f, max_z))};

    // New offsets are worked out from the heights before this dab (all of them), then written.
    std::vector<float> next(rect.count());
    const std::uint32_t w = rect.x1 - rect.x0 + 1;
    bool any = false;
    for (std::uint32_t z = rect.z0; z <= rect.z1; ++z)
        for (std::uint32_t x = rect.x0; x <= rect.x1; ++x) {
            const float dx = (static_cast<float>(x) - c.x) * cell, dz = (static_cast<float>(z) - c.y) * cell;
            const float weight = brush_weight(brush, std::sqrt(dx * dx + dz * dz));
            float offset = layer.offset(x, z);
            if (weight > 0.0f) {
                const float h = current.at(x, z);
                const float step = brush.strength * seconds * weight;
                switch (brush.tool) {
                case Tool::Raise: offset += step; break;
                case Tool::Lower: offset -= step; break;
                case Tool::Flatten: offset += std::clamp(brush.flatten_height - h, -step, step); break;
                case Tool::Smooth: {
                    float sum = 0.0f;
                    int n = 0;
                    for (int oz = -1; oz <= 1; ++oz)
                        for (int ox = -1; ox <= 1; ++ox) {
                            const auto sx = static_cast<std::int64_t>(x) + ox, sz = static_cast<std::int64_t>(z) + oz;
                            if (sx < 0 || sz < 0 || sx > static_cast<std::int64_t>(max_x) || sz > static_cast<std::int64_t>(max_z)) continue;
                            sum += current.at(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sz));
                            ++n;
                        }
                    offset += (sum / static_cast<float>(n) - h) * std::min(1.0f, step);
                    break;
                }
                }
                any = true;
            }
            next[static_cast<std::size_t>(z - rect.z0) * w + (x - rect.x0)] = offset;
        }
    if (!any) return std::nullopt;
    for (std::uint32_t z = rect.z0; z <= rect.z1; ++z)
        for (std::uint32_t x = rect.x0; x <= rect.x1; ++x) layer.set(x, z, next[static_cast<std::size_t>(z - rect.z0) * w + (x - rect.x0)]);
    return rect;
}

} // namespace noor::terrain_edit
