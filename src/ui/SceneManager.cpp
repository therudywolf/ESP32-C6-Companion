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
  int target = (s % SCENE_COUNT + SCENE_COUNT) % SCENE_COUNT;
  /* shortest-path direction around the ring → slide reveals that way */
  int diff = target - scene_;
  transDir_ = (diff > 0) ? (diff <= SCENE_COUNT / 2 ? 1 : -1)
                         : (diff < -SCENE_COUNT / 2 ? 1 : -1);
  scene_ = target;
  transStart_ = ui.now;
  sceneOsdAt_ = ui.now; /* brief scene-name OSD so switches are obvious */
  /* let the wolf know what the owner is looking at (not DEN / Forza) */
  ui.brain.setViewScene(
      (scene_ == SCENE_DEN || scene_ == SCENE_FORZA) ? nullptr
                                                     : scenes::title(scene_));
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
  /* a button dismisses the notification flyover (and doesn't also navigate) */
  if (notifUntil_) {
    notifUntil_ = 0;
    return;
  }
  /* a button during a media peek cancels the auto-return — you take over */
  if (mediaPeekUntil_) {
    mediaPeekUntil_ = 0;
    preMediaScene_ = -1;
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

  /* on-device colour editor: role list ↔ R/G/B tuning, save to the active slot */
  if (editMode_) {
    Settings &s = ui.st.settings;
    if (!editChan_) {
      switch (ev) {
      case EV_SHORT:
        editRole_ = (editRole_ + 1) % theme::COLOR_ROLES;
        editLoadRole();
        break;
      case EV_LONG:
        editChan_ = true;
        editCh_ = 0;
        break;
      case EV_DOUBLE: /* save to slot + persist + exit */
        theme::getPalette(s.custom);
        memcpy(s.slot[s.activeSlot], s.custom, sizeof(s.custom));
        s.slotUsed[s.activeSlot] = true;
        s.customActive = true;
        settings::save(s);
        editMode_ = false;
        {
          char t[24];
          snprintf(t, sizeof(t), "сохранено: слот %d", s.activeSlot + 1);
          toast(t);
        }
        break;
      case EV_TRIPLE: /* leave without naming a slot (live palette kept) */
        theme::getPalette(s.custom);
        s.customActive = true;
        settings::save(s);
        editMode_ = false;
        toast("готово");
        break;
      default:
        break;
      }
    } else {
      switch (ev) {
      case EV_SHORT: { /* bump the active channel in 32-steps, hit 255, wrap */
        int *ch = editCh_ == 0 ? &editR_ : editCh_ == 1 ? &editG_ : &editB_;
        if (*ch >= 255)
          *ch = 0;
        else
          *ch = (*ch + 32 > 255) ? 255 : *ch + 32;
        theme::setColorRole(editRole_, editR_, editG_, editB_);
        break;
      }
      case EV_LONG: /* next channel; after B, back to the role list */
        if (++editCh_ > 2) {
          editCh_ = 0;
          editChan_ = false;
        }
        break;
      case EV_DOUBLE:
        editChan_ = false; /* back to the role list */
        break;
      case EV_TRIPLE:
        theme::getPalette(s.custom);
        s.customActive = true;
        settings::save(s);
        editMode_ = false;
        toast("готово");
        break;
      default:
        break;
      }
    }
    return;
  }

  /* screen-composition picker: toggle which scenes ride the ring */
  if (scenePickMode_) {
    Settings &s = ui.st.settings;
    switch (ev) {
    case EV_SHORT:
      if (++scenePickSel_ >= SCENE_FORZA) scenePickSel_ = SCENE_DASH;
      break;
    case EV_LONG:
      s.sceneMask ^= (1u << scenePickSel_);
      s.sceneMask |= 1u; /* DEN never toggles off */
      break;
    case EV_DOUBLE:
      s.sceneMask |= 1u;
      settings::save(s);
      scenePickMode_ = false;
      toast("экраны сохранены");
      break;
    case EV_TRIPLE:
      scenePickMode_ = false;
      toast("готово");
      break;
    default:
      break;
    }
    return;
  }

  /* element-composition picker: toggle which widget classes show */
  if (elemPickMode_) {
    Settings &s = ui.st.settings;
    switch (ev) {
    case EV_SHORT:
      if (++elemPickSel_ >= theme::UI_ELEM_COUNT) elemPickSel_ = 0;
      break;
    case EV_LONG:
      s.uiElements ^= (1u << elemPickSel_);
      theme::uiElements = s.uiElements; /* apply live */
      break;
    case EV_DOUBLE:
      settings::save(s);
      elemPickMode_ = false;
      toast("элементы сохранены");
      break;
    case EV_TRIPLE:
      elemPickMode_ = false;
      toast("готово");
      break;
    default:
      break;
    }
    return;
  }

  if (menuOpen_) {
    const int kMenuItems = 18;
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
      denSel_ = (denSel_ + 1) % WolfPet::ACT_COUNT;
      break;
    case EV_LONG: {
      ui.pet.doAction(denSel_);
      ui.brain.onAction(denSel_);
      d_.led->setMode(StatusLed::BLIP_OK);
      toast(denSel_ == 0   ? "ням-ням!"
            : denSel_ == 1 ? "поиграли!"
            : denSel_ == 2 ? "мур-р-р..."
                           : "...");
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
    /* FORZA is an app, not a ring member: cycle the enabled ring scenes */
    gotoScene(nextVisibleScene(scene_, ui.st.settings.sceneMask, true), ui);
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
  case 1: /* brightness — steps 50/90/130/170/210, capped under the thermal cliff */
    s.brightness += 40;
    if (s.brightness > NOCT_BRIGHT_MAX) s.brightness = 50;
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
  case 6: /* theme preset (cycling drops any custom palette) */
    s.themePreset = (s.themePreset + 1) % theme::THEME_PRESETS;
    s.customActive = false;
    theme::applyPreset(s.themePreset);
    theme::setBgLight(s.bgLight);
    toast(theme::presetName(s.themePreset));
    break;
  case 7: /* background style */
    s.bgStyle = (s.bgStyle + 1) % theme::BG_STYLES;
    theme::setBgStyle(s.bgStyle);
    toast(theme::bgStyleName(s.bgStyle));
    break;
  case 8: /* light / dark background */
    s.bgLight = !s.bgLight;
    theme::setBgLight(s.bgLight);
    toast(s.bgLight ? "светлый фон" : "тёмный фон");
    break;
  case 9: { /* theme slot — cycle 1→2→3, load that saved palette if it exists */
    s.activeSlot = (s.activeSlot + 1) % 3;
    if (s.slotUsed[s.activeSlot]) {
      memcpy(s.custom, s.slot[s.activeSlot], sizeof(s.custom));
      s.customActive = true;
      theme::applyPalette(s.custom);
      theme::setBgLight(s.bgLight);
      char t[24];
      snprintf(t, sizeof(t), "слот %d", s.activeSlot + 1);
      toast(t);
    } else {
      char t[24];
      snprintf(t, sizeof(t), "слот %d (пусто)", s.activeSlot + 1);
      toast(t);
    }
    break;
  }
  case 10: /* on-device colour editor */
    menuOpen_ = false;
    editMode_ = true;
    editRole_ = 0;
    editChan_ = false;
    editLoadRole();
    toast("редактор цвета");
    break;
  case 11: /* screen composition picker */
    menuOpen_ = false;
    scenePickMode_ = true;
    scenePickSel_ = SCENE_DASH;
    toast("выбор экранов");
    break;
  case 12: /* Forza HUD (manual open — it also auto-enters on telemetry) */
    menuOpen_ = false;
    gotoScene(SCENE_FORZA, ui);
    break;
  case 13: /* sysinfo */
    sysInfo_ = true;
    menuOpen_ = false;
    break;
  case 14: { /* wolf chatter frequency */
    s.wolfChatter = (s.wolfChatter + 1) % 4;
    static const char *n[] = {"болтливость: выкл", "болтливость: редко",
                              "болтливость: норма", "болтливость: часто"};
    toast(n[s.wolfChatter & 3]);
    break;
  }
  case 15: { /* wolf tone / character */
    s.wolfTone = (s.wolfTone + 1) % 4;
    static const char *n[] = {"характер: обычный", "характер: добрый",
                              "характер: ворчун", "характер: дерзкий"};
    toast(n[s.wolfTone & 3]);
    break;
  }
  case 16: /* element composition picker */
    menuOpen_ = false;
    elemPickMode_ = true;
    elemPickSel_ = 0;
    toast("элементы экрана");
    break;
  case 17: /* close */
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

  char val[18][20];
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
  snprintf(val[6], 20, "%s", s.customActive ? "своя" : theme::presetName(s.themePreset));
  snprintf(val[7], 20, "%s", theme::bgStyleName(s.bgStyle));
  snprintf(val[8], 20, "%s", s.bgLight ? "светлый" : "тёмный");
  snprintf(val[9], 20, "#%d%s", s.activeSlot + 1,
           s.slotUsed[s.activeSlot] ? "" : " нов");
  val[10][0] = '\0';
  {
    int onCount = 0;
    for (int i = SCENE_DASH; i < SCENE_FORZA; i++)
      if ((s.sceneMask >> i) & 1u) onCount++;
    snprintf(val[11], 20, "%d/%d", onCount + 1, SCENE_FORZA); /* +1 = ЛОГОВО */
  }
  val[12][0] = '\0';
  val[13][0] = '\0';
  {
    static const char *chatterN[] = {"выкл", "редко", "норма", "часто"};
    static const char *toneN[] = {"обычный", "добрый", "ворчун", "дерзкий"};
    snprintf(val[14], 20, "%s", chatterN[s.wolfChatter & 3]);
    snprintf(val[15], 20, "%s", toneN[s.wolfTone & 3]);
  }
  {
    int onCount = 0;
    for (int i = 0; i < theme::UI_ELEM_COUNT; i++)
      if ((s.uiElements >> i) & 1u) onCount++;
    snprintf(val[16], 20, "%d/%d", onCount, theme::UI_ELEM_COUNT);
  }
  val[17][0] = '\0';

  static const char *names[] = {
      "Карусель",     "Яркость",       "LED",          "Волк LLM",
      "WiFi",         "Переворот",      "Тема",         "Фон",
      "Светлый фон",  "Слот темы",      "Цвета вручную", "Экраны",
      "Forza HUD",    "Инфо системы",   "Болтливость",  "Характер",
      "Элементы",     "Закрыть"};
  /* 6 visible rows, 22 px tall — no glyph overlap; list scrolls */
  const int kRows = 18, kVisible = 6, rowH = 22;
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

/* Pull the focused role's RGB (0..255) out of the live palette so the channel
 * sliders start from the real current colour. */
void SceneManager::editLoadRole() {
  uint16_t pal[theme::COLOR_ROLES];
  theme::getPalette(pal);
  uint16_t c = pal[editRole_];
  editR_ = ((c >> 11) & 0x1F) * 255 / 31;
  editG_ = ((c >> 5) & 0x3F) * 255 / 63;
  editB_ = (c & 0x1F) * 255 / 31;
}

/* On-device colour editor: 10-role swatch strip + R/G/B sliders, edits the live
 * palette so the whole HUD recolours under your hands. Saves to a theme slot. */
void SceneManager::drawColorEditor(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  Settings &s = ui.st.settings;
  g.fillRect(0, NOCT_CONTENT_TOP, NOCT_W, NOCT_H - NOCT_CONTENT_TOP, BG);
  g.drawFastHLine(0, NOCT_CONTENT_TOP, NOCT_W, ORANGE);

  uint16_t pal[theme::COLOR_ROLES];
  theme::getPalette(pal);

  char ttl[32];
  snprintf(ttl, sizeof(ttl), "РЕДАКТОР / СЛОТ %d", s.activeSlot + 1);
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  textAt(g, 8, 24, ttl, ORANGE);

  /* swatch strip — all 10 roles, cursor box on the focused one */
  for (int i = 0; i < theme::COLOR_ROLES; i++) {
    int x = 6 + i * 31;
    g.fillRect(x, 38, 28, 16, pal[i]);
    g.drawRect(x, 38, 28, 16, ORANGE_DIM);
    if (i == editRole_) {
      g.drawRect(x - 2, 36, 32, 20, TEXT);
      g.drawRect(x - 1, 37, 30, 18, TEXT);
    }
  }

  /* focused role name + live colour chip */
  g.setFont(&F_MED);
  g.setTextSize(1);
  char nm[24];
  clipW(g, theme::roleName(editRole_), nm, sizeof(nm), 210);
  textAt(g, 8, 60, nm, TEXT);
  g.fillRect(236, 58, 76, 20, pal[editRole_]);
  g.drawRect(236, 58, 76, 20, ORANGE_DIM);

  /* three channel sliders */
  const char *chName[3] = {"R", "G", "B"};
  uint16_t chCol[3] = {rgb(255, 60, 60), rgb(60, 230, 90), rgb(80, 140, 255)};
  int chVal[3] = {editR_, editG_, editB_};
  for (int c = 0; c < 3; c++) {
    int y = 84 + c * 22;
    bool active = editChan_ && editCh_ == c;
    if (active) g.fillTriangle(2, y + 1, 2, y + 13, 9, y + 7, ORANGE);
    g.setFont(&F_MED);
    textAt(g, 12, y - 2, chName[c], active ? ORANGE : DIM);
    hBar(g, 30, y, 240, 14, chVal[c] * 100 / 255, chCol[c]);
    char nv[8];
    snprintf(nv, sizeof(nv), "%d", chVal[c]);
    g.setFont(&F_TEXT);
    textRight(g, 314, y + 2, nv, active ? ORANGE : DIM);
  }

  g.setFont(&F_TEXT);
  if (!editChan_)
    textAt(g, 8, 156, "1x далее / долго настроить / 2x сохранить / 3x выход",
           DIM);
  else
    textAt(g, 8, 156, "1x +цвет / долго след.канал / 2x назад / 3x выход", DIM);
}

int SceneManager::nextVisibleScene(int from, uint32_t mask, bool allowDen) const {
  for (int k = 1; k <= SCENE_FORZA; k++) {
    int n = from + k;
    while (n >= SCENE_FORZA) n -= SCENE_FORZA; /* wrap inside the ring 0..15 */
    if (n == SCENE_DEN) {
      if (allowDen) return n;
      continue;
    }
    if (mask & (1u << n)) return n;
  }
  return SCENE_DEN; /* everything off → fall back home */
}

/* Screen-composition picker: toggle which scenes appear in the nav ring. */
void SceneManager::drawScenePicker(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  Settings &s = ui.st.settings;
  g.fillRect(0, NOCT_CONTENT_TOP, NOCT_W, NOCT_H - NOCT_CONTENT_TOP, BG);
  g.drawFastHLine(0, NOCT_CONTENT_TOP, NOCT_W, ORANGE);
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  textAt(g, 8, 24, "ЭКРАНЫ В КАРУСЕЛИ (ЛОГОВО всегда вкл)", ORANGE);

  const int first = SCENE_DASH, last = SCENE_FORZA - 1; /* DASH..HISTORY */
  const int total = last - first + 1;
  const int kVisible = 5, rowH = 22;
  int idx = scenePickSel_ - first;
  int scroll = idx - (kVisible - 1);
  if (scroll < 0) scroll = 0;
  g.setFont(&F_MED);
  g.setTextSize(1);
  for (int r = 0; r < kVisible && scroll + r < total; r++) {
    int sc = first + scroll + r;
    int y = 40 + r * rowH;
    bool sel = sc == scenePickSel_;
    bool on = (s.sceneMask >> sc) & 1u;
    if (sel) g.fillRect(6, y - 2, NOCT_W - 18, rowH - 2, ORANGE);
    char nm[24];
    clipW(g, scenes::title(sc), nm, sizeof(nm), 210);
    textAt(g, 14, y, nm, sel ? BG : TEXT);
    textRight(g, NOCT_W - 20, y, on ? "вкл" : "выкл",
              sel ? BG : (on ? GOOD : DIM));
  }
  /* scrollbar */
  int sbH = (NOCT_H - 44) * kVisible / total;
  int sbY = 40 + (NOCT_H - 44 - sbH) * scroll / (total - kVisible);
  g.fillRect(NOCT_W - 6, 40, 3, NOCT_H - 50, PANEL);
  g.fillRect(NOCT_W - 6, sbY, 3, sbH, ORANGE);

  g.setFont(&F_TEXT);
  textAt(g, 8, 156, "1x далее / долго вкл-выкл / 2x сохранить / 3x выход", DIM);
}

/* Element-composition picker: toggle which optional widget classes show. */
void SceneManager::drawElemPicker(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  Settings &s = ui.st.settings;
  g.fillRect(0, NOCT_CONTENT_TOP, NOCT_W, NOCT_H - NOCT_CONTENT_TOP, BG);
  g.drawFastHLine(0, NOCT_CONTENT_TOP, NOCT_W, ORANGE);
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  textAt(g, 8, 24, "ЭЛЕМЕНТЫ ИНТЕРФЕЙСА (на всех экранах)", ORANGE);

  static const char *names[theme::UI_ELEM_COUNT] = {
      "Графики", "Стрелки тренда", "Доп. строки", "Лапки",
      "Реплики на экранах"};
  g.setFont(&F_MED);
  g.setTextSize(1);
  for (int i = 0; i < theme::UI_ELEM_COUNT; i++) {
    int y = 40 + i * 22;
    bool sel = i == elemPickSel_;
    bool on = (s.uiElements >> i) & 1u;
    if (sel) g.fillRect(6, y - 2, NOCT_W - 18, 20, ORANGE);
    textAt(g, 14, y, names[i], sel ? BG : TEXT);
    textRight(g, NOCT_W - 20, y, on ? "вкл" : "выкл",
              sel ? BG : (on ? GOOD : DIM));
  }
  g.setFont(&F_TEXT);
  textAt(g, 8, 156, "1x далее / долго вкл-выкл / 2x сохранить / 3x выход", DIM);
}

/* Ambient screensaver: drifting starfield + wandering wolf + big clock.
 * Replaces the old "dim to black" — any button press wakes it. */
void SceneManager::drawScreensaver(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  unsigned long now = ui.now;

  /* starfield — 48 deterministic stars drifting down, twinkling */
  for (int i = 0; i < 48; i++) {
    uint32_t h = i * 2654435761u; /* Knuth hash → pseudo-random but stable */
    int sx = (h >> 8) % NOCT_W;
    int speed = 8 + (h % 5);
    int sy = (int)(((h >> 3) + now / (40 / speed)) % NOCT_H);
    int tw = (int)((now / 120 + i) % 6);
    uint16_t c = tw < 3 ? lerp565(BG, INFO, 60 + tw * 50) : DIM;
    g.drawPixel(sx, sy, c);
    if (i % 7 == 0) g.drawPixel(sx, (sy + 1) % NOCT_H, c);
  }

  /* big clock, centered */
  if (ui.st.pcClock[0]) {
    g.setFont(&theme::F_HUGE);
    g.setTextSize(2);
    int cw = g.textWidth(ui.st.pcClock);
    textCenter(g, NOCT_W / 2, 18 - (g.fontHeight() - 64) / 2, ui.st.pcClock,
               lerp565(ORANGE, TEXT, 80));
    g.setTextSize(1);
    (void)cw;
  }

  /* wandering wolf — bounces left↔right along the lower third */
  int span = NOCT_W - 64;
  int phase = (int)((now / 24) % (2 * span));
  int wx = phase < span ? phase : 2 * span - phase;
  int wy = 96 + (int)(sinf(now / 360.0f) * 6);
  /* fading paw-print trail the wolf leaves behind */
  for (int k = 1; k <= 4; k++) {
    long pnow = (long)now - k * 260;
    if (pnow < 0) break;
    int ph = (int)((pnow / 24) % (2 * span));
    int pwx = ph < span ? ph : 2 * span - ph;
    widgets::pawPrint(g, pwx + 32, 150 + (k & 1) * 6,
                      lerp565(BG, ORANGE_DIM, 150 - k * 30));
  }
  const unsigned char *frame = ((now / 200) & 3) == 0 ? wolf_blink : wolf_idle;
  if (!ui.st.link.tcpConnected) frame = wolf_idle;
  widgets::xbmScaled(g, wx, wy, frame, 32, 32, 2, ORANGE);

  /* tiny ambient PC line + hint */
  g.setFont(&theme::F_TEXT);
  if (ui.st.link.tcpConnected && !ui.st.link.signalLost) {
    char buf[40];
    snprintf(buf, sizeof(buf), "CPU %dC   GPU %dC   RAM %.0f%%", ui.st.hw.ct,
             ui.st.hw.gt,
             ui.st.hw.ra > 0.1f ? ui.st.hw.ru * 100 / ui.st.hw.ra : 0);
    textCenter(g, NOCT_W / 2, 80, buf, DIM);
  }
  if ((now / 700) & 1)
    textCenter(g, NOCT_W / 2, NOCT_H - 12, "нажми кнопку", ORANGE_DIM);
  /* framebuffer is pushed by the main loop after draw() returns */
}

void SceneManager::drawNotifCard(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  NotifData &n = ui.st.notif;
  const int dur = 7000;
  unsigned long age = ui.now - notifAt_;
  long leftL = (long)(notifUntil_ - ui.now);
  int left = leftL > 0 ? (int)leftL : 0;

  /* slide in from the top: ease-out over the first 220 ms */
  const int mx = 6, cw = NOCT_W - 2 * mx, ch = 116;
  float env = age < 220 ? (float)age / 220.0f : 1.0f;
  float e = 1.0f - powf(1.0f - env, 3.0f);
  int cy = -ch + (int)(e * (ch + 4)); /* from off-top up to y=4 */

  /* card: drop shadow, panel body, accent frame */
  g.fillRoundRect(mx + 2, cy + 3, cw, ch, 10, BG);
  g.fillRoundRect(mx, cy, cw, ch, 10, PANEL);
  g.drawRoundRect(mx, cy, cw, ch, 10, ACCENT);
  g.drawRoundRect(mx + 1, cy + 1, cw - 2, ch - 2, 9, lerp565(PANEL, ACCENT, 90));

  /* header: bell + app name (cyan) + optional "+N" queue badge */
  g.fillCircle(mx + 16, cy + 17, 3, INFO);
  g.drawCircle(mx + 16, cy + 17, 5, lerp565(PANEL, INFO, 120));
  g.setFont(&F_TEXT);
  g.setTextSize(1);
  /* clip the app name so a long one can't run into the +N badge */
  int appRight = mx + cw - (n.pending > 0 ? 44 : 10);
  g.setClipRect(mx + 28, cy + 6, appRight - (mx + 28), 18);
  textAt(g, mx + 28, cy + 10, n.app.length() ? n.app.c_str() : "Уведомление",
         INFO);
  g.clearClipRect();
  if (n.pending > 0) {
    char b[10];
    snprintf(b, sizeof(b), "+%d", n.pending);
    int bw = g.textWidth(b) + 12, bx = mx + cw - bw - 8;
    g.fillRoundRect(bx, cy + 8, bw, 16, 8, lerp565(BG, ACCENT, 70));
    g.drawRoundRect(bx, cy + 8, bw, 16, 8, ACCENT);
    textAt(g, bx + 6, cy + 11, b, ACCENT);
  }
  g.drawFastHLine(mx + 10, cy + 30, cw - 20, lerp565(PANEL, ACCENT, 70));

  /* sender / title — marquee if it doesn't fit */
  g.setFont(&F_MED);
  g.setTextSize(1);
  const char *t = n.title.length() ? n.title.c_str() : "-";
  int tx = mx + 10, tcw = cw - 20, tw = g.textWidth(t);
  g.setClipRect(tx, cy + 37, tcw, 22); /* full F_MED cell incl. descenders */
  if (tw > tcw) {
    int span = tw + 40, off = (int)((ui.now / 35) % span);
    textAt(g, tx - off, cy + 38, t, TEXT);
    textAt(g, tx - off + span, cy + 38, t, TEXT);
  } else {
    textAt(g, tx, cy + 38, t, TEXT);
  }
  g.clearClipRect();

  /* body — wrapped to up to 3 lines (empty in sender-only mode) */
  if (n.body.length()) {
    g.setFont(&F_TEXT);
    widgets::textWrap(g, n.body.c_str(), mx + 10, cy + 58, cw - 20, 15, 3,
                      lerp565(TEXT, BG, 50));
  }

  /* auto-dismiss countdown bar */
  int barW = cw - 20, rem = (int)((long)barW * left / dur);
  if (rem < 0) rem = 0;
  if (rem > barW) rem = barW;
  g.fillRoundRect(mx + 10, cy + ch - 10, barW, 4, 2, lerp565(BG, PANEL, 200));
  g.fillRoundRect(mx + 10, cy + ch - 10, rem, 4, 2, ACCENT);
}

void SceneManager::draw(UiCtx &ui) {
  LGFX_Sprite &g = ui.g;
  Settings &s = ui.st.settings;
  theme::nowMs = ui.now; /* frame clock for animated draw helpers */
  /* feed the reactive backdrop: busier PC = livelier bg, alert = red */
  theme::reactLevel = ui.st.hw.cl > ui.st.hw.gl ? ui.st.hw.cl : ui.st.hw.gl;
  theme::reactAlert = alertActive(ui);
  theme::uiElements = ui.st.settings.uiElements; /* per-element composition */
  g.fillSprite(BG);

  /* remote scene jump (companion app) */
  if (pendingScene_ >= 0 && pendingScene_ < SCENE_COUNT) {
    menuOpen_ = false;
    sysInfo_ = false;
    gotoScene(pendingScene_, ui);
    pendingScene_ = -1;
  }

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

  /* media peek: Spotify starting / a track change briefly takes over the МЕДИА
   * scene, then returns to where you were (works over the Forza HUD too). */
  {
    MediaData &md = ui.st.media;
    bool trackNew = md.track.length() && md.track != lastPeekTrack_;
    bool playStart = md.isPlaying && !lastPeekPlaying_;
    lastPeekTrack_ = md.track;
    lastPeekPlaying_ = md.isPlaying;
    bool free = !menuOpen_ && !sysInfo_ && !editMode_ && !scenePickMode_ &&
                !elemPickMode_ && !alertActive(ui);
    if ((trackNew || playStart) && md.isPlaying && free &&
        scene_ != SCENE_MEDIA) {
      int from = scene_;
      preMediaScene_ = from;
      gotoScene(SCENE_MEDIA, ui);
      mediaPeekUntil_ = ui.now + (from == SCENE_FORZA ? 4500 : 7000);
    } else if (trackNew && scene_ == SCENE_MEDIA && mediaPeekUntil_) {
      mediaPeekUntil_ = ui.now + (preMediaScene_ == SCENE_FORZA ? 4500 : 7000);
    }
    if (mediaPeekUntil_ && ui.now > mediaPeekUntil_) {
      mediaPeekUntil_ = 0;
      if (scene_ == SCENE_MEDIA && preMediaScene_ >= 0)
        gotoScene(preMediaScene_, ui);
      preMediaScene_ = -1;
    }
  }

  /* notification flyover: a new server notification (Telegram etc.) flies in
   * over whatever is on screen, holds ~7 s, then slides away. */
  {
    NotifData &nt = ui.st.notif;
    if (!notifSeeded_ && ui.st.link.tcpConnected) {
      /* on first connect, adopt the server's current seq as the baseline so a
       * notification still on the server's clock isn't replayed after a reboot */
      notifSeeded_ = true;
      lastNotifSeq_ = nt.seq;
    } else if (nt.seq != lastNotifSeq_) {
      lastNotifSeq_ = nt.seq;
      if (nt.seq > 0 && (nt.title.length() || nt.body.length())) {
        notifAt_ = ui.now;
        notifUntil_ = ui.now + 7000;
        if (d_.led) d_.led->flash(40, 150, 170, 600); /* cyan blip on arrival */
      }
    }
    if (notifUntil_ && (long)(ui.now - notifUntil_) >= 0) notifUntil_ = 0;
  }

  /* carousel — never on the wolf's home (you're interacting there: feed/play),
   * never inside his action submenu, never over the Forza HUD or a menu. */
  if (s.carouselEnabled && !menuOpen_ && !sysInfo_ && !editMode_ &&
      !scenePickMode_ && !elemPickMode_ && !mediaPeekUntil_ && !alertActive(ui) &&
      scene_ != SCENE_FORZA &&
      scene_ != SCENE_DEN && !denActionMode_ && ui.now - lastInput_ > 5000 &&
      ui.now - lastCarousel_ > (unsigned long)s.carouselIntervalSec * 1000UL) {
    lastCarousel_ = ui.now;
    gotoScene(nextVisibleScene(scene_, s.sceneMask, false), ui);
  }

  /* screensaver: after the dim timeout, dim a little and show the ambient
   * clock+wolf scene (not a black screen). Any input wakes it. */
  if (s.displayTimeoutSec > 0 && !dimmed_ && !ui.forzaLive && !editMode_ &&
      !menuOpen_ && !scenePickMode_ && !elemPickMode_ &&
      ui.now - lastInput_ > (unsigned long)s.displayTimeoutSec * 1000UL &&
      !alertActive(ui)) {
    dimmed_ = true;
    d_.disp->setBrightness(s.brightness > 90 ? 90 : s.brightness);
  }
  if (dimmed_ && alertActive(ui)) {
    dimmed_ = false;
    d_.disp->setBrightness(s.brightness);
  }
  if (dimmed_ && !alertActive(ui)) {
    drawScreensaver(ui);
    return;
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

  /* animated cyber backdrop behind content (Forza HUD stays clean) */
  if (effScene != SCENE_FORZA)
    theme::backdrop(g, NOCT_STATUS_H, NOCT_H);

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
  case SCENE_HISTORY: scenes::drawHistory(ui); break;
  case SCENE_FORZA: scenes::drawForza(ui); break;
  }

  /* chrome — the Forza HUD owns the whole screen, no bars. Footer hint line
   * removed (wasted space); scene position lives in the status bar now. */
  if (effScene != SCENE_FORZA)
    widgets::statusBar(ui, scenes::title(effScene), effScene, SCENE_FORZA);

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

  /* wolf speech overlay — the wolf "lives in the background": its comment
   * SLIDES UP from the bottom over ANY scene, sits, then slides away. DEN
   * shows speech inline so it's skipped there. */
  if (ui.brain.bubbleVisible(ui.now) && effScene != SCENE_DEN && !menuOpen_ &&
      !sysInfo_ && !editMode_ && !scenePickMode_ && !elemPickMode_ &&
      !ui.brain.thinking() && theme::uiOn(theme::UI_WOLFOVL)) {
    const String &p = ui.brain.phrase();
    if (p.length()) {
      /* taller card resting in the lower-CENTRE (was jammed at the bottom
       * edge: oh=46 rest y=122). 14 px of breathing room below it. */
      const int oh = 58, mx = 6, restGap = 14;
      float env = ui.brain.speechEnvelope(ui.now);
      if (env > 0.001f) {
        /* ease-out cubic on the slide */
        float e = 1.0f - powf(1.0f - env, 3.0f);
        int oy = NOCT_H - (int)(e * (oh + restGap));
        /* drop shadow + body */
        g.fillRoundRect(mx + 2, oy + 2, NOCT_W - 2 * mx, oh, 8, BG);
        g.fillRoundRect(mx, oy, NOCT_W - 2 * mx, oh, 8, PANEL);
        g.drawRoundRect(mx, oy, NOCT_W - 2 * mx, oh, 8, ORANGE);
        g.drawRoundRect(mx + 1, oy + 1, NOCT_W - 2 * mx - 2, oh - 2, 7,
                        lerp565(PANEL, ORANGE, 90));
        /* speaking wolf, framed and vertically centred in the taller card */
        g.fillRoundRect(mx + 4, oy + 4, 42, oh - 8, 6, BG);
        const unsigned char *fr = ((ui.now / 150) & 1) ? wolf_funny : wolf_idle;
        xbmScaled(g, mx + 9, oy + (oh - 32) / 2, fr, 32, 32, 1, ORANGE);
        /* name tag + text */
        g.setFont(&theme::F_TEXT);
        textAt(g, mx + 52, oy + 6, "НОКТЮРН", ACCENT);
        g.setFont(&theme::F_MED);
        widgets::textWrap(g, p.c_str(), mx + 52, oy + 20,
                          NOCT_W - 2 * mx - 58, 18, 2, TEXT);
      }
    }
  }

  /* menu / sysinfo overlays */
  if (menuOpen_) drawMenu(ui);
  if (editMode_) drawColorEditor(ui);
  if (scenePickMode_) drawScenePicker(ui);
  if (elemPickMode_) drawElemPicker(ui);
  if (sysInfo_) {
    g.fillRect(0, NOCT_CONTENT_TOP, NOCT_W, NOCT_CONTENT_H, BG);
    scenes::drawSysInfo(ui);
  }

  /* scene-change OSD: a brief centred name pill so switches read clearly
   * (fades in/out; skipped over modals and the Forza HUD) */
  {
    unsigned long osdAge = ui.now - sceneOsdAt_;
    if (sceneOsdAt_ && osdAge < 850 && !menuOpen_ && !editMode_ &&
        !scenePickMode_ && !elemPickMode_ && !sysInfo_ &&
        scene_ != SCENE_FORZA) {
      int a = osdAge < 130     ? (int)(osdAge * 255 / 130)
              : osdAge > 700   ? (int)((850 - osdAge) * 255 / 150)
                               : 255;
      a = a < 0 ? 0 : (a > 255 ? 255 : a);
      const char *nm = scenes::title(scene_);
      g.setFont(&F_MED);
      g.setTextSize(1);
      int tw = g.textWidth(nm) + 26;
      int px = (NOCT_W - tw) / 2, py = 24;
      g.fillRoundRect(px, py, tw, 24, 6, lerp565(BG, PANEL, a * 220 / 255));
      uint16_t fc = lerp565(BG, ORANGE, a);
      g.drawRoundRect(px, py, tw, 24, 6, fc);
      widgets::pawPrint(g, px + 12, py + 12, fc); /* furry flourish */
      widgets::pawPrint(g, px + tw - 12, py + 12, fc);
      textCenter(g, NOCT_W / 2, py + 3, nm, fc);
    }
  }

  /* notification flyover — over scenes + wolf overlay, below toast; never over a
   * modal or the alert frame (you're interacting there). */
  if (notifUntil_ && (long)(notifUntil_ - ui.now) > 0 && !alertActive(ui) &&
      !menuOpen_ && !editMode_ && !scenePickMode_ && !elemPickMode_ && !sysInfo_)
    drawNotifCard(ui);

  /* toast */
  if ((long)(toastUntil_ - ui.now) > 0) {
    g.setFont(&F_TEXT);
    int tw = g.textWidth(toast_.c_str()) + 16;
    int tx = (NOCT_W - tw) / 2;
    g.fillRoundRect(tx, 130, tw, 18, 4, ORANGE);
    textAt(g, tx + 8, 134, toast_.c_str(), BG);
  }

  /* scene-change wipe (180 ms), directional: forward reveals L→R, back R→L */
  if (transStart_ && ui.now - transStart_ < NOCT_TRANSITION_MS) {
    int p = (int)((ui.now - transStart_) * NOCT_W / NOCT_TRANSITION_MS);
    int edge = transDir_ >= 0 ? p : NOCT_W - p;
    if (transDir_ >= 0)
      g.fillRect(edge, NOCT_CONTENT_TOP, NOCT_W - edge, NOCT_CONTENT_H, BG);
    else
      g.fillRect(0, NOCT_CONTENT_TOP, edge, NOCT_CONTENT_H, BG);
    g.drawFastVLine(edge, NOCT_CONTENT_TOP, NOCT_CONTENT_H, ORANGE);
    g.drawFastVLine(edge + (transDir_ >= 0 ? 1 : -1), NOCT_CONTENT_TOP,
                    NOCT_CONTENT_H, ORANGE_DIM);
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
  widgets::pawPrint(g, 58, 140, ORANGE_DIM); /* flanking paws */
  widgets::pawPrint(g, NOCT_W - 58, 140, ORANGE_DIM);
  d_.disp->push();
  delay(350);

  /* 4. segmented loading bar with a paw walking it */
  int bx = 60, by = 156, bw = 200, seg = 10;
  for (int i = 0; i <= seg; i++) {
    g.fillRect(bx - 2, by - 6, bw + 4, 18, BG);
    g.drawRect(bx - 2, by - 2, bw + 4, 12, ORANGE_DIM);
    for (int k = 0; k < i; k++)
      g.fillRect(bx + k * (bw / seg) + 1, by, bw / seg - 2, 8, ORANGE);
    if (i < seg) widgets::pawPrint(g, bx + i * (bw / seg) + 4, by + 4, TEXT);
    d_.disp->push();
    delay(60);
  }
}
