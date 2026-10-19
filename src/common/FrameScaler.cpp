#include "common/FrameScaler.h"

#include <cstddef>

namespace common
{
    void blitScaledCentered(const uint16_t* src,
                            uint32_t        srcW,
                            uint32_t        srcH,
                            uint16_t*       dst,
                            uint32_t        dstW,
                            uint32_t        dstH,
                            uint32_t        dstStride,
                            uint16_t        border)
    {
        // Clear the whole visible area to the border colour first; the source
        // region is overwritten below. Cheap relative to the DVI scan-out, and
        // keeps the letterbox/pillarbox crisp without per-pixel branching.
        for (uint32_t y = 0; y < dstH; ++y)
        {
            uint16_t* row = dst + static_cast<size_t>(y) * dstStride;
            for (uint32_t x = 0; x < dstW; ++x)
            {
                row[x] = border;
            }
        }

        const uint32_t scale = computeScale(srcW, srcH, dstW, dstH);
        const uint32_t outW  = srcW * scale;
        const uint32_t outH  = srcH * scale;

        // Centre; the offset is 0 if the scaled frame is wider/taller than the
        // target (then the right/bottom edge is clipped by the bounds checks).
        const uint32_t offX = outW < dstW ? (dstW - outW) / 2 : 0;
        const uint32_t offY = outH < dstH ? (dstH - outH) / 2 : 0;

        for (uint32_t sy = 0; sy < srcH; ++sy)
        {
            const uint32_t dyBase = offY + sy * scale;
            if (dyBase >= dstH)
            {
                break;
            }
            const uint16_t* srcRow = src + static_cast<size_t>(sy) * srcW;

            for (uint32_t sx = 0; sx < srcW; ++sx)
            {
                const uint32_t dxBase = offX + sx * scale;
                if (dxBase >= dstW)
                {
                    break;
                }
                const uint16_t px = srcRow[sx];

                // Replicate the source pixel across the scale×scale block,
                // clipping at the destination edges.
                const uint32_t blkH = (dyBase + scale <= dstH) ? scale : (dstH - dyBase);
                const uint32_t blkW = (dxBase + scale <= dstW) ? scale : (dstW - dxBase);
                for (uint32_t ry = 0; ry < blkH; ++ry)
                {
                    uint16_t* outRow =
                        dst + static_cast<size_t>(dyBase + ry) * dstStride + dxBase;
                    for (uint32_t rx = 0; rx < blkW; ++rx)
                    {
                        outRow[rx] = px;
                    }
                }
            }
        }
    }
}
