#pragma once

// ── Label ──────────────────────────────────────────────────────────────────
// A single line of static text rendered with the 8x8 font. The string lives in
// a fixed inline buffer (no std::string) and is copied on setText, so callers
// may pass a transient source. Non-interactive: handleInput stays the base
// no-op.

#include "ui/Widget.h"

#include <cstddef>

namespace ui
{
    template <std::size_t Capacity = 32>
    class Label : public Widget
    {
        static_assert(Capacity >= 1, "Label needs room for at least a NUL");

    public:
        Label() = default;

        Label(int x, int y, const char* text, uint16_t color)
            : Widget(x, y, 0, kFontHeight), m_color(color)
        {
            setText(text);
        }

        void setText(const char* text)
        {
            std::size_t i = 0;
            if (text != nullptr)
            {
                for (; i < Capacity - 1 && text[i] != '\0'; ++i)
                {
                    m_text[i] = text[i];
                }
            }
            m_text[i] = '\0';
            m_len     = i;
            // Keep geometry in sync so containers can lay the label out.
            m_w = static_cast<int>(m_len) * kFontWidth;
            m_h = kFontHeight;
        }

        [[nodiscard]] const char* text() const { return m_text; }
        [[nodiscard]] std::size_t length() const { return m_len; }

        void setColor(uint16_t color) { m_color = color; }
        [[nodiscard]] uint16_t color() const { return m_color; }

        void paint(Painter& painter) override
        {
            if (!m_visible)
            {
                return;
            }
            painter.drawText(m_x, m_y, m_text, m_color);
        }

    private:
        char        m_text[Capacity] = {};
        std::size_t m_len            = 0;
        uint16_t    m_color          = 0xFFFF;  // white by default
    };
}
