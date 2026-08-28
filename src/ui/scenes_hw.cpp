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
/* Centred in the tile, not pinned to its left edge.
 *
 * `x`/`w` are the tile's, and the number plus its unit are measured and
 * placed as one group. Fixed at x+10 the pair sat hard against the left
 * border with a third of the tile empty beside it, and a two-digit reading
 * looked lost in a box sized for three. */
static void heroTemp(LGFX_Sprite &g, int x, int w, int y, int t, uint16_t c) {
  char v[8];
  snprintf(v, sizeof(v), "%d", t);
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  int vw = g.textWidth(v);
  g.setTextSize(1);
  g.setFont(&F_MED);
  int uw = g.textWidth("C");
  int x0 = x + (w - (vw + 4 + uw)) / 2;
  if (x0 < x + 4) x0 = x + 4;
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  textAt(g, x0, inkTop(g, y, 64), v, c);
  g.setTextSize(1);
  g.setFont(&F_MED);
  textAt(g, x0 + vw + 4, y + 44, "C", DIM);
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

  /* ── левая колонка: температура, единственный герой экрана ─────────── */
  panel(g, 4, 26, 130, 92, "температура");
  heroTemp(g, 4, 130, 44, hw.ct, tempColor(hw.ct, 75, 85));
  trendArrow(g, 118, 42, ui.gr.cpuTemp, 8, 1);

  /* ── правая колонка ─────────────────────────────────────────────────── */
  panel(g, 140, 26, 176, 50, "нагрузка");
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", hw.cl);
  textAt(g, 148, 40, v, pctColor(hw.cl));
  g.setTextSize(1);
  trendArrow(g, 222, 48, ui.gr.cpuLoad, 8, 3);
  sparkline(g, 232, 40, 76, 30, ui.gr.cpuLoad, GOOD);

  /* 39 рядов, не 32, и полоса значения начинается ПОД ярлыком.
   *
   * Ярлык теперь внутри карточки и занимает её первые двенадцать рядов, а
   * цифры начинались на четвёртом — «1510» шло прямо сквозь слова «кулер /
   * питание». Тридцати двух рядов на ярлык (12) плюс чернила F_BIG (24) не
   * хватает в принципе; тридцати девяти хватает, и они есть. */
  panel(g, 140, 79, 176, 39, "кулер / питание");
  snprintf(v, sizeof(v), "%d", hw.fans[0]);
  bigVal(g, 148, 91, 26, v, "RPM", TEXT);   /* чернила 92..115 */
  snprintf(v, sizeof(v), "%d", hw.pw);
  bigVal(g, 308, 91, 26, v, "Вт", TEXT, true); /* не "W": читается как "ш" */

  /* ── подвал: два процесса и такт ────────────────────────────────────── */
  /* 50 рядов до 169-го: вторая строка F_MED уходила выносными элементами на
   * нижнюю рамку карточки, и проверка наложений это ловила. */
  panel(g, 4, 120, 312, 50, "топ процессы / такт");
  g.setFont(&F_MED);
  g.setTextSize(1);
  for (int i = 0; i < 2; i++) {
    if (!ui.st.process.cpuNames[i].length()) continue;
    snprintf(v, sizeof(v), "%.13s %d%%", ui.st.process.cpuNames[i].c_str(),
             ui.st.process.cpuPercent[i]);
    textAt(g, 12, 132 + i * 17, v, i == 0 ? TEXT : DIM);
  }
  snprintf(v, sizeof(v), "%.1f", hw.cc / 1000.0f);
  bigVal(g, 306, 130, 24, v, "GHz", INFO, true);
}

