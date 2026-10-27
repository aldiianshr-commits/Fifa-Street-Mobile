/* FIFA STREET Recompiled - homebrew input/render demo (original code, MIT).
 *
 * fsr_frame() is called by the host once per video frame with a guest
 * framebuffer (RGBA8 stored as big-endian 0xRRGGBBAA words). It polls the
 * pad through the real XAM import XamInputGetState, so the Android touch
 * controller / gamepad -> runtime -> recompiled guest code path is exercised
 * end to end. A ball follows the left stick / D-pad; face buttons change its
 * colour; triggers change its size; the 14 squares at the bottom light up for
 * pressed buttons.
 */
#include <stdint.h>

typedef struct {
  uint32_t packet;
  uint16_t buttons;
  uint8_t lt, rt;
  int16_t lx, ly, rx, ry;
} XINPUT_STATE;

uint32_t XamInputGetState(uint32_t user, uint32_t flags, XINPUT_STATE* state);
uint32_t DbgPrint(const char* fmt, ...);

static int32_t g_x = 320 << 8, g_y = 180 << 8;
static uint32_t g_color = 0xFFD23CFFu;
static int g_started;

void fsr_demo_reset(void) {
  g_x = 320 << 8;
  g_y = 180 << 8;
  g_color = 0xFFD23CFFu;
  g_started = 0;
}

static inline void put(uint32_t* fb, int w, int x, int y, uint32_t c) { fb[y * w + x] = c; }

static void rect(uint32_t* fb, int w, int h, int x0, int y0, int rw, int rh, uint32_t c) {
  for (int y = y0; y < y0 + rh; y++) {
    if (y < 0 || y >= h) continue;
    for (int x = x0; x < x0 + rw; x++)
      if (x >= 0 && x < w) put(fb, w, x, y, c);
  }
}

uint32_t fsr_frame(uint32_t* fb, int32_t w, int32_t h, uint32_t frame) {
  if (!g_started) {
    g_started = 1;
    DbgPrint("homebrew demo running: %dx%d\n", w, h);
  }
  XINPUT_STATE st;
  uint32_t buttons = 0;
  int32_t lx = 0, ly = 0, size = 18;
  if (XamInputGetState(0, 0, &st) == 0) {
    buttons = st.buttons;
    lx = st.lx;
    ly = st.ly;
    size += (st.lt + st.rt) / 16;
  }
  if (buttons & 0x0004) lx = -20000;
  if (buttons & 0x0008) lx = 20000;
  if (buttons & 0x0001) ly = 20000;
  if (buttons & 0x0002) ly = -20000;
  if (lx > 4000 || lx < -4000) g_x += lx / 64;
  if (ly > 4000 || ly < -4000) g_y -= ly / 64;
  if (g_x < 0) g_x = 0;
  if (g_y < 0) g_y = 0;
  if (g_x > (w - 1) << 8) g_x = (w - 1) << 8;
  if (g_y > (h - 1) << 8) g_y = (h - 1) << 8;
  if (buttons & 0x1000) g_color = 0x3CD25AFFu;  /* A green */
  if (buttons & 0x2000) g_color = 0xE63C3CFFu;  /* B red */
  if (buttons & 0x4000) g_color = 0x3C78F0FFu;  /* X blue */
  if (buttons & 0x8000) g_color = 0xF0D23CFFu;  /* Y yellow */

  /* Background: animated street-court gradient. */
  for (int y = 0; y < h; y++) {
    uint32_t g = (uint32_t)(40 + (y * 60) / h);
    for (int x = 0; x < w; x++) {
      uint32_t r = (uint32_t)(20 + ((x + (int)frame) & 63));
      uint32_t b = (uint32_t)(50 + ((x ^ y) & 31));
      put(fb, w, x, y, r << 24 | g << 16 | b << 8 | 0xFF);
    }
  }
  /* Court lines. */
  rect(fb, w, h, 0, h / 2, w, 2, 0xE6E6E6FFu);
  rect(fb, w, h, w / 2 - 1, 0, 2, h, 0xE6E6E6FFu);
  /* Ball. */
  int cx = g_x >> 8, cy = g_y >> 8;
  for (int dy = -size; dy <= size; dy++)
    for (int dx = -size; dx <= size; dx++)
      if (dx * dx + dy * dy <= size * size) {
        int x = cx + dx, y = cy + dy;
        if (x >= 0 && y >= 0 && x < w && y < h) put(fb, w, x, y, ((dx ^ dy) & 4) ? g_color : 0x202020FFu);
      }
  /* Button indicators. */
  for (int i = 0; i < 16; i++) {
    uint32_t on = buttons & (1u << i);
    rect(fb, w, h, 8 + i * 22, h - 22, 18, 14, on ? 0xFFFFFFFFu : 0x000000A0u);
  }
  return buttons;
}
