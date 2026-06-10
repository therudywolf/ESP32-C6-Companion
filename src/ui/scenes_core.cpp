/* Nocturne C6 — scenes: titles, DEN (home), DASH. */
#include "core/config.h"
#include "pet/wolf_sprites.h"
#include "ui/Scenes.h"

using namespace theme;
using namespace widgets;

namespace scenes {

const char *title(int scene) {
  switch (scene) {
  case SCENE_DEN: return "ЛОГОВО";
  case SCENE_DASH: return "ОБЗОР";
  case SCENE_CPU: return "CPU";
  case SCENE_GPU: return "GPU";
  case SCENE_RAM: return "ПАМЯТЬ";
  case SCENE_DISKS: return "ДИСКИ";
  case SCENE_FANS: return "КУЛЕРЫ";
  case SCENE_MB: return "ПЛАТА";
  case SCENE_NET: return "СЕТЬ";
  case SCENE_MEDIA: return "МЕДИА";
  case SCENE_WEATHER: return "ПОГОДА";
  case SCENE_CLAUDE: return "CLAUDE";
  case SCENE_FOREST: return "ЛЕС";
  case SCENE_SERVICES: return "СЕРВИСЫ";
  case SCENE_EVENTS: return "СОБЫТИЯ";
  case SCENE_FORZA: return "FORZA";
  default: return "NOCTURNE";
  }
}

const char *actionHint(int scene, UiCtx &ui) {
  switch (scene) {
  case SCENE_DEN: {
    if (!ui.pet.isAlive()) return "—оживить";
    return "—действие";
  }
  case SCENE_CLAUDE: return "—обновить";
  case SCENE_FOREST:
  case SCENE_SERVICES: return "—обновить";
  default: return nullptr;
  }
}

void noSignal(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  bool blink = (ui.now / 600) & 1;
  textCenter(g, NOCT_W / 2, 70, "НЕТ СИГНАЛА", blink ? CRIT : ORANGE_DIM);
  g.setTextSize(1);
  g.setFont(&F_SMALL);
  if (!ui.st.link.wifiConnected) {
    textCenter(g, NOCT_W / 2, 95, "ищу WiFi...", DIM);
  } else {
    char buf[96];
    snprintf(buf, sizeof(buf), "WiFi: %s — сервер молчит", ui.st.link.ssid);
    textCenter(g, NOCT_W / 2, 95, buf, DIM);
  }
}

/* ── DEN — the wolf's home ───────────────────────────────────────────── */

static const unsigned char *wolfFrame(UiCtx &ui) {
  unsigned long now = ui.now;
  if (!ui.pet.isAlive()) return wolf_blink;
  if (ui.st.alertActive) return wolf_aggressive;
  if (ui.brain.talkingAnim(now))
    return ((now / 160) & 1) ? wolf_funny : wolf_idle;
  if (ui.pet.isSleeping()) return wolf_blink;
  if (ui.pet.mood() == 2 && (now % 6400) < 1300) return wolf_funny;
  if ((now % 3200) < 220) return wolf_blink; /* natural blink */
  return wolf_idle;
}

void drawDen(UiCtx &ui, int actionSel, bool actionMode) {
  LGFX_Sprite &g = ui.g;
  unsigned long now = ui.now;

  /* wolf, 2x chunky pixels; breathing offset when idle */
  int wx = 14, wy = 38;
  if (!ui.pet.isSleeping() && ui.pet.isAlive())
    wy += ((now % 2400) < 1200) ? 0 : 1;
  uint16_t wc = !ui.pet.isAlive() ? DIM
                : ui.st.alertActive ? CRIT
                                    : ORANGE;
  xbmScaled(g, wx, wy, wolfFrame(ui), WOLF_SPR_W, WOLF_SPR_H, 2, wc);

  /* tech bracket around the wolf */
  int bx = wx - 6, by = wy - 6, bs = 76;
  g.drawFastHLine(bx, by, 12, ORANGE_DIM);
  g.drawFastVLine(bx, by, 12, ORANGE_DIM);
  g.drawFastHLine(bx + bs - 12, by, 12, ORANGE_DIM);
  g.drawFastVLine(bx + bs, by, 12, ORANGE_DIM);
  g.drawFastHLine(bx, by + bs, 12, ORANGE_DIM);
  g.drawFastVLine(bx, by + bs - 12, 12, ORANGE_DIM);
  g.drawFastHLine(bx + bs - 12, by + bs, 12, ORANGE_DIM);
  g.drawFastVLine(bx + bs, by + bs - 12, 12, ORANGE_DIM);

  /* Zzz while sleeping */
  if (ui.pet.isSleeping() && ui.pet.isAlive()) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    int ph = (now / 700) % 3;
    textAt(g, wx + 62, wy - 2 - ph * 4, "z", INFO);
    g.setTextSize(2);
    if (ph > 0) textAt(g, wx + 70, wy - 14 - ph * 3, "Z", INFO);
    g.setTextSize(1);
  }

