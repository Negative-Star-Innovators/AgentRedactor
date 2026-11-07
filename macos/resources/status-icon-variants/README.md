# Menu bar icon variants (StatusIcon)

Committed references so we can return to any direction. Template-rendering
icons (alpha = glyph); preview by compositing onto light/dark backgrounds.
Source master: `icon_resources/fox_logo_high_quality_enhanced.png` (2160px).

| File | Description | Status |
|---|---|---|
| `G-outline-handdrawn.png` (+@2x) | Clean outline fox; hand-drawn mask band, two eye dots under the band, ribbon tails right. **This is the shipped icon as of 2026-10-11.** | current |
| `A-outline-clean@2x.png` | Outline fox + auto-detected pen stroke only, no face detail. | fallback |
| `D-outline-autodetail@2x.png` | Outline + auto edge-detected face details (busy). | rejected |

Recreation parameters for G (1080px processing space, fractions of the
cropped fox bbox): outer contour = alpha − MinFilter(31); band polygon
(0.055,0.245)(0.695,0.245)(0.715,0.275)(0.70,0.335)(0.075,0.355)(0.05,0.30);
eye dots at (0.19,0.365) & (0.465,0.365), r≈0.030H; ribbon strokes
(0.70,0.27)→(0.875,0.185) and (0.705,0.30)→(0.85,0.44), w≈0.018H, rounded
tips; GaussianBlur(0.6); square canvas 92% fill, centered.

Note: A/D reference PNGs were lost to /tmp cleanup before this archive was
made; both are recreatable — A = outer contour (MinFilter 31) plus
blur(4)→FIND_EDGES(threshold 46) internal strokes; D = A's internals plus
color-quantized (7-color MEDIANCUT) region boundaries restricted to the head
region (y < 0.52H), MaxFilter(3).
| `H-filled-hollowband.png` (+@2x) | Solid filled fox; the ninja band CUT OUT (hollow), two solid eye dots floating in the band gap; outline-only ribbon. **Shipped from 2026-10-11.** | current |
| `I-filled-hollowband-noeyes.png` (+@2x) | H without the eye dots — solid fox, hollow band only, outline ribbon. **Shipped from 2026-10-11 (user preference).** | current |
| `J-filled-thickband-eyes.png` (+@2x) | H with a thicker hollow band (y 0.225–0.375) and the eye dots back in the gap. **Shipped from 2026-10-11 (user preference).** | current |
| `K-filled-thickestband-eyes.png` (+@2x) | J with the band thickened further (y 0.20–0.40). **Shipped from 2026-10-11 (user preference).** | current |
| `L-filled-band-eyes-invpen.png` (+@2x) | K + the pen fully INVERTED: a diagonal stripe from the left-cap protrusion (0.03,0.70) to the right-tip protrusion (0.86,0.52), width ~0.055H, cut out of the solid fox. **Shipped from 2026-10-11 (user preference).** | current |
| `M-filled-band-eyes-penedges.png` (+@2x) | K + the pen as TWO thin inverted edge cuts (w 0.013H, centers ±0.028H perpendicular of the (0.03,0.70)→(0.86,0.52) axis); solid pen body between. **Shipped from 2026-10-11 (user preference).** | current |
| `N-filled-band-eyes-boldpen.png` (+@2x) | K + single bold inverted pen stripe (width 0.065H) — the L approach with more presence; the two-edge M read as mush at 1x. **Shipped from 2026-10-11 (user preference).** | current |
| `O-filled-band-eyes-2boldpen.png` (+@2x) | N's pen as TWO bold cuts (w 0.035H, centers ±0.042H perpendicular of the pen axis) with solid body between — bold take on the two-edge pen. **Shipped from 2026-10-11 (user preference).** | current |
| `P-fullwidth-lower-band.png` (+@2x) | O + mask band lowered (y 0.23–0.44), slightly wider, cut edge-to-edge through the whole fox; eyes follow to the gap center (0.335). **Shipped from 2026-10-11 (user preference).** | current |
| `Q-lower-thick-band.png` (+@2x) | P + band lowered further (y 0.27–0.50), eyes at 0.385. **Shipped from 2026-10-11 (user preference).** | current |
| `R-maskO-pen-lower-thick-wide.png` (+@2x) | O's mask/eyes (RESTORED after the P/Q experiment) + pen shifted down (axis (0.03,0.74)→(0.86,0.56)), cuts thicker (0.045H) and further apart (±0.060H → 0.075H solid body). **Shipped from 2026-10-11 (user preference).** | current |