void drawGpu(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[32];

  /* Та же сетка, что на ЦПУ: один герой слева, две карточки справа, подвал
   * во всю ширину. Соседние экраны должны читаться как один экран с другими
   * числами, а не как две разные вёрстки. */
  panel(g, 4, 26, 130, 92, "температура");
  heroTemp(g, 4, 130, 44, hw.gt, tempColor(hw.gt, 70, 80));
  trendArrow(g, 118, 42, ui.gr.gpuTemp, 8, 1);

  panel(g, 140, 26, 176, 38, "нагрузка");
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", hw.gl);
  textAt(g, 148, 38, v, pctColor(hw.gl));
  g.setTextSize(1);
  trendArrow(g, 222, 44, ui.gr.gpuLoad, 8, 3);
  sparkline(g, 232, 40, 76, 20, ui.gr.gpuLoad, GOOD);

  /* Такт памяти переехал сюда, к самой памяти. Он стоял в подвале среди
   * тактов ядра и мощности, где к нему нечего было отнести. */
  /* 51 ряд, а не 47, и шаг восемнадцать.
   *
   * Две строки F_MED — это тринадцать рядов чернил плюс четыре выносных
   * каждая: в сорок семь они влезали только вплотную, и хвост косой черты в
   * «2.1/12G» ложился на верх «810 МГц», а «ц» уходила за нижнюю кромку.
   * Пока такт памяти был трёхзначным, это было не видно. */
  /* Два числа в ОДНУ строку, полоса под ними.
   *
   * Двумя строками они помещались только вплотную: хвост косой черты в
   * «2.0/12G» приходил на верх «810 МГц» — ноль рядов просвета. Рядом они
   * читаются лучше и занимают на семнадцать рядов меньше. */
  panel(g, 140, 67, 176, 51, "vram / такт памяти");
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%.1f/%.0fG", hw.vu, hw.vt);
  textAt(g, 148, 81, v, TEXT);
  snprintf(v, sizeof(v), "%d МГц", hw.vclock);
  textRight(g, 308, 81, v, DIM);
  hBar(g, 148, 104, 160, 8, hw.gv, pctColor(hw.gv));

  /* ── подвал: два блока вместо одного на пять чисел ────────────────────
   *
   * Пять показаний в одной карточке оставляли каждому по одиннадцать
   * пикселей высоты. На расстоянии полуметра-метра, с которого этот экран
   * читают, это ниже разрешения глаза — владелец так и написал: «нижние
   * цифры кулер, память и т.д. слишком мелкие, нечитаемые». Теперь их
   * четыре, по два на карточку, и все в F_MED. */
  /* 52 ряда до последнего ряда экрана: ярлык 12 плюс две строки F_MED по
   * семнадцать — это сорок шесть, и на просвет между ними нужны ещё три. */
  panel(g, 4, 120, 154, 52, "такт / питание");
  g.setFont(&F_MED);
  g.setTextSize(1);
  snprintf(v, sizeof(v), "%d МГц", hw.gclock);
  textAt(g, 12, 132, v, TEXT);
  snprintf(v, sizeof(v), "%d Вт", hw.gtdp);   /* не "W": читается как "ш" */
  textAt(g, 12, 152, v, TEXT);

  panel(g, 162, 120, 154, 52, "гор.точка / кулер");
  snprintf(v, sizeof(v), "%d C", hw.gh);
  textAt(g, 170, 132, v, tempColor(hw.gh, 85, 95));
  snprintf(v, sizeof(v), "%d об/мин", hw.gf);
  textAt(g, 170, 152, v, TEXT);
}

