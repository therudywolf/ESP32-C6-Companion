#include "ui/SceneManager.h"

#include "pet/wolf_sprites.h"

using namespace theme;
using namespace widgets;

void SceneManager::begin(const Deps &deps) {
  d_ = deps;
  lastInput_ = millis();
}

void SceneManager::toast(const String &msg) {
  toast_ = msg;
  toastUntil_ = millis() + NOCT_TOAST_MS;
}

bool SceneManager::alertActive(UiCtx &ui) const {
  return ui.st.alertActive && (long)(ui.now - alertSnoozeUntil_) >= 0;
}

void SceneManager::gotoScene(int s, UiCtx &ui) {
  if (s == scene_) return;
  scene_ = (s % SCENE_COUNT + SCENE_COUNT) % SCENE_COUNT;
  transStart_ = ui.now;
  d_.tcp->sendScreen(scene_);
  if (scene_ == SCENE_CLAUDE) d_.tcp->sendCmd("claude");
  if (scene_ == SCENE_FOREST || scene_ == SCENE_SERVICES)
    d_.tcp->sendCmd("status");
}

int SceneManager::denActionSel(UiCtx &ui) const {
  /* the highlighted chip follows the wolf's need */
  if (!ui.pet.isAlive()) return 0; /* feed = revive */
  if (ui.pet.hunger() < 40) return 0;
  if (ui.pet.happy() < 40) return 1;
  return 2; /* talk */
}

void SceneManager::handleInput(ButtonEvent ev, UiCtx &ui) {
  if (ev == EV_NONE) return;
  lastInput_ = ui.now;
  if (dimmed_) { /* first press only wakes the screen */
    dimmed_ = false;
    d_.disp->setBrightness(ui.st.settings.brightness);
    return;
  }

  /* alert takeover: any LONG press snoozes */
  if (alertActive(ui) && ev == EV_LONG) {
    alertSnoozeUntil_ = ui.now + 60000UL;
    toast("тихо на 60 сек");
    return;
  }

  if (sysInfo_) {
    sysInfo_ = false;
    return;
  }

  if (menuOpen_) {
    const int kMenuItems = 9;
    switch (ev) {
    case EV_SHORT:
      menuSel_ = (menuSel_ + 1) % kMenuItems;
      break;
    case EV_LONG:
      menuAction(ui);
      break;
    case EV_DOUBLE:
    case EV_TRIPLE:
      menuOpen_ = false;
      break;
    default:
      break;
    }
    return;
  }

  /* DEN action submenu */
  if (scene_ == SCENE_DEN && denActionMode_) {
    denModeAt_ = ui.now;
    switch (ev) {
    case EV_SHORT:
      denSel_ = (denSel_ + 1) % 3;
      break;
    case EV_LONG: {
      ui.pet.doAction(denSel_);
      ui.brain.onAction(denSel_);
      d_.led->setMode(StatusLed::BLIP_OK);
      toast(denSel_ == 0 ? "ням-ням!" : denSel_ == 1 ? "поиграли!" : "...");
      denActionMode_ = false;
      break;
    }
    case EV_DOUBLE:
    case EV_TRIPLE:
      denActionMode_ = false;
      break;
    default:
      break;
    }
    return;
  }

  switch (ev) {
  case EV_SHORT: {
    /* FORZA is an app, not a ring member: cycle DEN..EVENTS */
    int next = scene_ + 1;
    if (next >= SCENE_FORZA) next = SCENE_DEN;
    gotoScene(next, ui);
    break;
  }
  case EV_DOUBLE:
    menuOpen_ = true;
    menuSel_ = 0;
    break;
  case EV_TRIPLE:
    gotoScene(SCENE_DEN, ui);
    break;
  case EV_LONG: {
    if (scene_ == SCENE_DEN) {
      denActionMode_ = true; /* enter the wolf's submenu */
      denSel_ = denActionSel(ui);
      denModeAt_ = ui.now;
    } else if (scene_ == SCENE_CLAUDE) {
      d_.tcp->sendCmd("claude");
      toast("обновляю...");
    } else if (scene_ == SCENE_FOREST || scene_ == SCENE_SERVICES) {
      d_.tcp->sendCmd("status");
      toast("обновляю...");
    }
    break;
  }
  default:
    break;
  }
}

