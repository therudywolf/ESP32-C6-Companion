#include "core/SettingsStore.h"

#include <Preferences.h>
#include <esp_system.h>

#include "core/config.h"
#include <string.h>

#include "ui/Carousel.h"
#include "ui/SceneIds.h"

namespace settings {

void load(Settings &s) {
  Preferences p;
  p.begin("nocturne", true);
  s.ledEnabled = p.getBool("led", true);
  s.carouselEnabled = p.getBool("carousel", false);
  s.carouselIntervalSec = p.getInt("carouselSec", 10);
  s.brightness = p.getInt("bright", 170);
  s.displayTimeoutSec = p.getInt("dispTimeout", 0);
  s.pinnedScene = p.getInt("pinScene", -1);
  s.netSel = p.getInt("netSel", -1);
  s.petLlm = p.getBool("petLlm", true);
  s.wolfChatter = p.getInt("wChat", 2);
  s.wolfTone = p.getInt("wTone", 0);
  s.notifShow = p.getBool("notifShow", true);
  s.ledMode = p.getInt("ledMode", 0);
  if (s.ledMode < 0 || s.ledMode > 3) s.ledMode = 0;
  s.uiElements = (uint16_t)p.getUShort("uiElem", 0xFFFF);
  if (s.wolfChatter < 0 || s.wolfChatter > 3) s.wolfChatter = 2;
  if (s.wolfTone < 0 || s.wolfTone > 3) s.wolfTone = 0;
  s.flipped = p.getBool("flip", false);
  s.themePreset = p.getInt("theme", 0);
  s.bgStyle = p.getInt("bgStyle", 1);
  s.dotStyle = p.getInt("dots", 0);
  if (s.dotStyle < 0 || s.dotStyle > 2) s.dotStyle = 0;
  s.bgLight = p.getBool("bgLight", false);
  s.customActive = p.getBool("customOn", false);
  p.getBytes("custom", s.custom, sizeof(s.custom));
  s.activeSlot = p.getInt("aslot", 0);
  if (s.activeSlot < 0 || s.activeSlot > 2) s.activeSlot = 0;
  s.slotUsed[0] = p.getBool("slotU0", false);
  s.slotUsed[1] = p.getBool("slotU1", false);
  s.slotUsed[2] = p.getBool("slotU2", false);
  p.getBytes("slot0", s.slot[0], sizeof(s.slot[0]));
  p.getBytes("slot1", s.slot[1], sizeof(s.slot[1]));
  p.getBytes("slot2", s.slot[2], sizeof(s.slot[2]));
  /* Scene mask, with a migration. A saved mask is a snapshot of the ring as it
   * was THEN: the companion panel writes it clamped to the scene count it knew
   * about, so every scene added later arrives switched OFF and looks broken —
   * which is exactly what ДОМ did on this board (stored 0xFFFF, bit 16 clear).
   * scnBits records how wide the ring was when the mask was written; anything
   * added since defaults to on, the same as a fresh install gets. */
  s.sceneMask = p.getUInt("scnMask", 0xFFFFFFFFu) | 1u; /* DEN always on */
  /* Carousel shape. The frequency table is stored as one blob rather than
   * twenty keys because an NVS write is the slow part, and it is re-seeded
   * from the preset whenever it is missing or the wrong size — which is what
   * a first boot, and a firmware that gained a scene, both look like. */
  s.carPreset = p.getInt("carPre", 1);
  if (s.carPreset < 0 || s.carPreset >= carousel::PRESET_N) s.carPreset = 1;
  size_t got = p.getBytes("carFreq", s.carFreq, sizeof(s.carFreq));
  bool sane = got == sizeof(s.carFreq);
  if (sane) {
    /* A table of nothing but zeroes is a table that shows nothing, which
     * would read as the carousel being broken rather than as being empty. */
    bool any = false;
    for (int i = 0; i < SCENE_COUNT; i++) {
      if (s.carFreq[i] >= carousel::FQ_COUNT) sane = false;
      if (s.carFreq[i] != carousel::FQ_OFF) any = true;
    }
    if (!any) sane = false;
  }
  if (!sane)
    memcpy(s.carFreq, carousel::PRESETS[s.carPreset].freq, sizeof(s.carFreq));
  int savedBits = p.getInt("scnBits", 16); /* 16 = the ring before ДОМ */
  if (savedBits < SCENE_FORZA) {
    for (int i = savedBits; i < SCENE_FORZA; i++) s.sceneMask |= (1u << i);
    Serial.printf("[CFG] ring grew %d -> %d scenes; new ones enabled\n",
                  savedBits, (int)SCENE_FORZA);
  }
  s.zbAlert = p.getBool("zbAl", false);
  s.zbTempMin = p.getInt("zbTmin", -99);
  s.zbTempMax = p.getInt("zbTmax", 99);
  s.zbHumMin = p.getInt("zbHmin", -1);
  s.zbHumMax = p.getInt("zbHmax", 101);
  s.zbBattMin = p.getInt("zbBat", 15);
  s.nightMode = p.getBool("night", false);
  s.nightFrom = p.getInt("nightF", 23);
  s.nightTo = p.getInt("nightT", 8);
  p.end();
  if (s.nightFrom < 0 || s.nightFrom > 23) s.nightFrom = 23;
  if (s.nightTo < 0 || s.nightTo > 23) s.nightTo = 8;
  if (s.pinnedScene < -1) s.pinnedScene = -1;
  /* Clamp to the same window the menu and the RC path use, so a stored value
   * always round-trips (see main.cpp's rcBright handling). */
  if (s.brightness < 30) s.brightness = 30;
  if (s.brightness > NOCT_BRIGHT_MAX) s.brightness = NOCT_BRIGHT_MAX;
}

void save(const Settings &s) {
  Preferences p;
  p.begin("nocturne", false);
  p.putBool("led", s.ledEnabled);
  p.putBool("carousel", s.carouselEnabled);
  p.putInt("carPre", s.carPreset);
  p.putBytes("carFreq", s.carFreq, sizeof(s.carFreq));
  p.putInt("carouselSec", s.carouselIntervalSec);
  p.putInt("bright", s.brightness > NOCT_BRIGHT_MAX ? NOCT_BRIGHT_MAX
                                                    : s.brightness);
  p.putInt("dispTimeout", s.displayTimeoutSec);
  p.putInt("pinScene", s.pinnedScene);
  p.putInt("netSel", s.netSel);
  p.putBool("petLlm", s.petLlm);
  p.putInt("wChat", s.wolfChatter);
  p.putInt("wTone", s.wolfTone);
  p.putBool("notifShow", s.notifShow);
  p.putInt("ledMode", s.ledMode);
  p.putUShort("uiElem", s.uiElements);
  p.putBool("flip", s.flipped);
  p.putInt("theme", s.themePreset);
  p.putInt("bgStyle", s.bgStyle);
  p.putInt("dots", s.dotStyle);
  p.putBool("bgLight", s.bgLight);
  p.putBool("customOn", s.customActive);
  p.putBytes("custom", s.custom, sizeof(s.custom));
  p.putInt("aslot", s.activeSlot);
  p.putBool("slotU0", s.slotUsed[0]);
  p.putBool("slotU1", s.slotUsed[1]);
  p.putBool("slotU2", s.slotUsed[2]);
  p.putBytes("slot0", s.slot[0], sizeof(s.slot[0]));
  p.putBytes("slot1", s.slot[1], sizeof(s.slot[1]));
  p.putBytes("slot2", s.slot[2], sizeof(s.slot[2]));
  p.putUInt("scnMask", s.sceneMask | 1u);
  p.putInt("scnBits", (int)SCENE_FORZA);
  p.putBool("zbAl", s.zbAlert);
  p.putInt("zbTmin", s.zbTempMin);
  p.putInt("zbTmax", s.zbTempMax);
  p.putInt("zbHmin", s.zbHumMin);
  p.putInt("zbHmax", s.zbHumMax);
  p.putInt("zbBat", s.zbBattMin);
  p.putBool("night", s.nightMode);
  p.putInt("nightF", s.nightFrom);
  p.putInt("nightT", s.nightTo);
  p.end();
}

void factoryReset(Settings &s) {
  Preferences p;
  p.begin("nocturne", false);
  p.clear(); /* only this namespace — "wolfpet" (the pet) is a separate one */
  p.end();
  s = Settings(); /* compiled defaults */
  load(s);        /* re-apply the same clamps a normal boot would */
}

/* ── Boot diagnostics ────────────────────────────────────────────────────
 * The watchdog in main.cpp panics-and-reboots on a wedged render loop, and the
 * board is always mounted, so a self-heal is silent by design. Persisting the
 * reason turns "it looks fine now" into "it rebooted 3 times, on the watchdog".
 */
static const char *reasonText(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON: return "питание";
  case ESP_RST_EXT: return "сброс";
  case ESP_RST_SW: return "перезапуск";
  case ESP_RST_PANIC: return "паника";
  case ESP_RST_INT_WDT: return "wdt (int)";
  case ESP_RST_TASK_WDT: return "wdt (задача)";
  case ESP_RST_WDT: return "wdt";
  case ESP_RST_DEEPSLEEP: return "сон";
  case ESP_RST_BROWNOUT: return "просадка";
  case ESP_RST_SDIO: return "sdio";
  default: return "неизвестно";
  }
}

void readBootInfo(BootInfo &b) {
  esp_reset_reason_t r = esp_reset_reason();
  b.reason = (int)r;
  b.reasonText = reasonText(r);
  b.lastWasFault = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
                    r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
                    r == ESP_RST_BROWNOUT);
  Preferences p;
  p.begin("nocturne", false);
  b.bootCount = p.getUInt("bootN", 0) + 1;
  b.faultCount = p.getUInt("faultN", 0) + (b.lastWasFault ? 1 : 0);
  p.putUInt("bootN", b.bootCount);
  if (b.lastWasFault) p.putUInt("faultN", b.faultCount);
  p.end();
  Serial.printf("[BOOT] reset=%s boot#%lu faults=%lu\n", b.reasonText,
                (unsigned long)b.bootCount, (unsigned long)b.faultCount);
}

} // namespace settings
