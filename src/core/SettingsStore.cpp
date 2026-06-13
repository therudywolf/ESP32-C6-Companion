#include "core/SettingsStore.h"

#include <Preferences.h>

#include "core/config.h"

namespace settings {

void load(Settings &s) {
  Preferences p;
  p.begin("nocturne", true);
  s.ledEnabled = p.getBool("led", true);
  s.carouselEnabled = p.getBool("carousel", false);
  s.carouselIntervalSec = p.getInt("carouselSec", 10);
  s.brightness = p.getInt("bright", 200);
  s.displayTimeoutSec = p.getInt("dispTimeout", 0);
  s.pinnedScene = p.getInt("pinScene", -1);
  s.netSel = p.getInt("netSel", -1);
  s.petLlm = p.getBool("petLlm", true);
  s.flipped = p.getBool("flip", false);
  s.themePreset = p.getInt("theme", 0);
  s.bgStyle = p.getInt("bgStyle", 1);
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
  s.sceneMask = p.getUInt("scnMask", 0xFFFFFFFFu) | 1u; /* DEN always on */
  p.end();
  if (s.brightness < 30) s.brightness = 30;
  if (s.brightness > NOCT_BRIGHT_MAX) s.brightness = NOCT_BRIGHT_MAX;
}

void save(const Settings &s) {
  Preferences p;
  p.begin("nocturne", false);
  p.putBool("led", s.ledEnabled);
  p.putBool("carousel", s.carouselEnabled);
  p.putInt("carouselSec", s.carouselIntervalSec);
  p.putInt("bright", s.brightness);
  p.putInt("dispTimeout", s.displayTimeoutSec);
  p.putInt("pinScene", s.pinnedScene);
  p.putInt("netSel", s.netSel);
  p.putBool("petLlm", s.petLlm);
  p.putBool("flip", s.flipped);
  p.putInt("theme", s.themePreset);
  p.putInt("bgStyle", s.bgStyle);
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
  p.end();
}

} // namespace settings
