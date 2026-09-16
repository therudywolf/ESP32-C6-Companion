#include "storage/CardConfig.h"

#include "storage/SdStore.h"

/* "ssid2" -> 2, "ssid" -> 1; 0 when the key is not indexed. */
/* Index out of a numbered key: "ssid" and "ssid1" both mean 1, "ssid2" means
 * 2, and 0 means the key does not belong to `base` or names an index out of
 * range.
 *
 * The prefix length is MEASURED rather than passed in. It used to be a
 * parameter, and the zigbee caller passed 5 for "name" - which is four
 * letters - reading the argument as "the highest index allowed". With
 * baseLen 5 every key from name1 to name9 has exactly that length, so all of
 * them took the "bare key, no number at all" branch and returned 1: name2
 * and name3 both landed in slot 0 and whichever was read last won. The one
 * real sensor was silently renamed, and the panel then showed it twice -
 * once under the name the server had learned before, once under the new one,
 * with identical readings because it was one device all along.
 *
 * So the two things that were conflated are now separate: the length is
 * derived, and the bound is the caller's to state. */
static int keyIndex(const String &key, const char *base, int maxN) {
  if (!key.startsWith(base)) return 0;
  int baseLen = (int)strlen(base);
  if ((int)key.length() == baseLen) return 1;
  int n = key.substring(baseLen).toInt();
  return (n >= 1 && n <= maxN) ? n : 0;
}

void CardConfig::apply(const String &section, const String &key,
                       const String &val) {
  if (section == "wifi") {
    int i = keyIndex(key, "ssid", NOCT_WIFI_MAX_NETS);
    if (i) {
      ssid_[i - 1] = val;
      applied_++;
      return;
    }
    i = keyIndex(key, "pass", NOCT_WIFI_MAX_NETS);
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
    else if (key == "token") { token_ = val; applied_++; }
    /* The fallback hub. Checked BEFORE "host" would match — it does not,
       because the comparison is exact, but the order is kept explicit so a
       future prefix match cannot swallow it. */
    else if (key == "host2") { host2_ = val; applied_++; }
    else if (key == "port2") { port2_ = (uint16_t)val.toInt(); applied_++; }
    else if (key == "token2") { token2_ = val; applied_++; }
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
    int i = keyIndex(key, "name", 5);
    if (i && i <= 5) {
      zbName_[i - 1] = val;
      applied_++;
    }
    return;
  }
  if (section == "wolf" || section == "pet") {
    if (key == "skin") { skin_ = val; applied_++; }
    else if (key == "name") { petName_ = val; applied_++; }
    return;
  }
}