void drawRam(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];
  int rpct = hw.ra > 0.1f ? (int)(hw.ru * 100 / hw.ra) : 0;

  /* 80, не 74: ярлык внутри карточки занимает 12 рядов, а герой F_HUGE в
   * двойном размере — 64 чернил. Семьдесят четыре их не вмещают, и число
   * пробивало нижнюю кромку. Бюджет полосы 26..170 сходится: 80 + 4 + 62. */
  /* 84 рядa, не 80. Ярлык занимает 12, герой — 64, и на выносные элементы
   * единицы измерения оставалось меньше, чем они занимают. Заодно ярлык
   * перестал задевать верх цифр: на восьмидесяти между ними было минус два
   * ряда. */
  panel(g, 4, 26, 312, 84, "оперативка");
  g.setFont(&F_HUGE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%.1f", hw.ru);
  int vw = g.textWidth(v);
  /* Считается ЗДЕСЬ, пока выбран F_HUGE.
   *
   * inkTop() смотрит на g.fontHeight(), а вызывался он ниже — уже после
   * setFont(&F_MED). Смещение считалось для строки в двадцать рядов вместо
   * девяноста четырёх, единица уезжала на пятнадцать рядов вниз и ложилась
   * на нижнюю кромку карточки. На бумаге арифметика выглядела правильной. */
  /* 43, а не 40: чернила ярлыка кончаются на 37-м ряду, и на сорока между
   * ним и верхом цифр оставался один ряд. */
  /* 41: чернила ярлыка кончаются на 35-м ряду, чернила героя начинаются
   * на 40-м — четыре ряда просвета. Ниже 41 не уйти, единица упрётся в
   * нижнюю кромку; выше 41 не уйти, герой сядет на ярлык. */
  const int ramHeroY = inkTop(g, 41, 64);
  /* 37, не 40. Ярлык занимает 12 рядов, герой — 64, карточка — 80: на
   * выносные единицы измерения оставалось четыре ряда, а нужно ровно
   * четыре, и косая черта в «/32ГБ» ложилась на нижнюю кромку. */
  textAt(g, 14, ramHeroY, v, pctColor(rpct));
  g.setTextSize(1);
  g.setFont(&F_MED);
  /* On the hero's own baseline, derived from where the hero actually landed.
   * At a fixed y=80 the two sat at different heights whenever the label band
   * moved the hero, which is the "чуть долизать" the owner could see and
   * could not name. */
  {
    const int base = ramHeroY + (INK_HUGE.top + INK_HUGE.height) * 2;
    /* Просто «ГБ». Косая черта — выносной элемент, и она свисала на
     * нижнюю кромку карточки; а объём и так назван строкой ниже
     * («свободно 21.0 ГБ из 32»), так что «/32» здесь ничего не добавлял. */
    snprintf(v, sizeof(v), "ГБ");
    (void)hw.ra;
    textAt(g, 22 + vw, base - INK_MED.top - INK_MED.height, v, DIM);
  }
  g.setTextSize(1);
  g.setFont(&F_VALUE);
  g.setTextSize(2);
  snprintf(v, sizeof(v), "%d%%", rpct);
  textRight(g, 306, 42, v, pctColor(rpct));
  g.setTextSize(1);
  hBar(g, 230, 76, 76, 16, rpct, pctColor(rpct));

  /* grown into the freed bottom band: top-2 processes + free memory */
  /* Свободная память переехала на строку ЯРЛЫКА.
   *
   * Три строки по семнадцать рядов в пятидесяти восьми оставляли последней
   * ноль пикселей просвета: чернила «claude.exe» кончались на 156-м ряду,
   * «свободно» начиналось на 157-м. Проверка наложений это пропускает —
   * пересечения нет, — а глаз видит кашу, о чём владелец и написал.
   *
   * Ярлык всё равно назывался «топ по памяти / свободно» и обещал обе вещи;
   * теперь он их обе и показывает, а двум строкам процессов достаётся весь
   * остаток карточки. */
  panel(g, 4, 112, 312, 58, "топ по памяти");
  {
    float freeTop = hw.ra - hw.ru;
    if (freeTop < 0) freeTop = 0;
    int fx = 306;
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    snprintf(v, sizeof(v), "ГБ из %.0f", hw.ra);
    int wUnit = g.textWidth(v);
    textAt(g, fx - wUnit, 114, v, DIM);
    fx -= wUnit + 6;
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%.1f", freeTop);
    int wNum = g.textWidth(v);
    textAt(g, fx - wNum, 113, v, GOOD);
    fx -= wNum + 6;
    g.setFont(&F_TEXT);
    textAt(g, fx - g.textWidth("свободно"), 114, "свободно", DIM);
  }
  g.setFont(&F_MED);
  for (int i = 0; i < 2; i++) {
    if (ui.st.process.ramNames[i].length() == 0) continue;
    /* Шаг 21, а не 17: чернила F_MED — тринадцать рядов, выносные элементы
     * ещё четыре, и на семнадцати между строками остаётся ровно ноль. */
    int y = 128 + i * 21;   /* чернила 131..143 и 152..164 */
    snprintf(v, sizeof(v), "%.14s", ui.st.process.ramNames[i].c_str());
    textAt(g, 12, y, v, i == 0 ? TEXT : DIM);
    snprintf(v, sizeof(v), "%d МБ", ui.st.process.ramMb[i]);
    textRight(g, 306, y, v, INFO);
  }

  /* The only place the free figure appears, and it was set in the smallest
   * face on the screen with 200 px of empty tile to its right. */
  /* "ГБ" cannot be set in F_VALUE — Latin subset, hollow boxes. Digits in
   * F_VALUE, Cyrillic in F_TEXT, and the two sit on one baseline. */
}

