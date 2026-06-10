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

/* 64 px hero temperature with a small unit */
static void heroTemp(LGFX_Sprite &g, int x, int y, int t, uint16_t c) {
  char v[8];
  snprintf(v, sizeof(v), "%d", t);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  int vw = g.textWidth(v);
  textAt(g, x, y, v, c);
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  textAt(g, x + vw + 4, y + 44, "C", DIM);
  g.setTextSize(1);
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
    g.setFont(&F_TEXT);
    g.setTextSize(2);
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
  g.setFont(&F_TEXT);
  g.setTextSize(2);
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
  textAt(g, 14, 32, v, pctColor(rpct));
  g.setTextSize(1);
  g.setFont(&F_TEXT);
  g.setTextSize(2);
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
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  for (int i = 0; i < 2; i++) {
    if (ui.st.process.ramNames[i].length() == 0) continue;
    int y = 114 + i * 17;
    snprintf(v, sizeof(v), "%.14s", ui.st.process.ramNames[i].c_str());
    textAt(g, 12, y, v, i == 0 ? TEXT : DIM);
    snprintf(v, sizeof(v), "%d МБ", ui.st.process.ramMb[i]);
    textRight(g, 306, y, v, INFO);
  }
  g.setTextSize(1);
}

void drawDisks(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];

  for (int i = 0; i < NOCT_HDD_COUNT; i++) {
    HddEntry &d = hw.hdd[i];
    int y = 25 + i * 26;
    if (d.total_gb < 0.1f) continue;
    int pct = (int)(d.used_gb * 100 / d.total_gb);
    g.setFont(&F_TEXT);
    g.setTextSize(2);
    snprintf(v, sizeof(v), "%s", d.name);
    textAt(g, 8, y + 3, v, ORANGE);
    g.setTextSize(1);
    hBar(g, 30, y + 4, 130, 15, pct, pctColor(pct));
    g.setFont(&F_TEXT);
    g.setTextSize(2);
    if (d.total_gb >= 1000)
      snprintf(v, sizeof(v), "%.1f/%.0fT", d.used_gb / 1000,
               d.total_gb / 1000);
    else
      snprintf(v, sizeof(v), "%.0f/%.0fG", d.used_gb, d.total_gb);
    textAt(g, 168, y + 3, v, TEXT);
    snprintf(v, sizeof(v), "%dC", d.temp);
    textRight(g, 314, y + 3, v, tempColor(d.temp, 45, 55));
    g.setTextSize(1);
  }

  g.setFont(&F_TEXT);
  g.setTextSize(2);
  char r1[12], r2[12];
  fmtRate(r1, sizeof(r1), hw.dr);
  fmtRate(r2, sizeof(r2), hw.dw);
  snprintf(v, sizeof(v), "чтение %s", r1);
  textAt(g, 8, 132, v, INFO);
  snprintf(v, sizeof(v), "запись %s", r2);
  textRight(g, 312, 132, v, WARN);
  g.setTextSize(1);
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
    g.setFont(&F_TEXT);
    g.setTextSize(2);
    snprintf(v, sizeof(v), "%d%%", pct);
    textCenter(g, x + 31, 136, v, ORANGE);
    g.setTextSize(1);
  }
}

void drawMb(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  struct {
    const char *n;
    int t;
    int warn, crit;
  } tiles[] = {
      {"SYS", hw.mb_sys, 50, 60},     {"VSOC", hw.mb_vsoc, 70, 85},
      {"VRM", hw.mb_vrm, 70, 90},     {"ЧИПСЕТ", hw.mb_chipset, 60, 75},
      {"КОРПУС", hw.ch, 45, 55},      {"S1", hw.s1, 45, 55},
      {"S2", hw.s2, 45, 55},          {"ЧИП.ФАН", hw.cf, 9999, 9999},
  };
  char v[12];
  for (int i = 0; i < 8; i++) {
    int x = 6 + (i % 4) * 78;
    int y = 28 + (i / 4) * 60;
    panel(g, x, y, 72, 52, tiles[i].n);
    bool isFan = i == 7;
    snprintf(v, sizeof(v), "%d", tiles[i].t);
    g.setFont(&F_BIG);
    uint16_t c = isFan ? INFO
                       : tempColor(tiles[i].t, tiles[i].warn, tiles[i].crit);
    textCenter(g, x + 36, y + 12, v, c);
    g.setFont(&F_TEXT);
    textCenter(g, x + 36, y + 40, isFan ? "RPM" : "C", DIM);
  }
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
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%.9s", ui.st.link.ssid);
  textAt(g, 172, 100, v, TEXT);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "RSSI %d dBm", ui.st.link.rssi);
  textAt(g, 172, 120, v, DIM);
  g.setTextSize(2);
  textAt(g, 172, 132, ui.st.link.tcpConnected ? "сервер: ок" : "сервер: нет",
         ui.st.link.tcpConnected ? GOOD : CRIT);
  g.setTextSize(1);
}

} // namespace scenes