void SceneManager::menuAction(UiCtx &ui) {
  Settings &s = ui.st.settings;
  switch (menuSel_) {
  case 0: { /* carousel */
    if (!s.carouselEnabled) {
      s.carouselEnabled = true;
      s.carouselIntervalSec = 5;
    } else if (s.carouselIntervalSec == 5) {
      s.carouselIntervalSec = 10;
    } else if (s.carouselIntervalSec == 10) {
      s.carouselIntervalSec = 15;
    } else {
      s.carouselEnabled = false;
    }
    break;
  }
  case 1: /* brightness */
    s.brightness += 51;
    if (s.brightness > 255) s.brightness = 51;
    d_.disp->setBrightness(s.brightness);
    break;
  case 2: /* LED */
    s.ledEnabled = !s.ledEnabled;
    d_.led->setEnabled(s.ledEnabled);
    break;
  case 3: /* wolf LLM */
    s.petLlm = !s.petLlm;
    break;
  case 4: /* wifi lock */
    s.netSel++;
    if (s.netSel >= d_.wifiCount) s.netSel = -1;
    d_.wifi->setForced(s.netSel);
    break;
  case 5: /* flip display 180 */
    s.flipped = !s.flipped;
    d_.disp->setFlipped(s.flipped);
    break;
  case 6: /* Forza HUD (manual open — it also auto-enters on telemetry) */
    menuOpen_ = false;
    gotoScene(SCENE_FORZA, ui);
    break;
  case 7: /* sysinfo */
    sysInfo_ = true;
    menuOpen_ = false;
    break;
  case 8: /* close */
    menuOpen_ = false;
    break;
  }
  settings::save(s);
}

void SceneManager::drawMenu(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  Settings &s = ui.st.settings;
  /* full content band — no overlaying scene leftovers */
  g.fillRect(0, NOCT_CONTENT_TOP, NOCT_W, NOCT_H - NOCT_CONTENT_TOP, BG);
  g.drawFastHLine(0, NOCT_CONTENT_TOP, NOCT_W, ORANGE);

  char val[9][20];
  if (s.carouselEnabled)
    snprintf(val[0], 20, "%dс", s.carouselIntervalSec);
  else
    snprintf(val[0], 20, "выкл");
  snprintf(val[1], 20, "%d%%", s.brightness * 100 / 255);
  snprintf(val[2], 20, "%s", s.ledEnabled ? "вкл" : "выкл");
  snprintf(val[3], 20, "%s", s.petLlm ? "вкл" : "выкл");
  if (s.netSel < 0)
    snprintf(val[4], 20, "авто");
  else
    snprintf(val[4], 20, "%.10s", d_.wifiNames[s.netSel]);
  snprintf(val[5], 20, "%s", s.flipped ? "180" : "0");
  val[6][0] = '\0';
  val[7][0] = '\0';
  val[8][0] = '\0';

  static const char *names[] = {"Карусель",     "Яркость",
                                "LED",          "Волк LLM",
                                "WiFi",         "Переворот",
                                "Forza HUD",    "Инфо системы",
                                "Закрыть"};
  /* 6 visible rows, 22 px tall — no glyph overlap; list scrolls */
  const int kRows = 9, kVisible = 6, rowH = 22;
  int scroll = menuSel_ - (kVisible - 1);
  if (scroll < 0) scroll = 0;
  g.setFont(&F_MED);
  g.setTextSize(1);
  for (int i = scroll; i < kRows && i < scroll + kVisible; i++) {
    int y = NOCT_CONTENT_TOP + 6 + (i - scroll) * rowH;
    bool sel = i == menuSel_;
    if (sel) g.fillRect(6, y - 2, NOCT_W - 18, rowH - 2, ORANGE);
    textAt(g, 14, y, names[i], sel ? BG : TEXT);
    if (val[i][0]) textRight(g, NOCT_W - 20, y, val[i], sel ? BG : DIM);
  }
  g.setTextSize(1);
  /* scrollbar */
  int sbH = (NOCT_H - NOCT_CONTENT_TOP - 8) * kVisible / kRows;
  int sbY = NOCT_CONTENT_TOP + 4 +
            (NOCT_H - NOCT_CONTENT_TOP - 8 - sbH) * scroll / (kRows - kVisible);
  g.fillRect(NOCT_W - 6, NOCT_CONTENT_TOP + 4, 3,
             NOCT_H - NOCT_CONTENT_TOP - 8, PANEL);
  g.fillRect(NOCT_W - 6, sbY, 3, sbH, ORANGE);
}

