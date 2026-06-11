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
static void bigVal(LGFX_Sprite &g, int x, int y, const char *num,
                   const char *unit, uint16_t c, bool rightAlign = false) {
  g.setFont(&F_BIG);
  int nw = g.textWidth(num);
  g.setFont(&F_TEXT);
  int uw = unit ? g.textWidth(unit) : 0;
  int x0 = rightAlign ? x - nw - uw - 4 : x;
  g.setFont(&F_BIG);
  textAt(g, x0, y, num, c);
  if (unit) {
    g.setFont(&F_TEXT);
    textAt(g, x0 + nw + 4, y + 13, unit, DIM);
  }
}

void drawCpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
  heroTemp(g, 14, 36, hw.ct, tempColor(hw.ct, 75, 85));

  panel(g, 140, 26, 176, 50, "НАГРУЗКА");
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", hw.cl);
  textAt(g, 148, 36, v, pctColor(hw.cl));
  g.setTextSize(1);
  sparkline(g, 232, 34, 76, 34, ui.gr.cpuLoad, GOOD);

  panel(g, 140, 82, 176, 32, "КУЛЕР / ПИТАНИЕ");
  snprintf(v, sizeof(v), "%d", hw.fans[0]);
  bigVal(g, 148, 86, v, "RPM", TEXT);
  snprintf(v, sizeof(v), "%d", hw.pw);
  bigVal(g, 308, 86, v, "W", TEXT, true);

  panel(g, 4, 120, 312, 30, "ТОП ПРОЦЕСС / ТАКТ");
  if (ui.st.process.cpuNames[0].length()) {
    g.setFont(&F_MED);
    g.setTextSize(1);
    snprintf(v, sizeof(v), "%.13s %d%%", ui.st.process.cpuNames[0].c_str(),
             ui.st.process.cpuPercent[0]);
    textAt(g, 12, 128, v, TEXT);
    g.setTextSize(1);
  }
  snprintf(v, sizeof(v), "%.1f", hw.cc / 1000.0f);
  bigVal(g, 306, 124, v, "GHz", INFO, true);
}

void drawGpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
  heroTemp(g, 14, 36, hw.gt, tempColor(hw.gt, 70, 80));

  panel(g, 140, 26, 176, 50, "НАГРУЗКА");
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", hw.gl);
  textAt(g, 148, 36, v, pctColor(hw.gl));
  g.setTextSize(1);
  sparkline(g, 232, 34, 76, 34, ui.gr.gpuLoad, GOOD);

  panel(g, 140, 82, 176, 32, "VRAM");
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%.1f/%.0fG", hw.vu, hw.vt);
  textAt(g, 148, 90, v, TEXT);
  g.setTextSize(1);
  hBar(g, 252, 90, 56, 14, hw.gv, pctColor(hw.gv));

  panel(g, 4, 120, 312, 30, "ТАКТ / ПИТАНИЕ / ГОР.ТОЧКА");
  snprintf(v, sizeof(v), "%d", hw.gclock);
  bigVal(g, 14, 124, v, "MHz", TEXT);
  snprintf(v, sizeof(v), "%d", hw.gtdp);
  bigVal(g, 178, 124, v, "W", TEXT);
  snprintf(v, sizeof(v), "%d", hw.gh);
  bigVal(g, 306, 124, v, "C", tempColor(hw.gh, 85, 95), true);
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
  textAt(g, 14, inkTop(g, 31, 64), v, pctColor(rpct));
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

  panel(g, 4, 108, 312, 42, "ТОП ПО ПАМЯТИ");
  g.setFont(&F_MED);
  for (int i = 0; i < 2; i++) {
    if (ui.st.process.ramNames[i].length() == 0) continue;
    int y = 110 + i * 20;
    snprintf(v, sizeof(v), "%.14s", ui.st.process.ramNames[i].c_str());
    textAt(g, 12, y, v, i == 0 ? TEXT : DIM);
    snprintf(v, sizeof(v), "%d МБ", ui.st.process.ramMb[i]);
    textRight(g, 306, y, v, INFO);
  }
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
    hBar(g, 30, y + 18, 284, 10, pct, pctColor(pct)); /* y+18..y+28 */
  }

  /* disk I/O across the freed bottom band (y148..170) */
  panel(g, 4, 148, 312, 22, "ДИСКОВЫЙ ОБМЕН");
  g.setFont(&F_MED);
  char r1[12], r2[12];
  fmtRate(r1, sizeof(r1), hw.dr);
  fmtRate(r2, sizeof(r2), hw.dw);
  g.fillTriangle(12, 154, 22, 154, 17, 163, INFO);
  snprintf(v, sizeof(v), "%s/s", r1);
  textAt(g, 26, 152, v, INFO);
  g.fillTriangle(172, 163, 182, 163, 177, 154, WARN);
  snprintf(v, sizeof(v), "%s/s", r2);
  textAt(g, 186, 152, v, WARN);
}

