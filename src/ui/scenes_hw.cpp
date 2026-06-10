/* Nocturne C6 — hardware scenes: CPU, GPU, RAM, DISKS, FANS, MB, NET. */
#include "core/config.h"
#include "ui/Scenes.h"

using namespace theme;
using namespace widgets;

namespace scenes {

static bool gate(UiCtx &ui) {
  if (ui.st.link.signalLost && !ui.st.hw.cc) {
    noSignal(ui);
    return false;
  }
  return true;
}

static void bigTemp(LGFX_Sprite &g, int x, int y, int t, uint16_t c) {
  char v[8];
  snprintf(v, sizeof(v), "%d", t);
  g.setFont(&F_HUGE);
  int vw = g.textWidth(v);
  textAt(g, x, y, v, c);
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  textAt(g, x + vw + 4, y + 14, "C", DIM);
  g.setTextSize(1);
}

void drawCpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
  bigTemp(g, 16, 42, hw.ct, tempColor(hw.ct, 75, 85));
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "%dW  %dMHz", hw.pw, hw.cc);
  textAt(g, 14, 94, v, DIM);

  panel(g, 140, 26, 176, 50, "НАГРУЗКА");
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%d%%", hw.cl);
  textAt(g, 148, 38, v, pctColor(hw.cl));
  sparkline(g, 196, 34, 112, 34, ui.gr.cpuLoad, GOOD);

  panel(g, 140, 82, 176, 32, "КУЛЕР");
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "%d RPM", hw.fans[0]);
  textAt(g, 148, 92, v, TEXT);
  snprintf(v, sizeof(v), "помпа %d", hw.fans[1]);
  textRight(g, 308, 92, v, DIM);

  /* top processes */
  panel(g, 4, 120, 312, 30, "ТОП ПРОЦЕССЫ");
  g.setFont(&F_TEXT);
  int px = 12;
  for (int i = 0; i < 3; i++) {
    if (ui.st.process.cpuNames[i].length() == 0) continue;
    snprintf(v, sizeof(v), "%s %d%%", ui.st.process.cpuNames[i].c_str(),
             ui.st.process.cpuPercent[i]);
    textAt(g, px, 132, v, i == 0 ? TEXT : DIM);
    px += g.textWidth(v) + 14;
    if (px > 280) break;
  }
}

void drawGpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  panel(g, 4, 26, 130, 88, "ТЕМПЕРАТУРА");
  bigTemp(g, 16, 42, hw.gt, tempColor(hw.gt, 70, 80));
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "hotspot %dC", hw.gh);
  textAt(g, 14, 94, v, tempColor(hw.gh, 85, 95));

  panel(g, 140, 26, 176, 50, "НАГРУЗКА");
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%d%%", hw.gl);
  textAt(g, 148, 38, v, pctColor(hw.gl));
  sparkline(g, 196, 34, 112, 34, ui.gr.gpuLoad, GOOD);

  panel(g, 140, 82, 176, 32, "VRAM");
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "%.1f / %.0fG", hw.vu, hw.vt);
  textAt(g, 148, 92, v, TEXT);
  hBar(g, 230, 90, 78, 10, hw.gv, pctColor(hw.gv));

  panel(g, 4, 120, 312, 30, "ЧАСТОТЫ / ПИТАНИЕ");
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "ядро %dMHz", hw.gclock);
  textAt(g, 12, 132, v, DIM);
  snprintf(v, sizeof(v), "vram %dMHz", hw.vclock);
  textAt(g, 110, 132, v, DIM);
  snprintf(v, sizeof(v), "%dW", hw.gtdp);
  textAt(g, 210, 132, v, TEXT);
  snprintf(v, sizeof(v), "%d RPM", hw.gf);
  textRight(g, 308, 132, v, DIM);
}

void drawRam(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];
  int rpct = hw.ra > 0.1f ? (int)(hw.ru * 100 / hw.ra) : 0;

  panel(g, 4, 26, 312, 62, "ОПЕРАТИВКА");
  g.setFont(&F_HUGE);
  snprintf(v, sizeof(v), "%.1f", hw.ru);
  int vw = g.textWidth(v);
  textAt(g, 16, 38, v, pctColor(rpct));
  g.setFont(&F_TEXT);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "/ %.0f ГБ", hw.ra);
  textAt(g, 24 + vw, 52, v, DIM);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%d%%", rpct);
  g.setFont(&F_VALUE);
  textRight(g, 304, 36, v, TEXT);
  hBar(g, 12, 72, 296, 10, rpct, pctColor(rpct));

  panel(g, 4, 96, 312, 54, "ТОП ПО ПАМЯТИ");
  g.setFont(&F_TEXT);
  for (int i = 0; i < 2; i++) {
    if (ui.st.process.ramNames[i].length() == 0) continue;
    int y = 108 + i * 18;
    textAt(g, 14, y, ui.st.process.ramNames[i].c_str(), TEXT);
    snprintf(v, sizeof(v), "%d МБ", ui.st.process.ramMb[i]);
    textRight(g, 240, y, v, DIM);
    int pmax = ui.st.process.ramMb[0] > 0 ? ui.st.process.ramMb[0] : 1;
    hBar(g, 248, y + 1, 60, 8, ui.st.process.ramMb[i] * 100 / pmax, INFO);
  }
}

