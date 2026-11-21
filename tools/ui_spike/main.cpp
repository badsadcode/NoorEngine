// noor_ui_spike (ADR-041): the pause menu both ways, over a picture from NoorPlatformer, focus on "Settings".
//   noor_ui_spike <spike folder> <font.ttf> <background.png> <out folder>
// Writes own.png, rml.png and both.png (side by side), and prints each one's cost per frame and triangles.
#include "spike.h"

#include <stb_image.h>  // its implementation is in the engine (asset/texture.cpp)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: noor_ui_spike <spike folder> <font.ttf> <background.png> <out folder>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path dir = argv[1], out = argv[4];
    fs::create_directories(out);
    constexpr int W = 1280, H = 720;
    int bw = 0, bh = 0, bn = 0;
    unsigned char* bg = stbi_load(argv[3], &bw, &bh, &bn, 3);
    const auto background = [&](ui_spike::Image& img) {
        if (!bg || bw != W || bh != H) return;
        for (std::size_t i = 0; i < img.rgb.size(); ++i) img.rgb[i] = ui_spike::Image::to_linear(bg[i] / 255.0f);
    };
    std::ifstream f(dir / "pause.json");
    std::stringstream layout;
    layout << f.rdbuf();

    const auto own = ui_spike::own_pause_menu(layout.str(), 1, W, H);
    const auto rml = ui_spike::rml_pause_menu((dir / "pause.rml").string(), argv[2], 1, W, H);
    std::printf("own:   %.4f ms a frame (layout + triangles, from JSON every frame), %zu triangles\n", own.ms_per_frame, own.triangles);
    std::printf("rmlui: %.4f ms a frame (Update + Render), %zu triangles\n", rml.ms_per_frame, rml.triangles);

    ui_spike::Image a(W, H), b(W, H);
    background(a);
    background(b);
    if (own.draw) own.draw(a);
    if (rml.draw) rml.draw(b);
    const auto pa = a.srgb8(), pb = b.srgb8();
    stbi_write_png((out / "own.png").string().c_str(), W, H, 3, pa.data(), W * 3);
    stbi_write_png((out / "rml.png").string().c_str(), W, H, 3, pb.data(), W * 3);
    std::vector<unsigned char> both(static_cast<std::size_t>(W) * 2 * H * 3);
    for (int y = 0; y < H; ++y) {
        std::copy_n(&pa[static_cast<std::size_t>(y) * W * 3], W * 3, &both[static_cast<std::size_t>(y) * W * 6]);
        std::copy_n(&pb[static_cast<std::size_t>(y) * W * 3], W * 3, &both[static_cast<std::size_t>(y) * W * 6 + W * 3]);
    }
    stbi_write_png((out / "both.png").string().c_str(), W * 2, H, 3, both.data(), W * 6);
    if (bg) stbi_image_free(bg);
    return rml.draw && own.draw ? 0 : 1;
}
