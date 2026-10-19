#include "common/Debouncer.h"

namespace common
{
    bool Debouncer::update(bool raw)
    {
        if (raw == m_state)
        {
            m_counter = 0;
        }
        else if (++m_counter >= m_stableCount)
        {
            m_state   = raw;
            m_counter = 0;
        }
        return m_state;
    }
}