void drawDisks(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];

  for (int i = 0; i < NOCT_HDD_COUNT; i++) {
    HddEntry &d = hw.hdd[i];
    int y = 26 + i * 26;
    if (d.total_gb < 0.1f) continue;
    int pct = (int)(d.used_gb * 100 / d.total_gb);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%s:", d.name);
    textAt(g, 8, y + 3, v, ORANGE);
    hBar(g, 34, y + 4, 156, 13, pct, pctColor(pct));
    if (d.total_gb >= 1000)
      snprintf(v, sizeof(v), "%.2f/%.2fT", d.used_gb / 1000,
               d.total_gb / 1000);
    else
      snprintf(v, sizeof(v), "%.0f/%.0fG", d.used_gb, d.total_gb);
    textAt(g, 198, y + 4, v, TEXT);
    snprintf(v, sizeof(v), "%dC", d.temp);
    textRight(g, 314, y + 4, v, tempColor(d.temp, 45, 55));
  }

  panel(g, 4, 130, 312, 20);
  g.setFont(&F_TEXT);
  char r1[12], r2[12];
  fmtRate(r1, sizeof(r1), hw.dr);
  fmtRate(r2, sizeof(r2), hw.dw);
  snprintf(v, sizeof(v), "чтение %s/s", r1);
  textAt(g, 14, 135, v, INFO);
  snprintf(v, sizeof(v), "запись %s/s", r2);
  textRight(g, 306, 135, v, WARN);
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
    /* normalize bar by typical max 2200 rpm */
    int barPct = rpm * 100 / 2200;
    if (barPct > 100) barPct = 100;
    vBar(g, x + 18, 30, 26, 76, barPct, rpm > 0 ? GOOD : PANEL);
    g.setFont(&F_TEXT);
    textCenter(g, x + 31, 110, names[i], DIM);
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d", rpm);
    textCenter(g, x + 31, 121, v, TEXT);
    g.setFont(&F_TEXT);
    snprintf(v, sizeof(v), "%d%%", pct);
    textCenter(g, x + 31, 138, v, ORANGE_DIM);
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
    g.setFont(&F_VALUE);
    bool isFan = i == 7;
    snprintf(v, sizeof(v), "%d", tiles[i].t);
    uint16_t c = isFan ? INFO : tempColor(tiles[i].t, tiles[i].warn,
                                          tiles[i].crit);
    textCenter(g, x + 36, y + 16, v, c);
    g.setFont(&F_SMALL);
    textCenter(g, x + 36, y + 38, isFan ? "RPM" : "C", DIM);
  }
}

void drawNet(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40], r[12];

  panel(g, 4, 26, 156, 60, "ВХОДЯЩИЙ");
  fmtRate(r, sizeof(r), hw.nd);
  g.setFont(&F_VALUE);
  textAt(g, 14, 40, r, INFO);
  g.setFont(&F_SMALL);
  textAt(g, 14, 60, "КБ/с", DIM);
  sparkline(g, 70, 56, 84, 24, ui.gr.netDown, INFO, 1000);

  panel(g, 164, 26, 152, 60, "ИСХОДЯЩИЙ");
  fmtRate(r, sizeof(r), hw.nu);
  g.setFont(&F_VALUE);
  textAt(g, 174, 40, r, GOOD);
  g.setFont(&F_SMALL);
  textAt(g, 174, 60, "КБ/с", DIM);
  sparkline(g, 228, 56, 82, 24, ui.gr.netUp, GOOD, 200);

  panel(g, 4, 94, 156, 56, "ПИНГ");
  g.setFont(&F_VALUE);
  snprintf(v, sizeof(v), "%d ms", hw.pg);
  textAt(g, 14, 110, v, hw.pg > 80 ? WARN : GOOD);
  g.setFont(&F_SMALL);
  textAt(g, 14, 134, "google:443", DIM);

  panel(g, 164, 94, 152, 56, "УСТРОЙСТВО");
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "%s", ui.st.link.ssid);
  textAt(g, 174, 104, v, TEXT);
  snprintf(v, sizeof(v), "RSSI %d dBm", ui.st.link.rssi);
  textAt(g, 174, 118, v, DIM);
  textAt(g, 174, 132,
         ui.st.link.tcpConnected ? "сервер: онлайн" : "сервер: нет",
         ui.st.link.tcpConnected ? GOOD : CRIT);
}

} // namespace scenes
