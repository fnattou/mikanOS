#include <cstdint>
#include <cstddef>
#include "frame_buffer_config.hpp"

struct PixelColor {
    uint8_t r, g, b;
};

/// @brief 1つの点を描画する
/// @param config 
/// @param x 
/// @param y 
/// @param c 色情報
/// @return 成功で０、失敗でー１
int WritePixel(const FrameBufferConfig& config,
    const int x, const int y, const PixelColor& c) {
    const int pixel_position = config.pixels_per_scan_line * y + x;
    uint8_t* const p = &config.frame_buffer[4 * pixel_position];
    if (config.pixel_format == kPixelRGBResv8BitPerColor) {
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
    }
    else if (config.pixel_format == kPixelBGRResv8BitPerColor) {
        p[0] = c.b;
        p[1] = c.g;
        p[2] = c.r;
    }
    else {
        return -1;
    }
    return 0;
}


extern "C" void KernelMain(const FrameBufferConfig& frame_buffer_config) {
    for (int x = 0; x < frame_buffer_config.horizontal_resolution; ++x) {
        for (int y = 0; y < frame_buffer_config.vertical_resolution; ++y) {
            WritePixel(frame_buffer_config, x, y, {255, 255, 255});
        }
    }
    for (int x = 0; x < 200; ++x) {
        for (int y = 0; y < 100; ++y) {
            WritePixel(frame_buffer_config, 100 + x, 100 + y, {0, 255, 0});
        }
    }

    while(1) __asm__("hlt");
}