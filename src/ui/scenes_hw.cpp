/* Nocturne C6 — hardware scenes: CPU, GPU, RAM, DISKS, FANS, MB, NET.
 * Metre-readability rule: one hero number per panel (32-64 px), secondary
 * data at 16 px minimum, tertiary data dropped. */
#include "core/config.h"
#include "ui/Scenes.h"

using namespace theme;
using namespace widgets;

namespace scenes {

static bool gate(UiCtx &ui) {
  if (ui.st.link.dataDead) {
    noSignal(ui);
    return false;
  }
  return true;
}

/* y to pass to textAt so the visible glyph TOP lands at wantTop (the u8g2
 * logisoso line box is taller than the digits — leading sits above). */
static int inkTop(LGFX_Sprite &g, int wantTop, int inkH) {
  int off = g.fontHeight() - inkH;
  if (off < 0) off = 0;
  return wantTop - off / 2; /* leading splits above/below the ink */
}

/* 64 px hero temperature with a small unit; y = desired INK top */
static void heroTemp(LGFX_Sprite &g, int x, int y, int t, uint16_t c) {
  char v[8];
  snprintf(v, sizeof(v), "%d", t);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  int vw = g.textWidth(v);
  textAt(g, x, inkTop(g, y, 64), v, c);
  g.setTextSize(1);
  g.setFont(&F_MED);
  textAt(g, x + vw + 4, y + 44, "C", DIM);
}

/* big number + small unit on one baseline */
/* `y`/`h` are the BAND the value must sit in, not a cursor position.
 *
 * F_BIG writes a 35 px line for 24 px of digits, so a caller placing the
 * cursor by eye clips its own number against the tile frame — measured, that
 * is exactly what CPU's RPM and watts were doing, by a pixel each. The unit
 * hangs off the number's baseline so the pair reads as one reading. */
static void bigVal(LGFX_Sprite &g, int x, int y, int h, const char *num,
                   const char *unit, uint16_t c, bool rightAlign = false) {
  g.setFont(&F_BIG);
  int nw = g.textWidth(num);
  g.setFont(&F_TEXT);
  int uw = unit ? g.textWidth(unit) : 0;
  int x0 = rightAlign ? x - nw - uw - 4 : x;
  g.setFont(&F_BIG);
  int ty = inkY(INK_BIG, y, h);
  textAt(g, x0, ty, num, c);
  if (unit) {
    g.setFont(&F_TEXT);
    int base = ty + INK_BIG.top + INK_BIG.height;
    textAt(g, x0 + nw + 4, base - INK_TEXT.top - INK_TEXT.height, unit, DIM);
  }
}

void drawCpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
  heroTemp(g, 14, 36, hw.ct, tempColor(hw.ct, 75, 85));
  trendArrow(g, 118, 32, ui.gr.cpuTemp, 8, 1);

  panel(g, 140, 26, 176, 50, "НАГРУЗКА");
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", hw.cl);
  textAt(g, 148, 36, v, pctColor(hw.cl));
  g.setTextSize(1);
  trendArrow(g, 218, 34, ui.gr.cpuLoad, 8, 3);
  sparkline(g, 232, 34, 76, 34, ui.gr.cpuLoad, GOOD);

  panel(g, 140, 82, 176, 32, "КУЛЕР / ПИТАНИЕ");
  snprintf(v, sizeof(v), "%d", hw.fans[0]);
  bigVal(g, 148, 85, 26, v, "RPM", TEXT);   /* плитка 82..113 */
  snprintf(v, sizeof(v), "%d", hw.pw);
  bigVal(g, 308, 85, 26, v, "Вт", TEXT, true); /* не "W": в этом шрифте он читается как "ш" */

  /* grown into the freed bottom band: top-2 CPU processes + clock */
  panel(g, 4, 120, 312, 48, "ТОП ПРОЦЕССЫ / ТАКТ");
  g.setFont(&F_MED);
  g.setTextSize(1);
  for (int i = 0; i < 2; i++) {
    if (!ui.st.process.cpuNames[i].length()) continue;
    snprintf(v, sizeof(v), "%.13s %d%%", ui.st.process.cpuNames[i].c_str(),
             ui.st.process.cpuPercent[i]);
    textAt(g, 12, 128 + i * 20, v, i == 0 ? TEXT : DIM);
  }
  snprintf(v, sizeof(v), "%.1f", hw.cc / 1000.0f);
  bigVal(g, 306, 123, 42, v, "GHz", INFO, true); /* плитка 120..167 */
}

void drawGpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
  heroTemp(g, 14, 36, hw.gt, tempColor(hw.gt, 70, 80));
  trendArrow(g, 118, 32, ui.gr.gpuTemp, 8, 1);

  panel(g, 140, 26, 176, 50, "НАГРУЗКА");
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", hw.gl);
  textAt(g, 148, 36, v, pctColor(hw.gl));
  g.setTextSize(1);
  trendArrow(g, 218, 34, ui.gr.gpuLoad, 8, 3);
  sparkline(g, 232, 34, 76, 34, ui.gr.gpuLoad, GOOD);

  panel(g, 140, 82, 176, 32, "VRAM");
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%.1f/%.0fG", hw.vu, hw.vt);
  textAt(g, 148, 90, v, TEXT);
  g.setTextSize(1);
  hBar(g, 252, 90, 56, 14, hw.gv, pctColor(hw.gv));

  /* grown into the freed bottom band: clock/power/hotspot + fan & mem clock */
  panel(g, 4, 120, 312, 48, "ТАКТ / ПИТАНИЕ / ГОР.ТОЧКА");
  snprintf(v, sizeof(v), "%d", hw.gclock);
  bigVal(g, 14, 119, 26, v, "МГц", TEXT);  /* плитка 120..167 */
  snprintf(v, sizeof(v), "%d", hw.gtdp);
  bigVal(g, 178, 119, 26, v, "Вт", TEXT);
  snprintf(v, sizeof(v), "%d", hw.gh);
  bigVal(g, 306, 119, 26, v, "C", tempColor(hw.gh, 85, 95), true);
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  /* the literal alone is 38 B of UTF-8, so this needs its own wider buffer */
  /* F_VALUE for the numbers, F_TEXT for the words that introduce them.
   * Both were F_TEXT, which put the GPU's fan speed two steps below the same
   * reading on the CPU screen — one figure, two sizes, on adjacent scenes. */
  /* Two readings on the tile's own bottom line, below the three heroes.
   * The units stay in F_TEXT because F_VALUE cannot spell them — it is a
   * Latin subset and drew "МГц" as three hollow boxes, which is how this
   * looked on the first attempt. */
  char foot[16];
  g.setFont(&F_TEXT);
  textAt(g, 14, 158, "кулер", DIM);
  g.setFont(&F_VALUE);
  snprintf(foot, sizeof(foot), "%d", hw.gf);
  textAt(g, 52, 154, foot, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 92, 158, "об/мин", DIM);
  g.setFont(&F_TEXT);
  textAt(g, 150, 158, "память", DIM);
  g.setFont(&F_VALUE);
  snprintf(foot, sizeof(foot), "%d", hw.vclock);
  textAt(g, 198, 154, foot, TEXT);
  g.setFont(&F_TEXT);
  textAt(g, 246, 158, "МГц", DIM);
}

void drawRam(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];
  int rpct = hw.ra > 0.1f ? (int)(hw.ru * 100 / hw.ra) : 0;

  panel(g, 4, 26, 312, 74, "ОПЕРАТИВКА");
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%.1f", hw.ru);
  int vw = g.textWidth(v);
  textAt(g, 14, inkTop(g, 34, 64), v, pctColor(rpct));
  g.setTextSize(1);
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "/%.0fГБ", hw.ra);
  textAt(g, 22 + vw, 76, v, DIM);
  g.setTextSize(1);
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", rpct);
  textRight(g, 306, 34, v, pctColor(rpct));
  g.setTextSize(1);
  hBar(g, 230, 70, 76, 18, rpct, pctColor(rpct));

  /* grown into the freed bottom band: top-2 processes + free memory */
  panel(g, 4, 108, 312, 58, "ТОП ПО ПАМЯТИ / СВОБОДНО");
  g.setFont(&F_MED);
  for (int i = 0; i < 2; i++) {
    if (ui.st.process.ramNames[i].length() == 0) continue;
    int y = 112 + i * 18;
    snprintf(v, sizeof(v), "%.14s", ui.st.process.ramNames[i].c_str());
    textAt(g, 12, y, v, i == 0 ? TEXT : DIM);
    snprintf(v, sizeof(v), "%d МБ", ui.st.process.ramMb[i]);
    textRight(g, 306, y, v, INFO);
  }
  float freeGb = hw.ra - hw.ru;
  if (freeGb < 0) freeGb = 0;
  /* The only place the free figure appears, and it was set in the smallest
   * face on the screen with 200 px of empty tile to its right. */
  /* "ГБ" cannot be set in F_VALUE — Latin subset, hollow boxes. Digits in
   * F_VALUE, Cyrillic in F_TEXT, and the two sit on one baseline. */
  g.setFont(&F_TEXT);
  textAt(g, 12, 156, "свободно", DIM);
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%.1f", freeGb);
  textAt(g, 74, 152, v, GOOD);
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "ГБ из %.0f", hw.ra);
  textAt(g, 118, 156, v, DIM);
}

