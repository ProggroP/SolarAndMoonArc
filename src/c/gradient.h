#pragma once
#include <pebble.h>

/* ===========================================================================
 * gradient.h - geditherte Farbverlaeufe fuer Pebble-Farbplattformen
 * ---------------------------------------------------------------------------
 * Die Farbdisplays (basalt / chalk / emery / gabbro) bieten nur 2 Bit pro
 * Kanal -> 4 Stufen je Kanal (0, 85, 170, 255) -> 64 Farben insgesamt.
 * Ein naiver Verlauf zeigt deshalb harte Streifen. Diese Bibliothek zeichnet
 * Verlaeufe direkt in den Framebuffer und nutzt 4x4-Ordered-Dithering (Bayer),
 * damit Zwischentoene durch raeumliche Mischung der zwei naechstgelegenen
 * Palettenstufen simuliert werden. Genau dieser Trick erzeugt das Punktraster.
 *
 * Endpunktfarben werden in voller 8-Bit-Aufloesung (GradRGB) gefuehrt, damit
 * eine animierte Ueberblendung (Interpolation der Endpunkte ueber die Zeit)
 * absolut weich bleibt - es werden NIE Frames gespeichert, jeder Frame wird
 * live gerechnet.
 *
 * Alle Zeichenfunktionen greifen via graphics_capture_frame_buffer zu und
 * gehoeren daher in einen Layer-update_proc.
 * ========================================================================= */

/* ---- Vollaufgeloeste Farbe (8 Bit je Kanal) ---------------------------- */
typedef struct { uint8_t r, g, b; } GradRGB;

#define GRAD_RGB(r, g, b)  ((GradRGB){ (uint8_t)(r), (uint8_t)(g), (uint8_t)(b) })

/* Pebble-Kanalraster: 4 Stufen bei 0, 85, 170, 255 */
#define GRAD_LEVEL_STEP   85
#define GRAD_MAX_LEVEL     3

static inline GradRGB grad_from_gcolor(GColor c) {
  return GRAD_RGB(c.r * GRAD_LEVEL_STEP, c.g * GRAD_LEVEL_STEP, c.b * GRAD_LEVEL_STEP);
}

/* Lineare Interpolation zweier Farben. t in 0..256 (256 == voll b). */
static inline GradRGB grad_lerp(GradRGB a, GradRGB b, uint16_t t) {
  if (t > 256) t = 256;
  return GRAD_RGB(
    a.r + (((int)b.r - (int)a.r) * (int)t >> 8),
    a.g + (((int)b.g - (int)a.g) * (int)t >> 8),
    a.b + (((int)b.b - (int)a.b) * (int)t >> 8));
}

#if defined(PBL_COLOR)

/* 4x4 Bayer-Schwellwertmatrix, Werte 0..15 */
static const uint8_t GRAD_BAYER4[4][4] = {
  {  0,  8,  2, 10 },
  { 12,  4, 14,  6 },
  {  3, 11,  1,  9 },
  { 15,  7, 13,  5 }
};

/* Quantisiert einen 0..255-Kanal auf eine 2-Bit-Stufe (0..3) mit Bayer-
   Schwelle (0..15). Aufrunden in die naechste Stufe, sobald der Restanteil
   innerhalb des 85er-Bandes die Dither-Schwelle uebersteigt:
   rem/85 > (bayer + 0.5)/16  <=>  rem*16 > 85*bayer + 42 */
static inline uint8_t grad_dither_ch(uint8_t v, uint8_t bayer) {
  uint8_t lo  = v / GRAD_LEVEL_STEP;            /* 0..3 */
  uint8_t rem = v - lo * GRAD_LEVEL_STEP;       /* 0..84 */
  if (lo < GRAD_MAX_LEVEL && (uint16_t)rem * 16 > (uint16_t)bayer * 85 + 42) {
    lo++;
  }
  return lo;
}

/* Baut das opake argb-Byte aus einer 24-Bit-Farbe + Bayer-Schwelle. */
static inline uint8_t grad_argb(GradRGB c, uint8_t bayer) {
  uint8_t r = grad_dither_ch(c.r, bayer);
  uint8_t g = grad_dither_ch(c.g, bayer);
  uint8_t b = grad_dither_ch(c.b, bayer);
  return (uint8_t)(0xC0 | (r << 4) | (g << 2) | b);   /* alpha=3, opak */
}

