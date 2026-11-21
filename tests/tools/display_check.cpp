// noor_display_check (2D foundations, part 6): checks a screenshot of a game drawn at a base resolution and scaled up.
//
//   noor_display_check <image> <x> <y> <width> <height> <block>
//
// Inside the rectangle every block x block square (from its top-left corner) must be one exact colour: each game pixel
// became whole screen pixels, nothing blurred between them. Outside it, every pixel must be black (the bars). Also at
// least two different colours inside, so an empty picture doesn't pass. Exit code 0 when all hold.
//
//   noor_display_check --same <a> <ax> <ay> <b> <bx> <by> <width> <height>
//
// The two pictures' rectangles (at ax, ay in a and bx, by in b) are identical, pixel for pixel: "expand" shows the same
// world as "keep" with more around it, never squashed.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace {
int same(char** argv) {
    int aw = 0, ah = 0, bw = 0, bh = 0, n = 0;
    unsigned char* a = stbi_load(argv[2], &aw, &ah, &n, 3);
    unsigned char* b = stbi_load(argv[5], &bw, &bh, &n, 3);
    if (!a || !b) {
        std::fprintf(stderr, "can't read %s or %s\n", argv[2], argv[5]);
        return 2;
    }
    const int ax = std::atoi(argv[3]), ay = std::atoi(argv[4]), bx = std::atoi(argv[6]), by = std::atoi(argv[7]);
    const int w = std::atoi(argv[8]), h = std::atoi(argv[9]);
    long differ = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool in = ax + x < aw && ay + y < ah && bx + x < bw && by + y < bh;
            if (!in || std::memcmp(a + ((static_cast<std::size_t>(ay + y)) * aw + ax + x) * 3, b + ((static_cast<std::size_t>(by + y)) * bw + bx + x) * 3, 3) != 0) ++differ;
        }
    stbi_image_free(a);
    stbi_image_free(b);
    std::printf("%dx%d compared: %ld pixels differ\n", w, h, differ);
    return differ == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 10 && std::strcmp(argv[1], "--same") == 0) return same(argv);
    if (argc != 7) {
        std::fprintf(stderr, "usage: noor_display_check <image> <x> <y> <width> <height> <block>\n");
        return 2;
    }
    int w = 0, h = 0, n = 0;
    unsigned char* img = stbi_load(argv[1], &w, &h, &n, 3);
    if (!img) {
        std::fprintf(stderr, "can't read %s\n", argv[1]);
        return 2;
    }
    const int rx = std::atoi(argv[2]), ry = std::atoi(argv[3]), rw = std::atoi(argv[4]), rh = std::atoi(argv[5]), k = std::atoi(argv[6]);
    const auto px = [&](int x, int y) { return img + (static_cast<std::size_t>(y) * w + x) * 3; };
    long bad_blocks = 0, bad_bars = 0;
    std::set<unsigned> colours;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool inside = x >= rx && x < rx + rw && y >= ry && y < ry + rh;
            const unsigned char* p = px(x, y);
            if (!inside) {
                if (p[0] > 2 || p[1] > 2 || p[2] > 2) ++bad_bars;
                continue;
            }
            colours.insert(p[0] << 16 | p[1] << 8 | p[2]);
            const int bx = rx + (x - rx) / k * k, by = ry + (y - ry) / k * k;  // the block's first pixel
            if (std::memcmp(p, px(bx, by), 3) != 0) ++bad_blocks;
        }
    stbi_image_free(img);
    std::printf("%dx%d: %ld pixels differ from their block, %ld bar pixels not black, %zu colours\n", w, h, bad_blocks, bad_bars, colours.size());
    return bad_blocks == 0 && bad_bars == 0 && colours.size() >= 2 ? 0 : 1;
}