void drawDisks(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];

  /* 4 fixed rows at 30px pitch (y26,56,86,116): label+stats line + a bar
   * just under it. Everything stays inside its row; I/O panel sits below. */
  static const int rowY[NOCT_HDD_COUNT] = {26, 56, 86, 116};
  for (int i = 0; i < NOCT_HDD_COUNT; i++) {
    HddEntry &d = hw.hdd[i];
    if (d.total_gb < 0.1f) continue;
    int y = rowY[i];
    int pct = (int)(d.used_gb * 100 / d.total_gb);
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%s", d.name);
    textAt(g, 8, y, v, ORANGE);
    if (d.total_gb >= 1000)
      snprintf(v, sizeof(v), "%.2f/%.2fT", d.used_gb / 1000, d.total_gb / 1000);
    else
      snprintf(v, sizeof(v), "%.0f/%.0fG", d.used_gb, d.total_gb);
    textAt(g, 150, y, v, TEXT);
    snprintf(v, sizeof(v), "%dC", d.temp);
    textRight(g, 314, y, v, tempColor(d.temp, 45, 55));
    /* y+20, не y+18: строка ёмкости содержит косую черту, а она свисает
     * под базовую линию, и полоса срезала ей хвост на два ряда. */
    hBar(g, 30, y + 20, 284, 8, pct, pctColor(pct)); /* y+20..y+27 */
  }

  /* Disk I/O along the bottom, as a RULE rather than a tile.
   *
   * A tile costs a label row, and panel() draws that label as a background
   * wipe three rows above its own top edge — on a 172 px screen with the
   * fourth bar ending at 143 there is nowhere to put it that does not erase
   * something. The rule says the same thing in one pixel.
   *
   * MB/s rather than KB/s: this is an NVMe, and four or five digits of
   * kilobytes is a number nobody reads as a speed. */
  g.drawFastHLine(8, 146, NOCT_W - 16, ORANGE_DIM);
  char r1[12], r2[12];
  fmtRateMb(r1, sizeof(r1), hw.dr);
  fmtRateMb(r2, sizeof(r2), hw.dw);
  const int ioY = inkY(INK_MED, 150, 20);
  g.setFont(&F_MED);
  g.fillTriangle(12, 154, 22, 154, 17, 162, INFO);
  snprintf(v, sizeof(v), "%s", r1);
  textAt(g, 28, ioY, v, INFO);
  g.fillTriangle(172, 162, 182, 162, 177, 154, WARN);
  snprintf(v, sizeof(v), "%s", r2);
  textAt(g, 188, ioY, v, WARN);
  g.setFont(&F_TEXT);
  textRight(g, NOCT_W - 8, ioY + 5, "МБ/с", DIM);
}

void drawFans(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  static const char *names[] = {"CPU", "ПОМПА", "GPU", "КОРПУС"};
  char v[40];

  int sum = 0, mx = 0;
  for (int i = 0; i < NOCT_FAN_COUNT; i++) {
    int x = 10 + i * 78;
    int rpm = hw.fans[i];
    int pct = hw.fan_controls[i];
    sum += pct;
    if (pct > mx) mx = pct;
    /* The bar shows the DUTY, the same number printed under it. It used to
     * show rpm/2200, so a CPU fan at 51% duty drew an 83% bar and the pump —
     * which lives near its top rpm by design — drew a nearly full one. Two
     * different quantities in one tile, one of them unlabelled.
     * When the board reports no duty (some headers give rpm only) the rpm
     * fallback is still better than an empty bar, so it stays as the else. */
    int barPct = pct > 0 ? pct : (rpm * 100 / 2200);
    if (barPct > 100) barPct = 100;
    vBar(g, x + 16, 28, 30, 68, barPct, rpm > 0 ? GOOD : PANEL);
    /* Name and duty share ONE line above the rpm. There is room for a label
     * row and a hero number between the bar (ends y96) and the footer rule
     * (y152), but not for three: F_BIG's ink is ~30 px, so a third line landed
     * inside the rpm digits. Putting the percentage beside the name also sits
     * it next to the bar that draws the same quantity. */
    /* The name stays small — it is a label. The duty does not: it is the
     * quantity the bar beside it draws, and at eight pixels it was 2.8 arc-
     * minutes from a metre away, below what an eye resolves at all. */
    /* The duty moved up onto its own row rather than sharing one with the
     * name: at F_VALUE it is 40 % wider than the F_SMALL it replaced, and in
     * a 62 px tile the two ran into each other ("КОРПУ85%"). */
    g.setFont(&F_SMALL);
    textCenter(g, x + 31, 98, names[i], DIM);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d%%", pct);
    textCenter(g, x + 31, 107, v, ORANGE);
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%d", rpm);
    textCenter(g, x + 31, 124, v, TEXT);
  }

  /* summary across the freed bottom band (F_TEXT stays inside y171) */
  if (uiOn(UI_STRIPS)) {
    g.drawFastHLine(8, 150, NOCT_W - 16, ORANGE_DIM);
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    char sum2[64]; /* "среднее ..." + "максимум ..." is 42 B before the numbers */
    snprintf(sum2, sizeof(sum2), "среднее %d%%      максимум %d%%",
             sum / NOCT_FAN_COUNT, mx);
    textCenter(g, NOCT_W / 2, 156, sum2, ORANGE);
  }
}