void drawDisks(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40];

  /* One card for the four drives, one for the throughput. Four separate
   * cards would spend a label row per drive on a word the letter already
   * says; one card with four rows spends it once. */
  panel(g, 4, 26, 312, 116, "диски");
  static const int rowY[NOCT_HDD_COUNT] = {38, 63, 88, 113};
  for (int i = 0; i < NOCT_HDD_COUNT; i++) {
    HddEntry &d = hw.hdd[i];
    if (d.total_gb < 0.1f) continue;
    int y = rowY[i];
    int pct = (int)(d.used_gb * 100 / d.total_gb);
    g.setFont(&F_MED);
    snprintf(v, sizeof(v), "%s", d.name);
    textAt(g, 12, y, v, ORANGE);
    if (d.total_gb >= 1000)
      snprintf(v, sizeof(v), "%.2f/%.2fT", d.used_gb / 1000, d.total_gb / 1000);
    else
      snprintf(v, sizeof(v), "%.0f/%.0fG", d.used_gb, d.total_gb);
    textAt(g, 148, y, v, TEXT);
    snprintf(v, sizeof(v), "%dC", d.temp);
    textRight(g, 308, y, v, tempColor(d.temp, 45, 55));
    /* Under the row's ink, not under its cursor: the capacity string carries
     * a slash, which hangs below the baseline, and a bar placed by the cursor
     * cut its tail off. */
    /* +20, не +18. Строка ёмкости содержит косую черту, а она свисает под
     * базовую линию: на восемнадцати полоса срезала ей хвост на два ряда,
     * на всех четырёх дисках сразу. */
    hBar(g, 34, y + 20, 274, 7, pct, pctColor(pct));
  }

  /* Throughput in its own card. It used to be a rule with numbers hanging
   * off it — the one thing on the screen that was not a card, and the owner
   * read exactly that as "цифры налазят на текст". */
  {
    Rect c = panelM(g, 4, 146, 312, 24);
    char r1[12], r2[12];
    fmtRateMb(r1, sizeof(r1), hw.dr);
    fmtRateMb(r2, sizeof(r2), hw.dw);
    const int iy = inkY(INK_MED, c.y, c.h);
    g.setFont(&F_MED);
    g.fillTriangle(c.x, c.y + 4, c.x + 10, c.y + 4, c.x + 5, c.y + 12, INFO);
    textAt(g, c.x + 16, iy, r1, INFO);
    g.fillTriangle(c.x + 156, c.y + 12, c.x + 166, c.y + 12, c.x + 161,
                   c.y + 4, WARN);
    textAt(g, c.x + 172, iy, r2, WARN);
    g.setFont(&F_TEXT);
    textRight(g, c.x + c.w, iy + 5, "МБ/с", DIM);
  }
}