void SceneManager::draw(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  Settings &s = ui.st.settings;
  g.fillSprite(BG);

  /* Forza auto-enter/exit: telemetry hijacks the screen like a real dash */
  if (ui.forzaLive && scene_ != SCENE_FORZA && !forzaLatched_) {
    forzaLatched_ = true;
    preForzaScene_ = scene_;
    gotoScene(SCENE_FORZA, ui);
    toast("FORZA!");
  }
  if (!ui.forzaLive && forzaLatched_) {
    forzaLatched_ = false;
    if (scene_ == SCENE_FORZA && preForzaScene_ >= 0)
      gotoScene(preForzaScene_, ui);
    preForzaScene_ = -1;
  }

  /* carousel — never while the Forza HUD is open */
  if (s.carouselEnabled && !menuOpen_ && !sysInfo_ && !alertActive(ui) &&
      scene_ != SCENE_FORZA &&
      ui.now - lastInput_ > 5000 &&
      ui.now - lastCarousel_ > (unsigned long)s.carouselIntervalSec * 1000UL) {
    lastCarousel_ = ui.now;
    int next = scene_ + 1;
    if (next >= SCENE_FORZA) next = SCENE_DASH; /* ring sans DEN/FORZA */
    gotoScene(next, ui);
  }

  /* screen dim */
  if (s.displayTimeoutSec > 0 && !dimmed_ && !ui.forzaLive &&
      ui.now - lastInput_ > (unsigned long)s.displayTimeoutSec * 1000UL &&
      !alertActive(ui)) {
    dimmed_ = true;
    d_.disp->setBrightness(15);
  }
  if (dimmed_ && alertActive(ui)) {
    dimmed_ = false;
    d_.disp->setBrightness(s.brightness);
  }

  /* alert takeover hijacks the scene — but NEVER the Forza HUD */
  int effScene = scene_;
  if (alertActive(ui) && scene_ != SCENE_FORZA) {
    if (preAlertScene_ < 0) preAlertScene_ = scene_;
    effScene = ui.st.alertTargetScene;
  } else {
    preAlertScene_ = -1;
  }

  /* DEN submenu timeout */
  if (denActionMode_ && ui.now - denModeAt_ > 7000) denActionMode_ = false;

  /* content */
  switch (effScene) {
  case SCENE_DEN:
    scenes::drawDen(ui, denActionMode_ ? denSel_ : denActionSel(ui),
                    denActionMode_);
    break;
  case SCENE_DASH: scenes::drawDash(ui); break;
  case SCENE_CPU: scenes::drawCpu(ui); break;
  case SCENE_GPU: scenes::drawGpu(ui); break;
  case SCENE_RAM: scenes::drawRam(ui); break;
  case SCENE_DISKS: scenes::drawDisks(ui); break;
  case SCENE_FANS: scenes::drawFans(ui); break;
  case SCENE_MB: scenes::drawMb(ui); break;
  case SCENE_NET: scenes::drawNet(ui); break;
  case SCENE_MEDIA: scenes::drawMedia(ui); break;
  case SCENE_WEATHER: scenes::drawWeather(ui); break;
  case SCENE_CLAUDE: scenes::drawClaude(ui); break;
  case SCENE_FOREST: scenes::drawForest(ui); break;
  case SCENE_SERVICES: scenes::drawServices(ui); break;
  case SCENE_EVENTS: scenes::drawEvents(ui); break;
  case SCENE_FORZA: scenes::drawForza(ui); break;
  }

  /* chrome — the Forza HUD owns the whole screen, no bars */
  if (effScene != SCENE_FORZA) {
    widgets::statusBar(ui, scenes::title(effScene));
    if (denActionMode_ && effScene == SCENE_DEN)
      widgets::footer(ui, "долгое=да", effScene, SCENE_FORZA);
    else
      widgets::footer(ui, scenes::actionHint(effScene, ui), effScene,
                      SCENE_FORZA);
  }

  /* alert frame on top */
  if (alertActive(ui) && effScene != SCENE_FORZA) {
    bool on = (ui.now / 250) & 1;
    uint16_t c = on ? CRIT : ORANGE_DIM;
    g.drawRect(0, 0, NOCT_W, NOCT_H, c);
    g.drawRect(1, 1, NOCT_W - 2, NOCT_H - 2, c);
    static const char *metrics[] = {"CPU TEMP", "GPU TEMP", "CPU LOAD",
                                    "GPU LOAD", "VRAM",     "RAM"};
    const char *m = (ui.st.alertMetric >= 0 && ui.st.alertMetric < 6)
                        ? metrics[ui.st.alertMetric]
                        : "ALERT";
    g.fillRect(60, 0, 200, 18, on ? CRIT : BG);
    g.setFont(&F_MED);
    g.setTextSize(1);
    char buf[40];
    snprintf(buf, sizeof(buf), "!! %s !!", m);
    textCenter(g, NOCT_W / 2, 1, buf, on ? BG : CRIT);
    g.setTextSize(1);
    xbmScaled(g, 4, NOCT_H - 38, wolf_aggressive, 32, 32, 1, on ? CRIT : DIM);
  }

  /* menu / sysinfo overlays */
  if (menuOpen_) drawMenu(ui);
  if (sysInfo_) {
    g.fillRect(0, NOCT_CONTENT_TOP, NOCT_W, NOCT_CONTENT_H, BG);
    scenes::drawSysInfo(ui);
  }

  /* toast */
  if ((long)(toastUntil_ - ui.now) > 0) {
    g.setFont(&F_TEXT);
    int tw = g.textWidth(toast_.c_str()) + 16;
    int tx = (NOCT_W - tw) / 2;
    g.fillRoundRect(tx, 130, tw, 18, 4, ORANGE);
    textAt(g, tx + 8, 134, toast_.c_str(), BG);
  }

  /* scene-change wipe (180 ms) */
  if (transStart_ && ui.now - transStart_ < NOCT_TRANSITION_MS) {
    int p = (int)((ui.now - transStart_) * NOCT_W / NOCT_TRANSITION_MS);
    g.fillRect(p, NOCT_CONTENT_TOP, NOCT_W - p, NOCT_CONTENT_H, BG);
    g.drawFastVLine(p, NOCT_CONTENT_TOP, NOCT_CONTENT_H, ORANGE);
    g.drawFastVLine(p + 1, NOCT_CONTENT_TOP, NOCT_CONTENT_H, ORANGE_DIM);
  } else {
    transStart_ = 0;
  }
}

