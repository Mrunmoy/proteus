#pragma once

// ── Widget base ────────────────────────────────────────────────────────────
// Common geometry + visibility + the two virtuals every widget overrides:
// paint() and handleInput(). No children are stored here — containers (Panel)
// own a fixed-capacity array, so the base stays allocation-free and trivial.
// Geometry is signed int so off-screen scrolling math never underflows a
// uint; the Painter clips negatives away.

#include "hal/IInput.h"
#include "ui/Painter.h"

namespace ui
{
    class Widget
    {
    public:
        Widget() = default;

        Widget(int x, int y, int w, int h)
            : m_x(x), m_y(y), m_w(w), m_h(h)
        {
        }

        virtual ~Widget() = default;

        Widget(const Widget&)            = delete;
        Widget& operator=(const Widget&) = delete;

        // ── Geometry ─────────────────────────────────────────────────────────
        void setGeometry(int x, int y, int w, int h)
        {
            m_x = x;
            m_y = y;
            m_w = w;
            m_h = h;
        }

        [[nodiscard]] int x() const { return m_x; }
        [[nodiscard]] int y() const { return m_y; }
        [[nodiscard]] int width() const { return m_w; }
        [[nodiscard]] int height() const { return m_h; }

        // ── Visibility ─────────────────────────────────────────────────────
        void setVisible(bool visible) { m_visible = visible; }
        [[nodiscard]] bool isVisible() const { return m_visible; }

        // ── Behaviour (overridden by concrete widgets) ───────────────────────
        virtual void paint(Painter& painter) = 0;

        // Returns true when the widget consumed the event so a parent can stop
        // propagating it. The base ignores everything.
        [[nodiscard]] virtual bool handleInput(const hal::InputEvent& event)
        {
            (void)event;
            return false;
        }

    protected:
        int  m_x       = 0;
        int  m_y       = 0;
        int  m_w       = 0;
        int  m_h       = 0;
        bool m_visible = true;
    };
}