void drawFans(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  static const char *names[] = {"CPU", "ПОМПА", "GPU", "КОРПУС"};
  char v[16];

  for (int i = 0; i < NOCT_FAN_COUNT; i++) {
    int x = 10 + i * 78;
    int rpm = hw.fans[i];
    int pct = hw.fan_controls[i];
    int barPct = rpm * 100 / 2200;
    if (barPct > 100) barPct = 100;
    vBar(g, x + 16, 28, 30, 68, barPct, rpm > 0 ? GOOD : PANEL);
    g.setFont(&F_TEXT);
    textCenter(g, x + 31, 100, names[i], DIM);
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%d", rpm);
    textCenter(g, x + 31, 112, v, TEXT);
    g.setFont(&F_MED);
    g.setTextSize(1);
    snprintf(v, sizeof(v), "%d%%", pct);
    textCenter(g, x + 31, 136, v, ORANGE);
    g.setTextSize(1);
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
  /* 5 real sensors; if chipset==case skip the duplicate 6th */
  int count = (hw.ch == hw.mb_chipset) ? 5 : 5;
  (void)count;
  char v[12];
  for (int i = 0; i < 5; i++) {
    int x = 6 + (i % 3) * 104;
    int y = 28 + (i / 3) * 62;
    int w = (i % 3 == 2) ? 100 : 100;
    panel(g, x, y, w, 54, tiles[i].n);
    snprintf(v, sizeof(v), "%d", tiles[i].t);
    g.setFont(&F_HUGE);
    uint16_t c = tempColor(tiles[i].t, tiles[i].warn, tiles[i].crit);
    int vw = g.textWidth(v);
    textAt(g, x + 10, y + 8 - (g.fontHeight() - 32) / 2, v, c);
    g.setFont(&F_MED);
    textAt(g, x + 14 + vw, y + 28, "C", DIM);
  }
  /* 6th cell: chipset-area fan, correctly labelled as RPM */
  int x = 6 + 2 * 104, y = 90;
  panel(g, x, y, 100, 54, "ЧИПСЕТ ФАН");
  snprintf(v, sizeof(v), "%d", hw.cf);
  g.setFont(&F_BIG);
  textAt(g, x + 10, y + 14, v, INFO);
  g.setFont(&F_MED);
  textRight(g, x + 94, y + 30, "RPM", DIM);
}

void drawNet(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40], r[12];

  panel(g, 4, 26, 156, 60, "ВХОДЯЩИЙ");
  fmtRate(r, sizeof(r), hw.nd);
  bigVal(g, 14, 36, r, "Б/с", INFO);
  sparkline(g, 14, 64, 134, 18, ui.gr.netDown, INFO, 1000);

  panel(g, 164, 26, 152, 60, "ИСХОДЯЩИЙ");
  fmtRate(r, sizeof(r), hw.nu);
  bigVal(g, 174, 36, r, "Б/с", GOOD);
  sparkline(g, 174, 64, 132, 18, ui.gr.netUp, GOOD, 200);

  panel(g, 4, 94, 156, 56, "ПИНГ");
  snprintf(v, sizeof(v), "%d", hw.pg);
  bigVal(g, 14, 104, v, "ms", hw.pg > 80 ? WARN : GOOD);
  g.setFont(&F_TEXT);
  textAt(g, 14, 134, "google:443", DIM);

  panel(g, 164, 94, 152, 56, "УСТРОЙСТВО");
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%.9s", ui.st.link.ssid);
  textAt(g, 172, 100, v, TEXT);
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "RSSI %d dBm", ui.st.link.rssi);
  textAt(g, 172, 121, v, DIM);
  g.setFont(&F_MED);
  textAt(g, 172, 129, ui.st.link.tcpConnected ? "сервер: ок" : "сервер: нет",
         ui.st.link.tcpConnected ? GOOD : CRIT);
}

} // namespace scenes
