#pragma once

// ── Panel ──────────────────────────────────────────────────────────────────
// A simple container: an optional filled background plus a fixed-capacity list
// of child widget pointers (the Panel does NOT own them — no allocation). It
// paints its background then each visible child in insertion order, and offers
// input to children front-to-back, stopping at the first that consumes the
// event. Children are referenced by pointer, so callers keep them alive (stack
// or static), matching the toolkit's no-heap rule.

#include "ui/Widget.h"

#include <cstddef>

namespace ui
{
    template <std::size_t MaxChildren = 8>
    class Panel : public Widget
    {
        static_assert(MaxChildren >= 1, "Panel needs room for at least one child");

    public:
        Panel() = default;

        Panel(int x, int y, int w, int h)
            : Widget(x, y, w, h)
        {
        }

        // Returns false when full so the caller knows the child was dropped.
        [[nodiscard]] bool addChild(Widget* child)
        {
            if (child == nullptr || m_count >= MaxChildren)
            {
                return false;
            }
            m_children[m_count] = child;
            ++m_count;
            return true;
        }

        [[nodiscard]] std::size_t childCount() const { return m_count; }

        void setBackground(uint16_t color, bool filled)
        {
            m_bgColor  = color;
            m_bgFilled = filled;
        }

        void paint(Painter& painter) override
        {
            if (!m_visible)
            {
                return;
            }
            if (m_bgFilled)
            {
                painter.fillRect(m_x, m_y, m_w, m_h, m_bgColor);
            }
            for (std::size_t i = 0; i < m_count; ++i)
            {
                if (m_children[i]->isVisible())
                {
                    m_children[i]->paint(painter);
                }
            }
        }

        [[nodiscard]] bool handleInput(const hal::InputEvent& event) override
        {
            for (std::size_t i = 0; i < m_count; ++i)
            {
                if (m_children[i]->isVisible() && m_children[i]->handleInput(event))
                {
                    return true;
                }
            }
            return false;
        }

    private:
        Widget*     m_children[MaxChildren] = {};
        std::size_t m_count    = 0;
        uint16_t    m_bgColor  = 0x0000;
        bool        m_bgFilled = false;
    };
}