/* Kleines Integer-sqrt fuer Radialdistanzen. */
static inline int32_t grad_isqrt(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

/* --- Vertikal: top -> bottom -------------------------------------------- */
static void gradient_fill_vertical_rgb(GContext *ctx, GRect rect,
                                       GradRGB top, GradRGB bottom) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect bnd = gbitmap_get_bounds(fb);
  int16_t y0 = rect.origin.y;
  int16_t y1 = rect.origin.y + rect.size.h - 1;
  int16_t x_lo = rect.origin.x;
  int16_t x_hi = rect.origin.x + rect.size.w - 1;
  int span = rect.size.h - 1;
  if (span < 1) span = 1;

  for (int16_t y = y0; y <= y1; y++) {
    if (y < bnd.origin.y || y >= bnd.origin.y + bnd.size.h) continue;
    uint16_t t = (uint16_t)((int32_t)(y - y0) * 256 / span);
    GradRGB row = grad_lerp(top, bottom, t);
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    int16_t xa = x_lo < info.min_x ? info.min_x : x_lo;
    int16_t xb = x_hi > info.max_x ? info.max_x : x_hi;
    const uint8_t *brow = GRAD_BAYER4[y & 3];
    for (int16_t x = xa; x <= xb; x++) {
      info.data[x] = grad_argb(row, brow[x & 3]);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

/* --- Horizontal: left -> right ------------------------------------------ */
static void gradient_fill_horizontal_rgb(GContext *ctx, GRect rect,
                                         GradRGB left, GradRGB right) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect bnd = gbitmap_get_bounds(fb);
  int16_t y0 = rect.origin.y;
  int16_t y1 = rect.origin.y + rect.size.h - 1;
  int16_t x_lo = rect.origin.x;
  int16_t x_hi = rect.origin.x + rect.size.w - 1;
  int span = rect.size.w - 1;
  if (span < 1) span = 1;

  for (int16_t y = y0; y <= y1; y++) {
    if (y < bnd.origin.y || y >= bnd.origin.y + bnd.size.h) continue;
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    int16_t xa = x_lo < info.min_x ? info.min_x : x_lo;
    int16_t xb = x_hi > info.max_x ? info.max_x : x_hi;
    const uint8_t *brow = GRAD_BAYER4[y & 3];
    for (int16_t x = xa; x <= xb; x++) {
      uint16_t t = (uint16_t)((int32_t)(x - x_lo) * 256 / span);
      info.data[x] = grad_argb(grad_lerp(left, right, t), brow[x & 3]);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

/* --- Linear unter beliebigem Winkel: c0 -> c1 --------------------------- *
 * angle: Pebble-Konvention (0 = 12 Uhr, im Uhrzeigersinn). Die Farbe
 * wandert von c0 nach c1 in Richtung des Winkels.                          */
static void gradient_fill_linear_rgb(GContext *ctx, GRect rect,
                                     GradRGB c0, GradRGB c1, int32_t angle) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect bnd = gbitmap_get_bounds(fb);
  int32_t ux = sin_lookup(angle);          /* -TRIG_MAX_RATIO..TRIG_MAX_RATIO */
  int32_t uy = -cos_lookup(angle);
  int16_t cx = rect.origin.x + rect.size.w / 2;
  int16_t cy = rect.origin.y + rect.size.h / 2;

  int16_t rx0 = rect.origin.x - cx;
  int16_t rx1 = rect.origin.x + rect.size.w - 1 - cx;
  int16_t ry0 = rect.origin.y - cy;
  int16_t ry1 = rect.origin.y + rect.size.h - 1 - cy;
  int32_t p0 = rx0 * ux + ry0 * uy;
  int32_t p1 = rx1 * ux + ry0 * uy;
  int32_t p2 = rx0 * ux + ry1 * uy;
  int32_t p3 = rx1 * ux + ry1 * uy;
  int32_t pmin = p0, pmax = p0;
  if (p1 < pmin) pmin = p1;
  if (p1 > pmax) pmax = p1;
  if (p2 < pmin) pmin = p2;
  if (p2 > pmax) pmax = p2;
  if (p3 < pmin) pmin = p3;
  if (p3 > pmax) pmax = p3;
  int32_t prange = pmax - pmin;
  if (prange < 1) prange = 1;

  int16_t y0 = rect.origin.y;
  int16_t y1 = rect.origin.y + rect.size.h - 1;
  int16_t x_lo = rect.origin.x;
  int16_t x_hi = rect.origin.x + rect.size.w - 1;

  for (int16_t y = y0; y <= y1; y++) {
    if (y < bnd.origin.y || y >= bnd.origin.y + bnd.size.h) continue;
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    int16_t xa = x_lo < info.min_x ? info.min_x : x_lo;
    int16_t xb = x_hi > info.max_x ? info.max_x : x_hi;
    const uint8_t *brow = GRAD_BAYER4[y & 3];
    int32_t yterm = (int32_t)(y - cy) * uy;
    for (int16_t x = xa; x <= xb; x++) {
      int32_t pp = (int32_t)(x - cx) * ux + yterm;
      uint16_t t = (uint16_t)(((int64_t)(pp - pmin) * 256) / prange);
      if (t > 256) t = 256;
      info.data[x] = grad_argb(grad_lerp(c0, c1, t), brow[x & 3]);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

/* --- Radial: c_inner (Zentrum) -> c_outer (Rand) ------------------------ *
 * Ideal fuer Sonnen-/Mond-Glow. clip begrenzt den bemalten Bereich.        */
static void gradient_fill_radial_rgb(GContext *ctx, GPoint center,
                                     int16_t r_inner, int16_t r_outer,
                                     GradRGB c_inner, GradRGB c_outer,
                                     GRect clip) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect bnd = gbitmap_get_bounds(fb);
  int span = r_outer - r_inner;
  if (span < 1) span = 1;

  int16_t y0 = clip.origin.y;
  int16_t y1 = clip.origin.y + clip.size.h - 1;
  int16_t x_lo = clip.origin.x;
  int16_t x_hi = clip.origin.x + clip.size.w - 1;

  for (int16_t y = y0; y <= y1; y++) {
    if (y < bnd.origin.y || y >= bnd.origin.y + bnd.size.h) continue;
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    int16_t xa = x_lo < info.min_x ? info.min_x : x_lo;
    int16_t xb = x_hi > info.max_x ? info.max_x : x_hi;
    const uint8_t *brow = GRAD_BAYER4[y & 3];
    int32_t dy = y - center.y;
    int32_t dy2 = dy * dy;
    for (int16_t x = xa; x <= xb; x++) {
      int32_t dx = x - center.x;
      int32_t d = grad_isqrt(dx * dx + dy2);
      uint16_t t;
      if (d <= r_inner) t = 0;
      else if (d >= r_outer) t = 256;
      else t = (uint16_t)((int32_t)(d - r_inner) * 256 / span);
      info.data[x] = grad_argb(grad_lerp(c_inner, c_outer, t), brow[x & 3]);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

#else  /* ---- Schwarz/Weiss-Plattformen: einfache Fallbacks (kein Dither) - */

static void gradient_fill_vertical_rgb(GContext *ctx, GRect rect,
                                       GradRGB top, GradRGB bottom) {
  (void)bottom;
  graphics_context_set_fill_color(ctx,
    (top.r + top.g + top.b) / 3 > 127 ? GColorWhite : GColorBlack);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
}
static void gradient_fill_horizontal_rgb(GContext *ctx, GRect rect,
                                         GradRGB left, GradRGB right) {
  (void)right;
  graphics_context_set_fill_color(ctx,
    (left.r + left.g + left.b) / 3 > 127 ? GColorWhite : GColorBlack);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
}
static void gradient_fill_linear_rgb(GContext *ctx, GRect rect,
                                     GradRGB c0, GradRGB c1, int32_t angle) {
  (void)c1; (void)angle;
  graphics_context_set_fill_color(ctx,
    (c0.r + c0.g + c0.b) / 3 > 127 ? GColorWhite : GColorBlack);
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
}
static void gradient_fill_radial_rgb(GContext *ctx, GPoint center,
                                     int16_t r_inner, int16_t r_outer,
                                     GradRGB c_inner, GradRGB c_outer,
                                     GRect clip) {
  (void)center; (void)r_inner; (void)c_outer; (void)clip;
  graphics_context_set_fill_color(ctx,
    (c_inner.r + c_inner.g + c_inner.b) / 3 > 127 ? GColorWhite : GColorBlack);
  graphics_fill_circle(ctx, center, r_outer);
}

#endif /* PBL_COLOR */

/* ---- Bequemlichkeits-Wrapper mit GColor-Endpunkten -------------------- */
static inline void gradient_fill_vertical(GContext *ctx, GRect rect,
                                          GColor top, GColor bottom) {
  gradient_fill_vertical_rgb(ctx, rect, grad_from_gcolor(top), grad_from_gcolor(bottom));
}
static inline void gradient_fill_horizontal(GContext *ctx, GRect rect,
                                            GColor left, GColor right) {
  gradient_fill_horizontal_rgb(ctx, rect, grad_from_gcolor(left), grad_from_gcolor(right));
}
static inline void gradient_fill_linear(GContext *ctx, GRect rect,
                                        GColor c0, GColor c1, int32_t angle) {
  gradient_fill_linear_rgb(ctx, rect, grad_from_gcolor(c0), grad_from_gcolor(c1), angle);
}
static inline void gradient_fill_radial(GContext *ctx, GPoint center,
                                        int16_t r_inner, int16_t r_outer,
                                        GColor c_inner, GColor c_outer, GRect clip) {
  gradient_fill_radial_rgb(ctx, center, r_inner, r_outer,
                           grad_from_gcolor(c_inner), grad_from_gcolor(c_outer), clip);
}