bool CardConfig::load(SdStore *sd) {
  if (!sd || !sd->ok()) return false;
  String text;
  /* One read of the whole file. It is a handful of lines; readAll caps it so a
   * junk file cannot blow the heap. */
  /* 4096 и ГОЛОВА файла, а не хвост. Раньше стояло 2048 с хвостовым чтением:
     конфиг больше двух килобайт молча терял начало — заголовок [wifi] и
     первую сеть вместе с ним. Пример на карте в examples/ весит 2292 байта,
     то есть попадал под это сразу, и признаком было только «ничего не
     распозналось». Перезапись секций читает под 4096 — теперь оба предела
     совпадают, иначе запись роняла бы то, чего не увидело чтение. */
  if (!sd->readAll("/nocturne.ini", text, 4096, false) || text.length() == 0)
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

/* Rewrite only the [zigbee] section, preserving every other line of the file
 * byte for byte. A config file the owner hand-edited must not be reformatted
 * by a rename: their comments, their key order and their wifi passwords all
 * stay exactly as typed. If there is no file yet, one is created containing
 * nothing but this section, which leaves secrets.h in charge of everything
 * else - an absent key means "not configured here", not "configured empty". */
bool CardConfig::rewriteSection(SdStore *sd, const char *section,
                                const String &body) {
  if (!sd) return false;
  String text;
  /* Та же голова, что и в load(): перезапись секции не имеет права видеть
     файл иначе, чем его видел разбор, иначе она допишет к тому, чего не
     читала, и потеряет то, что читала. */
  if (!sd->readAll("/nocturne.ini", text, 4096, false)) text = "";

  String want(section);
  want.toLowerCase();

  /* Copy every line that is not inside the target section. */
  String out;
  bool inTarget = false;
  int pos = 0;
  while (pos <= (int)text.length()) {
    int nlAt = text.indexOf('\n', pos);
    String line = nlAt < 0 ? text.substring(pos) : text.substring(pos, nlAt);
    String t = line;
    t.trim();
    if (t.startsWith("[")) {
      String sec = t.substring(1, t.indexOf(']'));
      sec.toLowerCase();
      inTarget = (sec == want);
    }
    if (!inTarget && (nlAt >= 0 || t.length())) {
      out += line;
      out += "\n";
    }
    if (nlAt < 0) break;
    pos = nlAt + 1;
  }

  /* Then append it, rebuilt from what is in memory. Unconditionally: whether
   * the file had this section before does not change what it must contain
   * after. */
  out += "[";
  out += section;
  out += "]\n";
  out += body;
  return sd->writeBlob("/nocturne.ini", out.c_str(), out.length());
}

/* rewriteSection replaces the WHOLE [server] block, so the body has to carry
   every key the section owns — including the ones this particular call is not
   changing. Building it in one place is the only way that stays true: with two
   endpoints written by two functions, the obvious version of setServer() would
   silently delete the fallback, and the board would look fine until the day
   the PC slept. */
String CardConfig::serverSection() const {
  String body = "host=" + host_ + "\n";
  if (port_) body += "port=" + String(port_) + "\n";
  if (panelPort_) body += "panel=" + String(panelPort_) + "\n";
  if (token_.length()) body += "token=" + token_ + "\n";
  if (host2_.length()) body += "host2=" + host2_ + "\n";
  if (port2_) body += "port2=" + String(port2_) + "\n";
  if (token2_.length()) body += "token2=" + token2_ + "\n";
  return body;
}

bool CardConfig::setServer(SdStore *sd, const String &host, uint16_t port,
                           const String &token) {
  host_ = host;
  host_.trim();
  if (port) port_ = port;
  token_ = token;
  token_.trim();
  bool ok = rewriteSection(sd, "server", serverSection());
  Serial.printf("[CFG] server -> %s:%u%s%s\n", host_.c_str(), (unsigned)port_,
                token_.length() ? " (with token)" : "",
                ok ? " (saved)" : " (NOT saved)");
  return ok;
}

bool CardConfig::setFallback(SdStore *sd, const String &host, uint16_t port,
                             const String &token) {
  host2_ = host;
  host2_.trim();
  port2_ = port;              /* 0 here means "same port as the primary" */
  token2_ = token;
  token2_.trim();
  bool ok = rewriteSection(sd, "server", serverSection());
  if (host2_.length())
    Serial.printf("[CFG] запасной хаб -> %s:%u%s%s\n", host2_.c_str(),
                  (unsigned)(port2_ ? port2_ : port_),
                  token2_.length() ? " (с токеном)" : "",
                  ok ? " (записано)" : " (НЕ записано)");
  else
    Serial.printf("[CFG] запасной хаб убран%s\n",
                  ok ? " (записано)" : " (НЕ записано)");
  return ok;
}

bool CardConfig::setZbName(SdStore *sd, int i, const String &name) {
  if (i < 0 || i >= 5) return false;
  zbName_[i] = name;
  zbName_[i].trim();
  String body;
  for (int k = 0; k < 5; k++) {
    if (!zbName_[k].length()) continue;
    body += "name";
    body += String(k + 1);
    body += "=";
    body += zbName_[k];
    body += "\n";
  }
  bool ok = rewriteSection(sd, "zigbee", body);
  Serial.printf("[CFG] slot %d renamed to '%s'%s\n", i + 1,
                zbName_[i].c_str(), ok ? " (saved)" : " (NOT saved)");
  return ok;
}

