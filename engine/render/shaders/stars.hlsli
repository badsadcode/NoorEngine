// The star field (Milestone 8A, part 5), a port of engine/render/include/noor/render/stars.h (the unit-tested
// reference): keep them in step. Cells on the six cube faces, at most one star each.

#ifndef NOOR_STARS_HLSLI
#define NOOR_STARS_HLSLI

static const uint kStarGrid = 256;
static const float kStarLimit = 6.5;
static const float kBrightestStar = -1.5;

float star_count_brighter(float m) { return 15.0 * pow(10.0, 0.5 * (m - 1.0)); }
float star_illuminance(float m) { return 2.08e-6 * pow(10.0, -0.4 * m); }

uint star_hash(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bbu;
    x ^= x >> 11;
    x *= 0xac4c1b51u;
    x ^= x >> 15;
    x *= 0x31848babu;
    x ^= x >> 14;
    return x;
}
float star_unit(uint h) { return float(h >> 8) * (1.0 / 16777216.0); }

float3 star_face_direction(uint face, float u, float v)
{
    float3 d;
    switch (face)
    {
    case 0: d = float3(1, -v, -u); break;
    case 1: d = float3(-1, -v, u); break;
    case 2: d = float3(u, 1, v); break;
    case 3: d = float3(u, -1, -v); break;
    case 4: d = float3(u, -v, 1); break;
    default: d = float3(-u, -v, -1); break;
    }
    return normalize(d);
}

// The face and the face coordinates (-1..1) a direction falls on (the inverse of star_face_direction).
void star_face_of(float3 d, out uint face, out float u, out float v)
{
    const float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x > 0.0 ? 0u : 1u;
        u = (d.x > 0.0 ? -d.z : d.z) / a.x;
        v = -d.y / a.x;
    }
    else if (a.y >= a.z)
    {
        face = d.y > 0.0 ? 2u : 3u;
        u = d.x / a.y;
        v = (d.y > 0.0 ? d.z : -d.z) / a.y;
    }
    else
    {
        face = d.z > 0.0 ? 4u : 5u;
        u = (d.z > 0.0 ? d.x : -d.x) / a.z;
        v = -d.y / a.z;
    }
}

// The star in a cell: false if there's none.
bool star_in_cell(uint face, uint ix, uint iy, uint seed, out float3 direction, out float magnitude, out float temperature)
{
    direction = 0.0;
    magnitude = 99.0;
    temperature = 6000.0;
    const uint cell = (face * kStarGrid + iy) * kStarGrid + ix;
    const uint h0 = star_hash(cell * 4u + seed * 0x9e3779b9u);
    const float total = star_count_brighter(kStarLimit);
    const float p = total / float(6u * kStarGrid * kStarGrid);
    const float r0 = star_unit(h0);
    if (r0 >= p) return false;
    const float rank = (r0 / p) * total;
    magnitude = max(kBrightestStar, 1.0 + 2.0 * log10(max(rank, 1e-3) / 15.0));
    const uint h1 = star_hash(h0 ^ 0x68e31da4u), h2 = star_hash(h1 ^ 0xb5297a4du);
    const float u = ((float(ix) + 0.15 + 0.7 * star_unit(h1)) / kStarGrid) * 2.0 - 1.0;
    const float v = ((float(iy) + 0.15 + 0.7 * star_unit(h2)) / kStarGrid) * 2.0 - 1.0;
    temperature = 3000.0 + 9000.0 * pow(star_unit(star_hash(h2 ^ 0x1b873593u)), 1.5);
    direction = star_face_direction(face, u, v);
    return true;
}

// The scene's axes into the star field's frame (stars.h star_frame).
float3 star_frame(float3 world, float latitude_deg, float sidereal_hours)
{
    const float colat = radians(90.0 - latitude_deg);
    const float c = cos(colat), s = sin(colat);
    const float3 t = float3(world.x, world.y * c - world.z * s, world.y * s + world.z * c);
    const float a = radians(15.0 * sidereal_hours), ca = cos(a), sa = sin(a);
    return float3(t.x * ca + t.z * sa, t.y, -t.x * sa + t.z * ca);
}

// A star's colour from its temperature, luminance about 1 (red dwarfs to blue-white).
float3 star_color(float temperature)
{
    const float t = saturate((temperature - 3000.0) / 9000.0);
    const float3 c = lerp(float3(1.0, 0.62, 0.38), float3(0.72, 0.84, 1.18), t);
    return c / dot(c, float3(0.2126, 0.7152, 0.0722));
}

// The light of the stars around direction `world` for a pixel of `pixel_angle` radians: each star a small
// Gaussian (sigma 0.7 pixel) holding its illuminance, so the sum over its pixels is right.
float3 starlight(float3 world, float latitude_deg, float sidereal_hours, uint seed, float pixel_angle)
{
    const float3 d = star_frame(world, latitude_deg, sidereal_hours);
    uint face;
    float u, v;
    star_face_of(d, face, u, v);
    const int cx = int((u * 0.5 + 0.5) * kStarGrid), cy = int((v * 0.5 + 0.5) * kStarGrid);
    const float sigma = max(pixel_angle * 0.7, 1e-5);
    float3 sum = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
        [unroll] for (int dx = -1; dx <= 1; ++dx)
        {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= int(kStarGrid) || y >= int(kStarGrid)) continue;
            float3 sd;
            float m, temp;
            if (!star_in_cell(face, uint(x), uint(y), seed, sd, m, temp)) continue;
            const float angle = acos(clamp(dot(d, sd), -1.0, 1.0));
            const float w = exp(-0.5 * (angle * angle) / (sigma * sigma)) / (2.0 * 3.14159265 * sigma * sigma);
            sum += star_color(temp) * star_illuminance(m) * w;
        }
    return sum;
}

#endif
