#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>

namespace camphone {

// Minimal 8x16 ASCII bitmap font table for essential characters
// (A-Z, 0-9, space, punctuation)
static const uint8_t kFont8x16[][16] = {
    // 0: Space
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    // 1: '!'
    {0,0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0x18,0x18,0,0,0},
    // 2: '-'
    {0,0,0,0,0,0,0,0x7E,0x7E,0,0,0,0,0,0,0},
    // 3: '.'
    {0,0,0,0,0,0,0,0,0,0,0x18,0x3C,0x3C,0x18,0,0},
    // 4: '/'
    {0,0,0x06,0x0C,0x18,0x18,0x30,0x30,0x60,0x60,0xC0,0,0,0,0,0},
    // 5: '0'
    {0,0,0x3C,0x66,0x6E,0x7E,0x76,0x66,0x66,0x66,0x3C,0,0,0,0,0},
    // 6: '1'
    {0,0,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0,0,0,0,0},
    // 7: '2'
    {0,0,0x3C,0x66,0x06,0x06,0x0C,0x18,0x30,0x60,0x7E,0,0,0,0,0},
    // 8: '3'
    {0,0,0x3C,0x66,0x06,0x1C,0x06,0x06,0x06,0x66,0x3C,0,0,0,0,0},
    // 9: '4'
    {0,0,0x0C,0x1C,0x34,0x64,0x7E,0x0C,0x0C,0x0C,0x1E,0,0,0,0,0},
    // 10: '5'
    {0,0,0x7E,0x60,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C,0,0,0,0,0},
    // 11: '6'
    {0,0,0x3C,0x66,0x60,0x7C,0x66,0x66,0x66,0x66,0x3C,0,0,0,0,0},
    // 12: '7'
    {0,0,0x7E,0x06,0x0C,0x0C,0x18,0x18,0x30,0x30,0x30,0,0,0,0,0},
    // 13: '8'
    {0,0,0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x3C,0,0,0,0,0},
    // 14: '9'
    {0,0,0x3C,0x66,0x66,0x66,0x3E,0x06,0x06,0x66,0x3C,0,0,0,0,0},
    // 15: ':'
    {0,0,0,0x18,0x18,0,0,0,0x18,0x18,0,0,0,0,0,0},
    // 16: 'A'
    {0,0,0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0,0,0,0,0},
    // 17: 'B'
    {0,0,0x7C,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0x7C,0,0,0,0,0},
    // 18: 'C'
    {0,0,0x3C,0x66,0x60,0x60,0x60,0x60,0x60,0x66,0x3C,0,0,0,0,0},
    // 19: 'D'
    {0,0,0x78,0x6C,0x66,0x66,0x66,0x66,0x66,0x6C,0x78,0,0,0,0,0},
    // 20: 'E'
    {0,0,0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x7E,0,0,0,0,0},
    // 21: 'F'
    {0,0,0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0,0,0,0,0},
    // 22: 'G'
    {0,0,0x3C,0x66,0x60,0x60,0x6E,0x66,0x66,0x66,0x3E,0,0,0,0,0},
    // 23: 'H'
    {0,0,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x66,0,0,0,0,0},
    // 24: 'I'
    {0,0,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0,0,0,0,0},
    // 25: 'K'
    {0,0,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x66,0x66,0,0,0,0,0},
    // 26: 'L'
    {0,0,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0,0,0,0,0},
    // 27: 'M'
    {0,0,0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x63,0x63,0,0,0,0,0},
    // 28: 'N'
    {0,0,0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x66,0x66,0,0,0,0,0},
    // 29: 'O'
    {0,0,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0,0,0,0,0},
    // 30: 'P'
    {0,0,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x60,0,0,0,0,0},
    // 31: 'R'
    {0,0,0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x66,0x66,0,0,0,0,0},
    // 32: 'S'
    {0,0,0x3C,0x66,0x60,0x3C,0x06,0x06,0x06,0x66,0x3C,0,0,0,0,0},
    // 33: 'T'
    {0,0,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0},
    // 34: 'U'
    {0,0,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0,0,0,0,0},
    // 35: 'V'
    {0,0,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0,0,0,0,0},
    // 36: 'W'
    {0,0,0x63,0x63,0x63,0x6B,0x6B,0x7F,0x77,0x63,0x63,0,0,0,0,0},
    // 37: 'X'
    {0,0,0x66,0x66,0x3C,0x18,0x18,0x3C,0x66,0x66,0x66,0,0,0,0,0},
    // 38: 'Y'
    {0,0,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0},
    // 39: 'Z'
    {0,0,0x7E,0x06,0x0C,0x18,0x30,0x60,0x60,0x60,0x7E,0,0,0,0,0},
    // 40: '('
    {0,0,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0,0,0,0,0},
    // 41: ')'
    {0,0,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0,0,0,0,0},
    // 42: '*' (Bullet / Dot)
    {0,0,0,0,0x18,0x3C,0x7E,0x7E,0x3C,0x18,0,0,0,0,0,0}
};

static const uint8_t* get_font_glyph(char c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c == ' ') return kFont8x16[0];
    if (c == '!') return kFont8x16[1];
    if (c == '-') return kFont8x16[2];
    if (c == '.') return kFont8x16[3];
    if (c == '/') return kFont8x16[4];
    if (c >= '0' && c <= '9') return kFont8x16[5 + (c - '0')];
    if (c == ':') return kFont8x16[15];
    if (c >= 'A' && c <= 'I') return kFont8x16[16 + (c - 'A')];
    if (c >= 'K' && c <= 'Z') return kFont8x16[25 + (c - 'K')];
    if (c == '(') return kFont8x16[40];
    if (c == ')') return kFont8x16[41];
    if (c == '*' || c == 0) return kFont8x16[42];
    return kFont8x16[0];
}