  /* right column: stats */
  int sx = 226, sw = 88;
  g.setFont(&F_TEXT);
  char v[16];
  snprintf(v, sizeof(v), "%d", ui.pet.hunger());
  labelBar(g, sx, 28, sw, "СЫТОСТЬ", ui.pet.hunger(), v,
           ui.pet.hunger() < 25 ? CRIT : ORANGE);
  snprintf(v, sizeof(v), "%d", ui.pet.happy());
  labelBar(g, sx, 54, sw, "РАДОСТЬ", ui.pet.happy(), v,
           ui.pet.happy() < 25 ? WARN : ORANGE);
  snprintf(v, sizeof(v), "%d", ui.pet.energy());
  labelBar(g, sx, 80, sw, "ЭНЕРГИЯ", ui.pet.energy(), v,
           ui.pet.energy() < 25 ? INFO : ORANGE);

  /* age + deterministic status */
  g.setFont(&F_SMALL);
  char age[24];
  snprintf(age, sizeof(age), "возраст: %luд", (unsigned long)ui.pet.ageDays());
  textAt(g, sx, 104, age, DIM);
  g.setFont(&F_TEXT);
  textAt(g, sx, 114, ui.pet.statusText(), TEXT);

  /* speech bubble (covers the stats while talking) or ambient PC status */
  if (ui.brain.bubbleVisible(now)) {
    speechBubble(ui, 100, 26, 214, 94);
  } else if (ui.st.link.tcpConnected && !ui.st.link.signalLost) {
    int px = 104, py = 30;
    char t[16];
    g.setFont(&F_TEXT);
    textAt(g, px, py + 4, "CPU", DIM);
    g.setTextSize(2);
    snprintf(t, sizeof(t), "%dC %d%%", ui.st.hw.ct, ui.st.hw.cl);
    textAt(g, px + 28, py, t, tempColor(ui.st.hw.ct, 75, 85));
    g.setTextSize(1);
    textAt(g, px, py + 24, "GPU", DIM);
    g.setTextSize(2);
    snprintf(t, sizeof(t), "%dC %d%%", ui.st.hw.gt, ui.st.hw.gl);
    textAt(g, px + 28, py + 20, t, tempColor(ui.st.hw.gt, 70, 80));
    g.setTextSize(1);
    /* server clock, huge */
    if (ui.st.pcClock[0]) {
      g.setFont(&F_HUGE);
      textAt(g, px, py + 44, ui.st.pcClock, ORANGE_DIM);
    }
  }

  /* action chips — big and obvious; selector frame blinks in action mode */
  static const char *names[] = {"КОРМИТЬ", "ИГРАТЬ", "ГОВОРИТЬ"};
  int cx = 10;
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  for (int i = 0; i < 3; i++) {
    int cw = g.textWidth(names[i]) + 14;
    bool sel = (i == actionSel);
    if (sel && actionMode) {
      g.fillRoundRect(cx, 126, cw, 22, 4, ORANGE);
      textAt(g, cx + 7, 129, names[i], BG);
      if ((now / 300) & 1)
        g.drawRoundRect(cx - 2, 124, cw + 4, 26, 5, TEXT);
    } else if (sel) {
      g.drawRoundRect(cx, 126, cw, 22, 4, ORANGE);
      textAt(g, cx + 7, 129, names[i], ORANGE);
    } else {
      g.drawRoundRect(cx, 126, cw, 22, 4, ORANGE_DIM);
      textAt(g, cx + 7, 129, names[i], DIM);
    }
    cx += cw + 7;
  }
  g.setTextSize(1);
}

