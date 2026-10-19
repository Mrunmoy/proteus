#pragma once

// ── Surface drawing primitives ─────────────────────────────────────────────
// Thin, clipping helpers over hal::Surface. Every primitive clips against the
// surface bounds so a widget can never scribble outside its framebuffer (this
// is what the out-of-bounds tests assert). Pixels are RGB565, matching the
// platform contract (hal::Surface / hal::rgb565).

#include "hal/IDisplay.h"
#include "ui/Font8x8.h"

#include <cstddef>
#include <cstdint>

namespace ui
{
    class Painter
    {
    public:
        explicit Painter(hal::Surface& surface)
            : m_surface(surface)
        {
        }

        // ── Pixel ──────────────────────────────────────────────────────────
        // Out-of-bounds writes are dropped, not wrapped — the single choke
        // point every other primitive funnels through, so clipping lives here
        // once.
        void putPixel(int x, int y, uint16_t color)
        {
            if (x < 0 || y < 0 ||
                x >= static_cast<int>(m_surface.width) ||
                y >= static_cast<int>(m_surface.height))
            {
                return;
            }
            m_surface.pixels[static_cast<size_t>(y) * m_surface.stride +
                             static_cast<size_t>(x)] = color;
        }

        // ── Filled rectangle ─────────────────────────────────────────────────
        void fillRect(int x, int y, int w, int h, uint16_t color)
        {
            if (w <= 0 || h <= 0)
            {
                return;
            }
            const int x0 = clampLow(x, 0);
            const int y0 = clampLow(y, 0);
            const int x1 = clampHigh(x + w, static_cast<int>(m_surface.width));
            const int y1 = clampHigh(y + h, static_cast<int>(m_surface.height));

            for (int yy = y0; yy < y1; ++yy)
            {
                uint16_t* row = m_surface.pixels + static_cast<size_t>(yy) * m_surface.stride;
                for (int xx = x0; xx < x1; ++xx)
                {
                    row[xx] = color;
                }
            }
        }

        // ── Single glyph ─────────────────────────────────────────────────────
        // Renders only the lit bits, so text composes over whatever is already
        // in the surface (no opaque cell). Codepoints outside the font range
        // draw nothing.
        void drawChar(int x, int y, char c, uint16_t color)
        {
            const uint8_t uc = static_cast<uint8_t>(c);
            if (uc < kFontFirstChar || uc > kFontLastChar)
            {
                return;
            }
            const uint8_t* glyph = kFont8x8[uc - kFontFirstChar];
            for (int row = 0; row < kFontHeight; ++row)
            {
                const uint8_t bits = glyph[row];
                for (int col = 0; col < kFontWidth; ++col)
                {
                    // Font bytes are MSB-first: bit 7 is the LEFTMOST column.
                    if ((bits >> (kFontWidth - 1 - col)) & 0x1)
                    {
                        putPixel(x + col, y + row, color);
                    }
                }
            }
        }

        // ── Null-terminated string ───────────────────────────────────────────
        // Advances one glyph cell per character; no wrapping (the caller owns
        // layout). Stops at NUL.
        void drawText(int x, int y, const char* text, uint16_t color)
        {
            if (text == nullptr)
            {
                return;
            }
            int penX = x;
            for (const char* p = text; *p != '\0'; ++p)
            {
                drawChar(penX, y, *p, color);
                penX += kFontWidth;
            }
        }

        [[nodiscard]] hal::Surface& surface()
        {
            return m_surface;
        }

    private:
        [[nodiscard]] static int clampLow(int v, int lo)
        {
            return v < lo ? lo : v;
        }

        [[nodiscard]] static int clampHigh(int v, int hi)
        {
            return v > hi ? hi : v;
        }

        hal::Surface& m_surface;
    };
}
