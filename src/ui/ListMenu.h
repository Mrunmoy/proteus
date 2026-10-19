#pragma once

// ── ListMenu ───────────────────────────────────────────────────────────────
// A vertical, scrollable list of text items — the ROM-selection widget. Items
// live in a fixed [MaxItems][ItemLen] char matrix (no std::string, no heap);
// the current selection is drawn highlighted and the view scrolls to keep the
// selection visible once the list outgrows the visible rows.
//
// Navigation contract (decided here, documented for callers):
//   Up / Down move the selection by one. Selection CLAMPS at the ends — it does
//   NOT wrap. (Clamp was chosen over wrap because a long ROM list that silently
//   jumps top↔bottom is disorienting on a handheld; the user always knows
//   "down" can't move past the bottom.)
//   A or Start activates the current item.
//
// Signals:
//   selectionChanged(int index) — fires only when the index actually changes.
//   activated(int index)        — fires on A / Start.

#include "ui/Signal.h"
#include "ui/Widget.h"

#include <cstddef>

namespace ui
{
    template <std::size_t MaxItems = 64,
              std::size_t ItemLen  = 32,
              std::size_t SlotCap  = 4>
    class ListMenu : public Widget
    {
        static_assert(MaxItems >= 1, "ListMenu needs room for at least one item");
        static_assert(ItemLen  >= 2, "ListMenu items need room for a char + NUL");

    public:
        // ── Signals (public, Qt-style) ───────────────────────────────────────
        Signal<SlotCap, int> selectionChanged;
        Signal<SlotCap, int> activated;

        ListMenu() = default;

        ListMenu(int x, int y, int w, int h)
            : Widget(x, y, w, h)
        {
        }

        // ── Item management ─────────────────────────────────────────────────
        void clear()
        {
            m_count     = 0;
            m_selected  = 0;
            m_scrollTop = 0;
        }

        // Returns false when the list is full so callers populating from
        // SdCard::listRoms() can stop cleanly at MaxItems.
        [[nodiscard]] bool addItem(const char* text)
        {
            if (m_count >= MaxItems)
            {
                return false;
            }
            copyInto(m_items[m_count], text);
            ++m_count;
            return true;
        }

        [[nodiscard]] std::size_t count() const { return m_count; }
        [[nodiscard]] const char* itemText(std::size_t i) const
        {
            return (i < m_count) ? m_items[i] : "";
        }

        // ── Selection / view ─────────────────────────────────────────────────
        [[nodiscard]] int selectedIndex() const { return m_selected; }
        [[nodiscard]] std::size_t scrollTop() const { return m_scrollTop; }

        void setSelectedIndex(int index)
        {
            if (m_count == 0)
            {
                return;
            }
            const int clamped = clampIndex(index);
            if (clamped != m_selected)
            {
                m_selected = clamped;
                ensureVisible();
                selectionChanged.emit(m_selected);
            }
        }

        // Row colours; defaults give white-on-transparent text with a solid
        // highlight bar for the selection.
        void setColors(uint16_t text, uint16_t highlightText, uint16_t highlightBg)
        {
            m_textColor          = text;
            m_highlightTextColor = highlightText;
            m_highlightBgColor   = highlightBg;
        }

        // ── Input ───────────────────────────────────────────────────────────
        [[nodiscard]] bool handleInput(const hal::InputEvent& event) override
        {
            // Drive navigation from press edges only; held/repeat is the port's
            // concern (it re-emits Down edges via the repeat machinery).
            if (event.type != hal::InputEvent::Type::Down &&
                event.type != hal::InputEvent::Type::Repeat)
            {
                return false;
            }
            switch (event.button)
            {
                case hal::Button::Up:
                    move(-1);
                    return true;
                case hal::Button::Down:
                    move(+1);
                    return true;
                case hal::Button::A:
                case hal::Button::Start:
                    if (m_count > 0)
                    {
                        activated.emit(m_selected);
                    }
                    return true;
                default:
                    return false;
            }
        }

        // ── Paint ───────────────────────────────────────────────────────────
        void paint(Painter& painter) override
        {
            if (!m_visible)
            {
                return;
            }
            const std::size_t rows    = visibleRows();
            const int         rowH     = kRowHeight;
            const int         textPad  = (rowH - kFontHeight) / 2;

            for (std::size_t r = 0; r < rows; ++r)
            {
                const std::size_t idx = m_scrollTop + r;
                if (idx >= m_count)
                {
                    break;
                }
                const int rowY = m_y + static_cast<int>(r) * rowH;
                const bool selected = (static_cast<int>(idx) == m_selected);

                uint16_t fg = m_textColor;
                if (selected)
                {
                    painter.fillRect(m_x, rowY, m_w, rowH, m_highlightBgColor);
                    fg = m_highlightTextColor;
                }
                painter.drawText(m_x + kTextPadX, rowY + textPad, m_items[idx], fg);
            }
        }

        // ── Layout helpers (public so the menu is testable) ───────────────────
        [[nodiscard]] std::size_t visibleRows() const
        {
            if (m_h <= 0)
            {
                return 0;
            }
            const std::size_t fit = static_cast<std::size_t>(m_h) / kRowHeight;
            return fit == 0 ? 1 : fit;  // always show at least one row
        }

    private:
        static constexpr int kRowHeight = 12;  // 8px glyph + 4px breathing room
        static constexpr int kTextPadX  = 2;

        void move(int delta)
        {
            if (m_count == 0)
            {
                return;
            }
            const int target = clampIndex(m_selected + delta);
            if (target != m_selected)
            {
                m_selected = target;
                ensureVisible();
                selectionChanged.emit(m_selected);
            }
        }

        [[nodiscard]] int clampIndex(int index) const
        {
            const int last = static_cast<int>(m_count) - 1;
            if (index < 0)
            {
                return 0;
            }
            if (index > last)
            {
                return last;
            }
            return index;
        }

        // Scroll the window the minimum amount needed to keep m_selected on
        // screen (standard "follow the cursor" behaviour).
        void ensureVisible()
        {
            const std::size_t rows = visibleRows();
            const std::size_t sel  = static_cast<std::size_t>(m_selected);
            if (sel < m_scrollTop)
            {
                m_scrollTop = sel;
            }
            else if (sel >= m_scrollTop + rows)
            {
                m_scrollTop = sel - rows + 1;
            }
        }

        static void copyInto(char (&dst)[ItemLen], const char* src)
        {
            std::size_t i = 0;
            if (src != nullptr)
            {
                for (; i < ItemLen - 1 && src[i] != '\0'; ++i)
                {
                    dst[i] = src[i];
                }
            }
            dst[i] = '\0';
        }

        char        m_items[MaxItems][ItemLen] = {};
        std::size_t m_count               = 0;
        int         m_selected            = 0;
        std::size_t m_scrollTop           = 0;

        uint16_t    m_textColor          = 0xFFFF;  // white
        uint16_t    m_highlightTextColor = 0x0000;  // black on…
        uint16_t    m_highlightBgColor   = 0xFFFF;  // …white bar
    };
}
