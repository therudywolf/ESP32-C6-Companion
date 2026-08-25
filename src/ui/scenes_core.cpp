/* Nocturne C6 — scenes: titles, DEN (home), DASH. */
#include <math.h>

#include "core/config.h"
#include "pet/wolf_sprites.h"
#include "core/Barometer.h"
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
  case SCENE_HISTORY: return "ИСТОРИЯ / ЧАС";
  case SCENE_HOME: return "ДОМ";
  case SCENE_BOARD: return "ПЛАТА C6";
  case SCENE_MOTION: return "ДВИЖЕНИЕ";
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
  case SCENE_HISTORY: return "—час/сутки";
  case SCENE_HOME: return "—окно графика";
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
  g.setFont(&F_TEXT);
  char buf[96];
  if (!ui.st.link.wifiConnected) {
    textCenter(g, NOCT_W / 2, 95, "ищу WiFi...", DIM);
  } else {
    snprintf(buf, sizeof(buf), "WiFi: %s - сервер молчит", ui.st.link.ssid);
    textCenter(g, NOCT_W / 2, 95, buf, DIM);
  }
  /* How long the numbers behind this screen have been frozen. A blank that
   * does not say "since when" leaves the owner unable to tell a reboot from a
   * PC that died on Tuesday. */
  if (ui.st.payloadAgeSec >= 0) {
    int a = ui.st.payloadAgeSec;
    if (a < 3600) snprintf(buf, sizeof(buf), "последние данные %d мин назад", a / 60);
    else if (a < 86400) snprintf(buf, sizeof(buf), "последние данные %d ч назад", a / 3600);
    else snprintf(buf, sizeof(buf), "последние данные %d дн назад", a / 86400);
    textCenter(g, NOCT_W / 2, 114, buf, DIM);
  }
  /* And where there IS still something to look at. The board keeps a wolf, a
   * climate sensor, its own vitals and the card archive with the PC dark; a
   * screen that only says "no" teaches nothing about that. */
  if (ui.st.pcOffline) {
    g.setFont(&F_SMALL);
    textCenter(g, NOCT_W / 2, 136,
               "работают: ЛОГОВО · ДОМ · ПЛАТА C6 · ИСТОРИЯ", ORANGE_DIM);
    g.setFont(&F_TEXT);
  }
}

/* ── DEN — the wolf's home ───────────────────────────────────────────── */

/* Which frame the wolf shows right now. Named apart from the global
 * wolfFrame(id) it calls: an overload inside namespace scenes only resolved
 * through ADL on the enum, which is far too clever to leave lying around. */
static const unsigned char *currentWolfFrame(UiCtx &ui) {
  unsigned long now = ui.now;
  if (!ui.pet.isAlive()) return wolfFrame(WOLF_BLINK);
  if (ui.st.alertActive) return wolfFrame(WOLF_AGGRO);
  if (ui.brain.talkingAnim(now))
    return ((now / 160) & 1) ? wolfFrame(WOLF_FUNNY) : wolfFrame(WOLF_IDLE);
  if (ui.pet.isSleeping()) return wolfFrame(WOLF_BLINK);
  /* a pup is restless, an elder is not: same frames, different tempo */
  unsigned long playful = ui.pet.stage() == WolfPet::STAGE_PUP ? 4200 : 6400;
  unsigned long blinkEvery = ui.pet.stage() == WolfPet::STAGE_ELDER ? 5200 : 3200;
  if (ui.pet.mood() == 2 && (now % playful) < 1300) return wolfFrame(WOLF_FUNNY);
  if ((now % blinkEvery) < 220) return wolfFrame(WOLF_BLINK); /* natural blink */
  return wolfFrame(WOLF_IDLE);
}