/* ── Boot: panel test card → BIOS post → wolf reveal → load bar ──────── */

void SceneManager::bootAnimation(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;

  /* 1. test card (validates offset/inversion/rgb order at a glance) */
  g.fillSprite(BG);
  g.fillRect(0, 0, NOCT_W / 3, 40, rgb(255, 0, 0));
  g.fillRect(NOCT_W / 3, 0, NOCT_W / 3, 40, rgb(0, 255, 0));
  g.fillRect(2 * NOCT_W / 3, 0, NOCT_W / 3, 40, rgb(0, 0, 255));
  for (int i = 0; i < 8; i++) {
    uint8_t v = i * 36;
    g.fillRect(i * NOCT_W / 8, 44, NOCT_W / 8, 20, rgb(v, v, v));
  }
  g.drawRect(0, 0, NOCT_W, NOCT_H, ORANGE); /* full border = offsets OK */
  g.setFont(&F_TEXT);
  textCenter(g, NOCT_W / 2, 90, "PANEL TEST", TEXT);
  textCenter(g, NOCT_W / 2, 110, "320 x 172 ST7789", DIM);
  d_.disp->push();
  delay(700);

  /* 2. BIOS post */
  static const char *post[] = {"NOCTURNE BIOS v" NOCT_VERSION,
                               "RISC-V CORE 160MHZ ........ OK",
                               "WIFI6 RADIO ............... OK",
                               "WOLF KERNEL ........... LOADED"};
  g.fillSprite(BG);
  g.setFont(&F_TEXT);
  for (int line = 0; line < 4; line++) {
    String s = post[line];
    for (size_t c = 1; c <= (size_t)s.length(); c += 2) {
      g.fillRect(0, 20 + line * 16, NOCT_W, 16, BG);
      textAt(g, 12, 22 + line * 16, s.substring(0, c).c_str(), ORANGE);
      d_.disp->push();
      delay(8);
    }
  }
  delay(250);

  /* 3. wolf logo reveal (3x = 96x96), clipped from top */
  for (int reveal = 8; reveal <= 96; reveal += 8) {
    g.fillSprite(BG);
    g.setClipRect(0, 0, NOCT_W, 30 + reveal);
    xbmScaled(g, (NOCT_W - 96) / 2, 30, wolf_idle, 32, 32, 3, ORANGE);
    g.clearClipRect();
    d_.disp->push();
    delay(30);
  }
  g.setFont(&F_MED);
  g.setTextSize(1);
  textCenter(g, NOCT_W / 2, 134, "N O C T U R N E", TEXT);
  g.setTextSize(1);
  d_.disp->push();
  delay(350);

  /* 4. segmented loading bar */
  int bx = 60, by = 156, bw = 200, seg = 10;
  for (int i = 0; i <= seg; i++) {
    g.fillRect(bx - 2, by - 2, bw + 4, 12, BG);
    g.drawRect(bx - 2, by - 2, bw + 4, 12, ORANGE_DIM);
    for (int k = 0; k < i; k++)
      g.fillRect(bx + k * (bw / seg) + 1, by, bw / seg - 2, 8, ORANGE);
    d_.disp->push();
    delay(60);
  }
}