void drawMb(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  /* Only REAL motherboard temps (s1/s2/cf are just duplicate fan RPMs — they
   * live on the FANS scene). 6 tiles in a 3x2 grid, big numbers. */
  struct {
    const char *n;
    int t;
    int warn, crit;
  } tiles[] = {
      {"SYS", hw.mb_sys, 50, 60},      {"VSOC", hw.mb_vsoc, 70, 85},
      {"VRM", hw.mb_vrm, 70, 90},      {"ЧИПСЕТ", hw.mb_chipset, 60, 75},
      {"КОРПУС", hw.ch, 45, 55},       {"PCH", hw.ch, 45, 55},
  };
  /* 6 cells in a 3x2 grid filling y26..164 (taller than before) */
  char v[12];
  for (int i = 0; i < 5; i++) {
    int x = 6 + (i % 3) * 104;
    int y = 26 + (i / 3) * 70; /* rows y26..92, y96..162 */
    panel(g, x, y, 100, 66, tiles[i].n);
    snprintf(v, sizeof(v), "%d", tiles[i].t);
    g.setFont(&F_HUGE);
    uint16_t c = tempColor(tiles[i].t, tiles[i].warn, tiles[i].crit);
    int vw = g.textWidth(v);
    textAt(g, x + 12, y + 14 - (g.fontHeight() - 32) / 2, v, c);
    g.setFont(&F_MED);
    g.drawCircle(x + 18 + vw, y + 22, 4, DIM); /* ° */
    g.drawCircle(x + 18 + vw, y + 22, 3, DIM);
  }
  /* 6th cell: package power (the chipset-fan reading was bogus / irrelevant) */
  int x = 6 + 2 * 104, y = 96;
  panel(g, x, y, 100, 66, "ПИТАНИЕ");
  snprintf(v, sizeof(v), "%d", hw.pw);
  g.setFont(&F_HUGE);
  int vw = g.textWidth(v);
  textAt(g, x + 12, y + 14 - (g.fontHeight() - 32) / 2, v, ACCENT);
  g.setFont(&F_MED);
  textAt(g, x + 16 + vw, y + 30, "Вт", DIM);
}

void drawNet(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40], r[12];

  panel(g, 4, 26, 156, 60, "ВХОДЯЩИЙ");
  fmtRate(r, sizeof(r), hw.nd);
  /* Плитка 26..85. Значению нужно 24 ряда чернил, графику 18 — вместе с
   * зазором это ровно то, что есть, но полоса в 50 рядов под значение
   * опускала его чернила на шесть рядов в график. */
  bigVal(g, 14, 30, 26, r, "Б/с", INFO);
  sparkline(g, 14, 60, 134, 18, ui.gr.netDown, INFO, 1000);

  panel(g, 164, 26, 152, 60, "ИСХОДЯЩИЙ");
  fmtRate(r, sizeof(r), hw.nu);
  bigVal(g, 174, 30, 26, r, "Б/с", GOOD);
  sparkline(g, 174, 60, 132, 18, ui.gr.netUp, GOOD, 200);

  /* bottom panels grown into the freed band (y94..168), spacing fixed so the
   * RSSI line and the server line no longer overlap */
  panel(g, 4, 94, 156, 74, "ПИНГ");
  snprintf(v, sizeof(v), "%d", hw.pg);
  bigVal(g, 14, 101, 60, v, "ms", hw.pg > 80 ? WARN : GOOD); /* 94..167 */
  g.setFont(&F_MED);
  textAt(g, 14, 142, "google:443", DIM);

  panel(g, 164, 94, 152, 74, "УСТРОЙСТВО");
  g.setFont(&F_MED);
  g.setTextSize(1);
  clipW(g, ui.st.link.ssid, v, sizeof(v), 140); /* codepoint-safe (no mid-glyph cut) */
  textAt(g, 172, 102, v, TEXT);          /* y102..122 */
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "RSSI %d dBm", ui.st.link.rssi);
  textAt(g, 172, 126, v, DIM);           /* y126..139 */
  g.setFont(&F_MED);
  textAt(g, 172, 146, ui.st.link.tcpConnected ? "сервер: ок" : "сервер: нет",
         ui.st.link.tcpConnected ? GOOD : CRIT); /* y146..166 */
}

} // namespace scenes