void drawDen(UiCtx &ui, int actionSel, bool actionMode) {
  LGFX_Sprite &g = ui.g;
  unsigned long now = ui.now;

  /* ambient embers drifting up the den — subtle life, kept to the wolf's side */
  for (int i = 0; i < 4; i++) {
    int ex = 16 + (i * 47 + (int)(now / 70)) % 188;
    int ey = NOCT_H - 3 - (int)((now / 28 + i * 900) % 140);
    g.drawPixel(ex, ey, lerp565(BG, INFO, 26 + (i % 3) * 14));
  }

  /* wolf, 2x chunky pixels; breathing offset when idle, hop on a fresh action */
  unsigned long rAt = ui.brain.reactionAt();
  int wx = 14, wy = 38;
  if (!ui.pet.isSleeping() && ui.pet.isAlive())
    wy += ((now % 2400) < 1200) ? 0 : 1;
  if (rAt && now - rAt < 450)
    wy -= (int)(sinf((float)(now - rAt) / 450.0f * 3.14159f) * 6.0f);
  /* Age shows in the coat: a pup is brighter and busier, an elder greys out
   * toward TEXT. Same four frames, no new art — the wolf visibly ages. */
  uint16_t wc = !ui.pet.isAlive() ? DIM
                : ui.st.alertActive ? CRIT
                                    : ORANGE;
  if (ui.pet.isAlive() && !ui.st.alertActive) {
    if (ui.pet.stage() == WolfPet::STAGE_ELDER) wc = lerp565(ORANGE, TEXT, 110);
    else if (ui.pet.stage() == WolfPet::STAGE_PUP) wc = lerp565(ORANGE, WARN, 70);
  }
  xbmScaled(g, wx, wy, currentWolfFrame(ui), WOLF_SPR_W, WOLF_SPR_H, 2, wc);

  /* reaction burst: hearts (feed) / sparks (play) / notes (talk) float up */
  if (rAt && now - rAt < 1500) {
    float t = (float)(now - rAt) / 1500.0f;
    int kind = ui.brain.reactionKind();
    /* 0 feed=red heart, 1 play=spark, 2 pet=pink heart, 3 talk=note */
    uint16_t pc = kind == 0   ? CRIT
                  : kind == 1 ? ACCENT
                  : kind == 2 ? rgb(255, 120, 180)
                              : INFO;
    bool heart = (kind == 0 || kind == 2);
    for (int i = 0; i < 7 && t <= 0.92f; i++) {
      int px = wx + 30 + (int)(sinf(i * 0.9f + t * 3.0f) * 18);
      int py = wy + 24 - (int)(t * (46 + (i % 3) * 8));
      if (py < 22) continue;
      int sz = (int)((1.0f - t) * 3) + 1;
      if (heart) {
        g.fillCircle(px - 1, py, sz, pc);
        g.fillCircle(px + 1, py, sz, pc);
        g.fillTriangle(px - sz - 1, py, px + sz + 1, py, px, py + sz + 2, pc);
      } else if (kind == 1) { /* spark */
        g.drawFastHLine(px - sz - 1, py, 2 * sz + 3, pc);
        g.drawFastVLine(px, py - sz - 1, 2 * sz + 3, pc);
      } else { /* music note */
        g.fillCircle(px, py + 2, sz, pc);
        g.drawFastVLine(px + sz, py - sz - 2, sz + 4, pc);
      }
    }
  }

  /* tech bracket around the wolf — corners pulse in sequence (scanning HUD) */
  int bx = wx - 6, by = wy - 6, bs = 76;
  int activeCorner = (now / 350) % 4;
  uint16_t c0 = (activeCorner == 0) ? ORANGE : ORANGE_DIM;
  uint16_t c1 = (activeCorner == 1) ? ORANGE : ORANGE_DIM;
  uint16_t c2 = (activeCorner == 2) ? ORANGE : ORANGE_DIM;
  uint16_t c3 = (activeCorner == 3) ? ORANGE : ORANGE_DIM;
  g.drawFastHLine(bx, by, 12, c0);
  g.drawFastVLine(bx, by, 12, c0);
  g.drawFastHLine(bx + bs - 12, by, 12, c1);
  g.drawFastVLine(bx + bs, by, 12, c1);
  g.drawFastHLine(bx + bs - 12, by + bs, 12, c2);
  g.drawFastVLine(bx + bs, by + bs - 12, 12, c2);
  g.drawFastHLine(bx, by + bs, 12, c3);
  g.drawFastVLine(bx, by + bs - 12, 12, c3);

  /* Zzz while sleeping */
  if (ui.pet.isSleeping() && ui.pet.isAlive()) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    int ph = (now / 700) % 3;
    int zy = wy - 2 - ph * 4;
    if (zy < 21) zy = 21;
    textAt(g, wx + 62, zy, "z", INFO);
    g.setTextSize(2);
    int zy2 = wy - 12 - ph * 3;
    if (zy2 < 21) zy2 = 21;
    if (ph > 0) textAt(g, wx + 70, zy2, "Z", INFO);
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

  /* Deterministic status, big, with the life stage under it. The stage line
   * used to sit at y118 in F_TEXT, whose ink reaches y130 — straight through
   * the action buttons that start at y124. It also repeated the age in days,
   * which the bottom band already prints; the stage name alone is what this
   * line was for. */
  g.setFont(&F_MED);
  textAt(g, sx, 94, ui.pet.statusText(), ORANGE);
  {
    g.setFont(&F_SMALL);
    textAt(g, sx, 114, ui.pet.stageName(), DIM);
    g.setFont(&F_MED);
  }

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

  /* action chips — big and obvious; selector frame blinks in action mode.
   * Short imperatives so all four fit one row: feed/play/pet/talk. */
  static const char *names[] = {"КОРМИ", "ИГРАЙ", "ГЛАДЬ", "ГОВОРИ"};
  int cx = 10;
  g.setFont(&F_MED);
  for (int i = 0; i < WolfPet::ACT_COUNT; i++) {
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

  /* Bottom band: what the weather is about to do, and how long we have been
   * up. The forecast goes HERE rather than only on ДОМ because a barometric
   * state lasts hours and changes a couple of times a week — you want to know
   * it at a glance from the screen you actually sit in front of, not by
   * navigating to the one screen that owns the sensor. A toast announces the
   * change once; this is what the change left behind. */
  if (!actionMode && uiOn(UI_STRIPS)) {
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    char vb[80];
    unsigned long upm = now / 60000UL;

    bool said = false;
    if (ui.st.zbTrendOk) {
      auto t = barometer::classify(ui.st.zbPress10Delta3h, 3);
      if (t != barometer::TEND_STEADY && t != barometer::TEND_UNKNOWN) {
        /* Amber only for the fast drop — the one worth turning your head for.
         * Colouring every tendency would make the colour mean nothing. */
        uint16_t c = barometer::headacheWatch(t) ? WARN : ORANGE_DIM;
        int aw = baroArrow(g, 10, 160, barometer::direction(t),
                           barometer::isSharp(t), c);
        snprintf(vb, sizeof(vb), "%s", barometer::forecast(t));
        textAt(g, 10 + aw, 160, vb, c);
        said = true;
      }
    }
    if (!said) {
      /* ageDays() is uint32_t: %d was a format mismatch, and the widest case
       * overran the buffer by a few bytes. */
      snprintf(vb, sizeof(vb), "возраст %lu дн     в сети %luч %02luм",
               (unsigned long)ui.pet.ageDays(), upm / 60, upm % 60);
      textAt(g, 10, 160, vb, DIM);
    } else {
      snprintf(vb, sizeof(vb), "%luч %02luм", upm / 60, upm % 60);
      textRight(g, NOCT_W - 10, 160, vb, DIM);
    }
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
    textAt(g, t[k].x + 8, t[k].y + 8 - (g.fontHeight() - 32) / 2, v,
           tempColor(temp, cpu ? 75 : 70, cpu ? 85 : 80));
    g.setFont(&F_MED);
    g.setTextSize(1);
    textAt(g, t[k].x + 11 + vw, t[k].y + 24, "C", DIM);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d%%", load);
    textRight(g, t[k].x + t[k].w - 8, t[k].y + 8, v, pctColor(load));
    trendArrow(g, t[k].x + t[k].w - 12 - g.textWidth(v), t[k].y + 8,
               cpu ? ui.gr.cpuLoad : ui.gr.gpuLoad, 8, 3);
    g.setTextSize(1);
    hBar(g, t[k].x + 6, t[k].y + 45, t[k].w - 12, 11, load, pctColor(load));
  }

  /* RAM */
  panel(g, t[2].x, t[2].y, t[2].w, t[2].h, "RAM");
  int rpct = hw.ra > 0.1f ? (int)(hw.ru * 100 / hw.ra) : 0;
  g.setFont(&F_HUGE);
  snprintf(v, sizeof(v), "%.1f", hw.ru);
  int vw2 = g.textWidth(v);
  textAt(g, t[2].x + 8, t[2].y + 8 - (g.fontHeight() - 32) / 2, v,
         pctColor(rpct));
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "/%.0fG", hw.ra);
  textAt(g, t[2].x + 11 + vw2, t[2].y + 24, v, DIM);
  trendArrow(g, t[2].x + t[2].w - 14, t[2].y + 8, ui.gr.ramUsed, 8, 1);
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

  /* freed bottom band: total power draw + hottest component (F_TEXT keeps the
   * line inside y171 — F_MED's 20px cell would clip past the screen edge) */
  if (uiOn(UI_STRIPS)) {
    g.drawFastHLine(4, 153, NOCT_W - 8, ORANGE_DIM);
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    snprintf(v, sizeof(v), "питание %d Вт", hw.pw);
    textAt(g, 6, 158, v, INFO);
    int peak = hw.ct > hw.gt ? hw.ct : hw.gt;
    snprintf(v, sizeof(v), "пик %d C", peak);
    textRight(g, NOCT_W - 6, 158, v, tempColor(peak, 75, 85));
  }
}

} // namespace scenes