void drawFans(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  static const char *names[] = {"CPU", "ПОМПА", "GPU", "КОРПУС"};
  char v[40];

  int sum = 0, mx = 0;
  for (int i = 0; i < NOCT_FAN_COUNT; i++) {
    /* A card per fan. The extremes then have somewhere to be: inside their
     * own fan's card, beside its own bar — "минимум и максимум надо писать
     * справа у каждого графика", which is where they now are. */
    const int x = 4 + i * 78;
    panel(g, x, 26, 74, 120, names[i]);
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
    /* Extremes since BOOT, per fan. Not since forever: a reboot is when the
     * machine's workload changed, and carrying yesterday's peak past it would
     * make the number describe a session nobody remembers. Zeroes are skipped
     * — a fan reading 0 is a fan not reporting, and it would pin every
     * minimum to zero. */
    static int fanMin[NOCT_FAN_COUNT] = {0};
    static int fanMax[NOCT_FAN_COUNT] = {0};
    if (rpm > 0) {
      if (!fanMin[i] || rpm < fanMin[i]) fanMin[i] = rpm;
      if (rpm > fanMax[i]) fanMax[i] = rpm;
    }
    vBar(g, x + 6, 42, 18, 52, barPct, rpm > 0 ? GOOD : PANEL);
    g.setFont(&F_TEXT);
    if (fanMax[i]) {
      snprintf(v, sizeof(v), "%d", fanMax[i]);
      textAt(g, x + 28, 42, v, DIM);
      textAt(g, x + 28, 56, "макс", DIM);
      snprintf(v, sizeof(v), "%d", fanMin[i]);
      textAt(g, x + 28, 70, v, DIM);
      textAt(g, x + 28, 84, "мин", DIM);
    }
    /* The duty is the quantity the bar beside it draws; the rpm is the
     * reading. Both above the eye's threshold at a metre, which the old
     * eight-pixel name row was not. */
    g.setFont(&F_VALUE);
    snprintf(v, sizeof(v), "%d%%", pct);
    textCenter(g, x + 37, 98, v, ORANGE);
    g.setFont(&F_BIG);
    snprintf(v, sizeof(v), "%d", rpm);
    textCenter(g, x + 37, 112, v, TEXT);
  }

  /* The summary is a card like everything else — it was a rule with text
   * under it, the one block on the screen in a different idiom. */
  if (uiOn(UI_STRIPS)) {
    Rect c = panelM(g, 4, 150, 312, 21);
    g.setFont(&F_TEXT);
    g.setTextSize(1);
    char sum2[64];
    snprintf(sum2, sizeof(sum2), "среднее %d%%      максимум %d%%",
             sum / NOCT_FAN_COUNT, mx);
    textCenter(g, NOCT_W / 2, inkY(INK_TEXT, c.y, c.h), sum2, ORANGE);
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

  /* One placement rule for all six cells, measured rather than counted.
   *
   * Every cell used to put its number at a fixed x+12 with the degree mark
   * hung off the number's width, so a 3-digit reading sat where a 2-digit one
   * did not and neither was centred — the owner's "цифры везде скачут и не по
   * центру клеток" is a description of exactly that arithmetic. Centring the
   * number-and-mark PAIR makes a cell look the same whatever it reads. */
  for (int i = 0; i < 6; i++) {
    const int x = 6 + (i % 3) * 104;
    const int y = 26 + (i / 3) * 70; /* ряды 26..91 и 96..161 */
    const bool power = i == 5;
    panel(g, x, y, 100, 66, power ? "питание" : tiles[i].n);
    snprintf(v, sizeof(v), "%d", power ? hw.pw : tiles[i].t);
    uint16_t c = power ? ACCENT
                       : tempColor(tiles[i].t, tiles[i].warn, tiles[i].crit);
    g.setFont(&F_HUGE);
    g.setTextSize(1);
    const int vw = g.textWidth(v);
    /* The mark: a 9 px ring for a degree, or the word for watts. */
    int mw = 9;
    if (power) {
      g.setFont(&F_MED);
      mw = g.textWidth("Вт");
      g.setFont(&F_HUGE);
    }
    const int x0 = x + (100 - (vw + 4 + mw)) / 2;
    const int ty = inkY(INK_HUGE, y + PANEL_LABEL_H, 66 - PANEL_LABEL_H);
    textAt(g, x0, ty, v, c);
    const int inkTopY = ty + INK_HUGE.top;
    if (power) {
      g.setFont(&F_MED);
      /* On the number's baseline, so the pair reads as one reading. */
      textAt(g, x0 + vw + 4,
             inkTopY + INK_HUGE.height - INK_MED.top - INK_MED.height, "Вт",
             DIM);
    } else {
      /* The ring sits at the digits' TOP, where a degree mark belongs. */
      const int cy = inkTopY + 5;
      g.drawCircle(x0 + vw + 8, cy, 4, DIM);
      g.drawCircle(x0 + vw + 8, cy, 3, DIM);
    }
  }
}

void drawNet(UiCtx &ui) {
  if (!gate(ui)) return;
  LGFX_Sprite &g = ui.g;
  HardwareData &hw = ui.st.hw;
  char v[40], r[12];

  panel(g, 4, 26, 156, 60, "входящий");
  fmtRate(r, sizeof(r), hw.nd);
  /* Плитка 26..85. Значению нужно 24 ряда чернил, графику 18 — вместе с
   * зазором это ровно то, что есть, но полоса в 50 рядов под значение
   * опускала его чернила на шесть рядов в график. */
  /* 32: заливка вкладки ярлыка кончается на y+5, то есть на 31. */
  bigVal(g, 14, 41, 24, r, "Б/с", INFO);
  sparkline(g, 14, 68, 134, 12, ui.gr.netDown, INFO, 1000);

  panel(g, 164, 26, 152, 60, "исходящий");
  fmtRate(r, sizeof(r), hw.nu);
  bigVal(g, 174, 41, 24, r, "Б/с", GOOD);
  sparkline(g, 174, 68, 132, 12, ui.gr.netUp, GOOD, 200);

  /* bottom panels grown into the freed band (y94..168), spacing fixed so the
   * RSSI line and the server line no longer overlap */
  panel(g, 4, 94, 156, 74, "пинг");
  snprintf(v, sizeof(v), "%d", hw.pg);
  bigVal(g, 14, 101, 60, v, "ms", hw.pg > 80 ? WARN : GOOD); /* 94..167 */
  g.setFont(&F_MED);
  textAt(g, 14, 142, "google:443", DIM);

  panel(g, 164, 94, 152, 74, "устройство");
  g.setFont(&F_MED);
  g.setTextSize(1);
  clipW(g, ui.st.link.ssid, v, sizeof(v), 140); /* codepoint-safe (no mid-glyph cut) */
  textAt(g, 172, 108, v, TEXT);          /* под ярлыком карточки */
  g.setFont(&F_TEXT);
  snprintf(v, sizeof(v), "RSSI %d dBm", ui.st.link.rssi);
  textAt(g, 172, 126, v, DIM);           /* y126..139 */
  g.setFont(&F_MED);
  textAt(g, 172, 146, ui.st.link.tcpConnected ? "сервер: ок" : "сервер: нет",
         ui.st.link.tcpConnected ? GOOD : CRIT); /* y146..166 */
}

} // namespace scenes