/* ── DASH — 2x2 overview ─────────────────────────────────────────────── */

void drawDash(UiCtx &ui) {
  if (!ui.st.link.tcpConnected || !ui.st.hw.cc) {
    if (ui.st.link.signalLost) {
      noSignal(ui);
      return;
    }
  }
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[24];

  struct Tile {
    int x, y, w, h;
  };
  const Tile t[4] = {{4, 26, 154, 60}, {162, 26, 154, 60},
                     {4, 90, 154, 60}, {162, 90, 154, 60}};

  /* CPU */
  panel(g, t[0].x, t[0].y, t[0].w, t[0].h, "CPU");
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), "%d", hw.ct);
  int vw = g.textWidth(v);
  textAt(g, t[0].x + 8, t[0].y + 10, v, tempColor(hw.ct, 75, 85));
  g.setFont(&F_TEXT);
  textAt(g, t[0].x + 11 + vw, t[0].y + 12, "C", DIM);
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%d%%", hw.cl);
  textRight(g, t[0].x + t[0].w - 8, t[0].y + 8, v, TEXT);
  sparkline(g, t[0].x + 70, t[0].y + 22, t[0].w - 78, 20, ui.gr.cpuLoad, GOOD);
  hBar(g, t[0].x + 6, t[0].y + 46, t[0].w - 12, 9, hw.cl, pctColor(hw.cl));

  /* GPU */
  panel(g, t[1].x, t[1].y, t[1].w, t[1].h, "GPU");
  g.setFont(&F_BIG);
  snprintf(v, sizeof(v), "%d", hw.gt);
  vw = g.textWidth(v);
  textAt(g, t[1].x + 8, t[1].y + 10, v, tempColor(hw.gt, 70, 80));
  g.setFont(&F_TEXT);
  textAt(g, t[1].x + 11 + vw, t[1].y + 12, "C", DIM);
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%d%%", hw.gl);
  textRight(g, t[1].x + t[1].w - 8, t[1].y + 8, v, TEXT);
  sparkline(g, t[1].x + 70, t[1].y + 22, t[1].w - 78, 20, ui.gr.gpuLoad, GOOD);
  hBar(g, t[1].x + 6, t[1].y + 46, t[1].w - 12, 9, hw.gl, pctColor(hw.gl));

  /* RAM */
  panel(g, t[2].x, t[2].y, t[2].w, t[2].h, "RAM");
  int rpct = hw.ra > 0.1f ? (int)(hw.ru * 100 / hw.ra) : 0;
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%.1f", hw.ru);
  textAt(g, t[2].x + 8, t[2].y + 14, v, pctColor(rpct));
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "/ %.0fG", hw.ra);
  textAt(g, t[2].x + 56, t[2].y + 17, v, DIM);
  snprintf(v, sizeof(v), "%d%%", rpct);
  textRight(g, t[2].x + t[2].w - 8, t[2].y + 8, v, TEXT);
  hBar(g, t[2].x + 6, t[2].y + 40, t[2].w - 12, 12, rpct, pctColor(rpct));

  /* NET */
  panel(g, t[3].x, t[3].y, t[3].w, t[3].h, "NET");
  char r1[12], r2[12];
  fmtRate(r1, sizeof(r1), hw.nd);
  fmtRate(r2, sizeof(r2), hw.nu);
  g.setFont(&F_TEXT);
  g.fillTriangle(t[3].x + 8, t[3].y + 10, t[3].x + 14, t[3].y + 10,
                 t[3].x + 11, t[3].y + 15, INFO); /* down */
  textAt(g, t[3].x + 18, t[3].y + 8, r1, INFO);
  g.fillTriangle(t[3].x + 8, t[3].y + 29, t[3].x + 14, t[3].y + 29,
                 t[3].x + 11, t[3].y + 24, GOOD); /* up */
  textAt(g, t[3].x + 18, t[3].y + 22, r2, GOOD);
  snprintf(v, sizeof(v), "ping %dms", hw.pg);
  textRight(g, t[3].x + t[3].w - 8, t[3].y + 8, v, DIM);
  sparkline(g, t[3].x + 64, t[3].y + 22, t[3].w - 72, 30, ui.gr.netDown,
            INFO, 1000);
}

} // namespace scenes
