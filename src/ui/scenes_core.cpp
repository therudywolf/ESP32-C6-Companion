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
  g.setFont(&F_MED);
  g.setTextSize(1);
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

  /* right column: stat bars with the label INSIDE (no stacking overlaps) */
  int sx = 222, sw = 94;
  auto statBar = [&](int y, const char *label, int val, uint16_t c) {
    g.drawRect(sx, y, sw, 22, ORANGE_DIM);
    int fill = (sw - 4) * val / 100;
    if (fill > 0) theme::ditherRect(g, sx + 2, y + 2, fill, 18, c);
    g.setFont(&F_MED);
    textAt(g, sx + 6, y + 2, label, BG); /* shadow for contrast */
    textAt(g, sx + 5, y + 1, label, TEXT);
  };
  statBar(24, "СЫТОСТЬ", ui.pet.hunger(),
          ui.pet.hunger() < 25 ? CRIT : GOOD);
  statBar(48, "РАДОСТЬ", ui.pet.happy(),
          ui.pet.happy() < 25 ? WARN : GOOD);
  statBar(72, "ЭНЕРГИЯ", ui.pet.energy(),
          ui.pet.energy() < 25 ? INFO : GOOD);

  /* deterministic status, big */
  g.setFont(&F_MED);
  textAt(g, sx, 98, ui.pet.statusText(), ORANGE);

  /* speech bubble (covers the stats while talking) or ambient PC status */
  if (ui.brain.bubbleVisible(now)) {
    speechBubble(ui, 100, 26, 214, 94);
  } else if (ui.st.link.tcpConnected && !ui.st.link.signalLost) {
    int px = 104, py = 28;
    char t[16];
    g.setFont(&F_TEXT);
    textAt(g, px, py + 6, "CPU", DIM);
    g.setFont(&F_MED);
    snprintf(t, sizeof(t), "%dC %d%%", ui.st.hw.ct, ui.st.hw.cl);
    textAt(g, px + 30, py, t, tempColor(ui.st.hw.ct, 75, 85));
    g.setFont(&F_TEXT);
    textAt(g, px, py + 28, "GPU", DIM);
    g.setFont(&F_MED);
    snprintf(t, sizeof(t), "%dC %d%%", ui.st.hw.gt, ui.st.hw.gl);
    textAt(g, px + 30, py + 22, t, tempColor(ui.st.hw.gt, 70, 80));
    /* server clock, huge */
    if (ui.st.pcClock[0]) {
      g.setFont(&F_HUGE);
      textAt(g, px, py + 48, ui.st.pcClock, ORANGE_DIM);
    }
  }

  /* action mode: dim the scene so the chips become THE focus */
  if (actionMode) {
    theme::ditherRect(g, 0, 20, NOCT_W, 100, BG);
    g.setFont(&F_MED);
    textAt(g, 10, 100, "ВЫБЕРИ ДЕЙСТВИЕ:", ORANGE);
  }

  /* action chips — big and obvious; selector frame blinks in action mode */
  static const char *names[] = {"КОРМИТЬ", "ИГРАТЬ", "ГОВОРИТЬ"};
  int cx = 10;
  g.setFont(&F_MED);
  for (int i = 0; i < 3; i++) {
    int cw = g.textWidth(names[i]) + 14;
    bool sel = (i == actionSel);
    if (sel && actionMode) {
      g.fillRoundRect(cx, 124, cw, 24, 4, ORANGE);
      textAt(g, cx + 7, 127, names[i], BG);
      if ((now / 300) & 1)
        g.drawRoundRect(cx - 2, 122, cw + 4, 28, 5, TEXT);
    } else if (sel) {
      g.drawRoundRect(cx, 124, cw, 24, 4, ORANGE);
      textAt(g, cx + 7, 127, names[i], ORANGE);
    } else {
      g.drawRoundRect(cx, 124, cw, 24, 4, ORANGE_DIM);
      textAt(g, cx + 7, 127, names[i], DIM);
    }
    cx += cw + 7;
  }
}

/* ── DASH — 2x2 overview ─────────────────────────────────────────────── */

void drawDash(UiCtx &ui) {
  if (ui.st.link.dataDead) {
    noSignal(ui);
    return;
  }
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[24];

  struct Tile {
    int x, y, w, h;
  };
  const Tile t[4] = {{4, 26, 154, 60}, {162, 26, 154, 60},
                     {4, 90, 154, 60}, {162, 90, 154, 60}};

  /* CPU / GPU: hero temp + load% + bar */
  for (int k = 0; k < 2; k++) {
    bool cpu = k == 0;
    panel(g, t[k].x, t[k].y, t[k].w, t[k].h, cpu ? "CPU" : "GPU");
    int temp = cpu ? hw.ct : hw.gt;
    int load = cpu ? hw.cl : hw.gl;
    g.setFont(&F_HUGE);
    snprintf(v, sizeof(v), "%d", temp);
    int vw = g.textWidth(v);
    textAt(g, t[k].x + 8, t[k].y + 8, v,
           tempColor(temp, cpu ? 75 : 70, cpu ? 85 : 80));
    g.setFont(&F_MED);
    g.setTextSize(1);
    textAt(g, t[k].x + 11 + vw, t[k].y + 24, "C", DIM);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d%%", load);
    textRight(g, t[k].x + t[k].w - 8, t[k].y + 8, v, pctColor(load));
    g.setTextSize(1);
    hBar(g, t[k].x + 6, t[k].y + 45, t[k].w - 12, 11, load, pctColor(load));
  }

  /* RAM */
  panel(g, t[2].x, t[2].y, t[2].w, t[2].h, "RAM");
  int rpct = hw.ra > 0.1f ? (int)(hw.ru * 100 / hw.ra) : 0;
  g.setFont(&F_HUGE);
  snprintf(v, sizeof(v), "%.1f", hw.ru);
  int vw2 = g.textWidth(v);
  textAt(g, t[2].x + 8, t[2].y + 8, v, pctColor(rpct));
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "/%.0fG", hw.ra);
  textAt(g, t[2].x + 11 + vw2, t[2].y + 24, v, DIM);
  g.setTextSize(1);
  hBar(g, t[2].x + 6, t[2].y + 45, t[2].w - 12, 11, rpct, pctColor(rpct));

  /* NET */
  panel(g, t[3].x, t[3].y, t[3].w, t[3].h, "NET");
  char r1[12], r2[12];
  fmtRate(r1, sizeof(r1), hw.nd);
  fmtRate(r2, sizeof(r2), hw.nu);
  g.fillTriangle(t[3].x + 10, t[3].y + 14, t[3].x + 20, t[3].y + 14,
                 t[3].x + 15, t[3].y + 22, INFO); /* down */
  g.setFont(&F_BIG);
  textAt(g, t[3].x + 26, t[3].y + 6, r1, INFO);
  g.fillTriangle(t[3].x + 10, t[3].y + 46, t[3].x + 20, t[3].y + 46,
                 t[3].x + 15, t[3].y + 38, GOOD); /* up */
  textAt(g, t[3].x + 26, t[3].y + 32, r2, GOOD);
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%dms", hw.pg);
  textRight(g, t[3].x + t[3].w - 8, t[3].y + 8, v, DIM);
  g.setTextSize(1);
}

} // namespace scenes