class VCamStandbyRenderer {
public:
    static void render_nv12(uint8_t* dst, uint32_t width, uint32_t height, uint64_t frame_index) {
        uint32_t y_size = width * height;
        uint32_t uv_size = y_size / 2;

        // 1. Dark professional background with subtle vertical vignette gradient
        // Y = 28..38, U = 128 (neutral), V = 128 (neutral)
        for (uint32_t y = 0; y < height; ++y) {
            uint8_t base_y = 28 + static_cast<uint8_t>((y * 12) / height);
            std::memset(dst + y * width, base_y, width);
        }
        // Neutral chroma (gray / monochrome dark theme)
        uint8_t* uv = dst + y_size;
        std::memset(uv, 128, uv_size);

        // Subtle accent bar on top (CameraPhone Cyan: Y=180, U=170, V=80)
        uint32_t bar_h = std::max(4u, height / 200);
        for (uint32_t y = 0; y < bar_h; ++y) {
            std::memset(dst + y * width, 180, width);
        }
        for (uint32_t y = 0; y < bar_h / 2; ++y) {
            uint8_t* row = uv + y * width;
            for (uint32_t x = 0; x < width / 2; ++x) {
                row[x * 2]     = 170; // U
                row[x * 2 + 1] = 80;  // V
            }
        }

        // 2. Render centered text
        std::string title = "CAMERAPHONE 4K PRO";
        std::string sub1  = "VIRTUAL CAMERA - STANDBY";
        std::string sub2  = "CONECTE O CELULAR VIA CABO USB OU WI-FI IP";
        
        // Pulsing activity indicator
        bool pulse = (frame_index / 30) % 2 == 0;
        std::string dot = pulse ? "* AGUARDANDO TRANSMISSAO *" : "  AGUARDANDO TRANSMISSAO  ";

        uint32_t scale_title = std::max(2u, width / 480);
        uint32_t scale_sub   = std::max(1u, scale_title / 2);

        uint32_t center_y = height / 2;
        draw_text(dst, width, height, title, center_y - 60 * scale_sub, scale_title, 240);
        draw_text(dst, width, height, sub1, center_y - 10 * scale_sub, scale_sub, 180);
        draw_text(dst, width, height, sub2, center_y + 30 * scale_sub, scale_sub, 150);
        draw_text(dst, width, height, dot, center_y + 80 * scale_sub, scale_sub, pulse ? 220 : 120);
    }

private:
    static void draw_text(uint8_t* y_plane, uint32_t width, uint32_t height,
                          const std::string& text, uint32_t start_y, uint32_t scale, uint8_t brightness) {
        uint32_t text_w = static_cast<uint32_t>(text.size()) * 8 * scale;
        if (text_w > width) return;
        uint32_t start_x = (width - text_w) / 2;

        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            const uint8_t* glyph = get_font_glyph(c);
            uint32_t char_x = start_x + static_cast<uint32_t>(i) * 8 * scale;

            for (uint32_t row = 0; row < 16; ++row) {
                uint8_t bits = glyph[row];
                for (uint32_t col = 0; col < 8; ++col) {
                    if (bits & (0x80 >> col)) {
                        for (uint32_t dy = 0; dy < scale; ++dy) {
                            uint32_t py = start_y + row * scale + dy;
                            if (py >= height) continue;
                            uint8_t* row_ptr = y_plane + py * width;
                            for (uint32_t dx = 0; dx < scale; ++dx) {
                                uint32_t px = char_x + col * scale + dx;
                                if (px < width) {
                                    row_ptr[px] = brightness;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
};

} // namespace camphone
