#include "storage/CardConfig.h"

#include "storage/SdStore.h"

/* "ssid2" -> 2, "ssid" -> 1; 0 when the key is not indexed. */
static int keyIndex(const String &key, const char *base, int baseLen) {
  if (!key.startsWith(base)) return 0;
  if ((int)key.length() == baseLen) return 1;
  int n = key.substring(baseLen).toInt();
  return (n >= 1 && n <= NOCT_WIFI_MAX_NETS) ? n : 0;
}

void CardConfig::apply(const String &section, const String &key,
                       const String &val) {
  if (section == "wifi") {
    int i = keyIndex(key, "ssid", 4);
    if (i) {
      ssid_[i - 1] = val;
      applied_++;
      return;
    }
    i = keyIndex(key, "pass", 4);
    if (i) {
      pass_[i - 1] = val;
      applied_++;
    }
    return;
  }
  if (section == "server") {
    if (key == "host") { host_ = val; applied_++; }
    else if (key == "port") { port_ = (uint16_t)val.toInt(); applied_++; }
    else if (key == "panel") { panelPort_ = (uint16_t)val.toInt(); applied_++; }
    return;
  }
  if (section == "llm") {
    if (key == "endpoint" || key == "endpoint1") { llm_ = val; applied_++; }
    else if (key == "model") { llmModel_ = val; applied_++; }
    else if (key == "key") { llmKey_ = val; applied_++; }
    return;
  }
  if (section == "alarm") {
    if (key == "at") {
      int colon = val.indexOf(':');
      if (colon > 0) {
        int h = val.substring(0, colon).toInt();
        int m = val.substring(colon + 1).toInt();
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
          alarm_ = h * 60 + m;
          applied_++;
        }
      }
    }
    return;
  }
  if (section == "zigbee") {
    int i = keyIndex(key, "name", 4);
    if (i && i <= 4) {
      zbName_[i - 1] = val;
      applied_++;
    }
    return;
  }
  if (section == "wolf") {
    if (key == "skin") { skin_ = val; applied_++; }
    return;
  }
}

bool CardConfig::load(SdStore *sd) {
  if (!sd || !sd->ok()) return false;
  String text;
  /* One read of the whole file. It is a handful of lines; readAll caps it so a
   * junk file cannot blow the heap. */
  if (!sd->readAll("/nocturne.ini", text, 2048) || text.length() == 0)
    return false;
  loaded_ = true;

  String section;
  int start = 0;
  while (start <= text.length()) {
    int nl = text.indexOf('\n', start);
    String line = (nl < 0) ? text.substring(start) : text.substring(start, nl);
    start = (nl < 0) ? text.length() + 1 : nl + 1;
    line.replace("\r", "");
    line.trim();
    if (!line.length() || line.startsWith("#") || line.startsWith(";")) continue;
    if (line.startsWith("[") && line.endsWith("]")) {
      section = line.substring(1, line.length() - 1);
      section.toLowerCase();
      section.trim();
      continue;
    }
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim();
    key.toLowerCase();
    val.trim();
    if (!key.length()) continue;
    apply(section, key, val);
  }

  /* Collapse the sparse ssidN/passN pairs into the contiguous list WifiManager
   * wants. A password-less entry is allowed (open network); an SSID-less one is
   * not a network at all. */
  netCount_ = 0;
  for (int i = 0; i < NOCT_WIFI_MAX_NETS; i++) {
    if (!ssid_[i].length()) continue;
    nets_[netCount_].ssid = ssid_[i].c_str();
    nets_[netCount_].pass = pass_[i].c_str();
    netCount_++;
  }
  Serial.printf("[CFG] /nocturne.ini: %d key(s), %d wifi net(s)%s\n", applied_,
                netCount_, applied_ ? "" : " - nothing recognised");
  return applied_ > 0;
}
