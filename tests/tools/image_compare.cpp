// noor_image_compare: the render tests' judge (Milestone 5, ADR-012).
//
//   noor_image_compare <actual image> <reference.png> [--update] [--tolerance N] [--max-bad P]
//                      [--diff out.png] [--size WxH]
//
// The actual image (a screenshot: BMP or PNG) is shrunk to the reference's size by averaging
// blocks of pixels, which irons out edge aliasing and small driver differences. A pixel is "bad"
// when any channel differs by more than --tolerance (0..255, default 12); the test fails when
// more than --max-bad percent of pixels are bad (default 1.0). Also prints the mean difference.
// --update writes the shrunk image as the new reference (size --size, default 320x180) instead.
// --diff writes a picture of the differences (bad pixels red, the rest dimmed) for looking at.
// Exit code: 0 pass, 1 fail, 2 usage or file error.

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> rgb;  // 3 bytes per pixel
};

bool load(const std::string& path, Image& out) {
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &out.width, &out.height, &channels, 3);
    if (!data) {
        std::fprintf(stderr, "can't read %s: %s\n", path.c_str(), stbi_failure_reason());
        return false;
    }
    out.rgb.assign(data, data + static_cast<std::size_t>(out.width) * out.height * 3);
    stbi_image_free(data);
    return true;
}

// Box-filters `in` down to w x h (each output pixel averages the input pixels it covers).
Image shrink(const Image& in, int w, int h) {
    Image out{w, h, std::vector<unsigned char>(static_cast<std::size_t>(w) * h * 3)};
    for (int y = 0; y < h; ++y) {
        const int y0 = y * in.height / h, y1 = std::max(y0 + 1, (y + 1) * in.height / h);
        for (int x = 0; x < w; ++x) {
            const int x0 = x * in.width / w, x1 = std::max(x0 + 1, (x + 1) * in.width / w);
            double sum[3] = {0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                    for (int c = 0; c < 3; ++c) sum[c] += in.rgb[(static_cast<std::size_t>(sy) * in.width + sx) * 3 + c];
            const double n = static_cast<double>((y1 - y0) * (x1 - x0));
            for (int c = 0; c < 3; ++c)
                out.rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c] = static_cast<unsigned char>(std::lround(sum[c] / n));
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: noor_image_compare <actual> <reference.png> [--update] [--tolerance N] [--max-bad P] [--diff out.png] [--size WxH]\n");
        return 2;
    }
    const std::string actual_path = argv[1], reference_path = argv[2];
    bool update = false;
    int tolerance = 12;
    double max_bad = 1.0;
    std::string diff_path;
    int ref_w = 320, ref_h = 180;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--update") update = true;
        else if (a == "--tolerance" && more) tolerance = std::atoi(argv[++i]);
        else if (a == "--max-bad" && more) max_bad = std::atof(argv[++i]);
        else if (a == "--diff" && more) diff_path = argv[++i];
        else if (a == "--size" && more) std::sscanf(argv[++i], "%dx%d", &ref_w, &ref_h);
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }

    Image actual;
    if (!load(actual_path, actual)) return 2;
    if (update) {
        const Image small = shrink(actual, ref_w, ref_h);
        if (!stbi_write_png(reference_path.c_str(), small.width, small.height, 3, small.rgb.data(), small.width * 3)) {
            std::fprintf(stderr, "can't write %s\n", reference_path.c_str());
            return 2;
        }
        std::printf("reference written: %s (%dx%d from %dx%d)\n", reference_path.c_str(), ref_w, ref_h, actual.width, actual.height);
        return 0;
    }

    Image reference;
    if (!load(reference_path, reference)) {
        std::fprintf(stderr, "(make one with --update after checking the picture by eye)\n");
        return 2;
    }
    // Same shape required: comparing a 16:9 picture with a 4:3 one says nothing useful.
    if (std::abs(static_cast<double>(actual.width) / actual.height - static_cast<double>(reference.width) / reference.height) > 0.01) {
        std::fprintf(stderr, "aspect ratios differ: actual %dx%d, reference %dx%d\n", actual.width, actual.height, reference.width, reference.height);
        return 1;
    }
    const Image small = shrink(actual, reference.width, reference.height);
    std::size_t bad = 0;
    double total = 0;
    Image diff{reference.width, reference.height, std::vector<unsigned char>(small.rgb.size())};
    const std::size_t pixels = static_cast<std::size_t>(reference.width) * reference.height;
    for (std::size_t p = 0; p < pixels; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(small.rgb[p * 3 + c]) - static_cast<int>(reference.rgb[p * 3 + c]));
            worst = std::max(worst, d);
            total += d;
        }
        const bool is_bad = worst > tolerance;
        bad += is_bad ? 1 : 0;
        for (int c = 0; c < 3; ++c) diff.rgb[p * 3 + c] = is_bad ? (c == 0 ? 255 : 0) : static_cast<unsigned char>(reference.rgb[p * 3 + c] / 4);
    }
    const double bad_percent = 100.0 * static_cast<double>(bad) / static_cast<double>(pixels);
    const double mean = total / static_cast<double>(pixels * 3);
    if (!diff_path.empty()) stbi_write_png(diff_path.c_str(), diff.width, diff.height, 3, diff.rgb.data(), diff.width * 3);
    const bool pass = bad_percent <= max_bad;
    std::printf("%s: %.2f%% of pixels differ by more than %d (limit %.2f%%), mean difference %.2f\n", pass ? "PASS" : "FAIL",
                bad_percent, tolerance, max_bad, mean);
    return pass ? 0 : 1;
}
