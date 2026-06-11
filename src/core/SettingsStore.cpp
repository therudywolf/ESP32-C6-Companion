#include "core/SettingsStore.h"

#include <Preferences.h>

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
  p.end();
  if (s.brightness < 10) s.brightness = 10;
  if (s.brightness > 255) s.brightness = 255;
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
  p.end();
}

} // namespace settings
