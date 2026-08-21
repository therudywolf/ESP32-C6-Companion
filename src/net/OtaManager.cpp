#include "net/OtaManager.h"

#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>

#include "core/config.h"

/* RFC1918 / loopback literal? Same rule the LLM client uses for its bearer
 * token — a firmware image is even more sensitive than a token. */
static bool privateHost(const char *h) {
  if (strncmp(h, "10.", 3) == 0 || strncmp(h, "192.168.", 8) == 0 ||
      strncmp(h, "127.", 4) == 0)
    return true;
  if (strncmp(h, "172.", 4) == 0) { /* 172.16.0.0/12 */
    int second = atoi(h + 4);
    return second >= 16 && second <= 31;
  }
  return false;
}

bool OtaManager::urlAllowed(const String &url, const char *trustedHost) {
  if (!url.startsWith("http://") && !url.startsWith("https://")) return false;
  int hs = url.indexOf("://");
  if (hs < 0) return false;
  hs += 3;
  int he = url.length();
  for (int i = hs; i < (int)url.length(); i++) {
    char c = url[i];
    if (c == '/' || c == ':' || c == '?') {
      he = i;
      break;
    }
  }
  if (he <= hs) return false;
  String host = url.substring(hs, he);
  /* credentials in the authority (user@host) would let a crafted URL smuggle a
   * different host past a naive prefix check — refuse them outright */
  if (host.indexOf('@') >= 0) return false;
  if (trustedHost && *trustedHost && host == trustedHost) return true;
  return privateHost(host.c_str());
}

void OtaManager::begin(const char *hostname, const char *password,
                       const char *trustedHost) {
  trustedHost_ = trustedHost;
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPort(NOCT_OTA_PORT);
  if (password && *password) ArduinoOTA.setPassword(password);
  /* No mDNS: it costs a task and RAM on a 512 KB part, and the СИСТЕМА screen
   * already shows the IP to point --upload-port at. */
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([this]() {
    active_ = true;
    progress(0, "приём прошивки");
  });
  ArduinoOTA.onProgress([this](unsigned int done, unsigned int total) {
    progress(total ? (int)((uint64_t)done * 100 / total) : 0, "приём прошивки");
  });
  ArduinoOTA.onEnd([this]() { progress(100, "готово, перезапуск"); });
  ArduinoOTA.onError([this](ota_error_t e) {
    active_ = false;
    pct_ = -1;
    msg_ = String("ошибка OTA ") + (int)e;
    Serial.printf("[OTA] error %d\n", (int)e);
  });
}

void OtaManager::progress(int pct, const char *msg) {
  pct_ = pct;
  msg_ = msg;
  if (ui_) ui_(pct, msg);
}

void OtaManager::tick(bool wifiUp) {
  if (!wifiUp) return;
  if (!started_) {
    ArduinoOTA.begin();
    started_ = true;
    Serial.printf("[OTA] push listener on :%d\n", NOCT_OTA_PORT);
  }
  ArduinoOTA.handle();
  if (pendingUrl_.length()) {
    String url = pendingUrl_;
    pendingUrl_ = "";
    runPull(url);
  }
}

bool OtaManager::requestPull(const String &url) {
  if (!urlAllowed(url, trustedHost_)) {
    msg_ = "OTA: адрес отклонён";
    Serial.printf("[OTA] refused url '%s'\n", url.c_str());
    return false;
  }
  pendingUrl_ = url; /* executed from tick(), i.e. on the loop task */
  return true;
}

void OtaManager::runPull(const String &url) {
  Serial.printf("[OTA] pull %s\n", url.c_str());
  active_ = true;
  progress(0, "загрузка прошивки");

  HTTPClient http;
  if (!http.begin(url)) {
    active_ = false;
    pct_ = -1;
    msg_ = "OTA: не открыть URL";
    return;
  }
  http.setTimeout(20000);
  http.setConnectTimeout(5000);
  int code = http.GET();
  int len = http.getSize();
  if (code != 200 || len <= 0) {
    http.end();
    active_ = false;
    pct_ = -1;
    msg_ = String("OTA: HTTP ") + code;
    Serial.printf("[OTA] pull HTTP %d len %d\n", code, len);
    return;
  }
  if (!Update.begin((size_t)len)) {
    http.end();
    active_ = false;
    pct_ = -1;
    msg_ = "OTA: мало места";
    Serial.println("[OTA] Update.begin failed (partition too small?)");
    return;
  }

  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;
  unsigned long lastData = millis();
  while (written < (size_t)len) {
    size_t avail = s->available();
    if (avail) {
      int n = s->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n <= 0) break;
      if (Update.write(buf, n) != (size_t)n) break;
      written += n;
      lastData = millis();
      progress((int)((uint64_t)written * 100 / (size_t)len), "загрузка прошивки");
    } else {
      if (millis() - lastData > 15000) break; /* stalled */
      delay(1);
    }
  }
  http.end();

  if (written != (size_t)len || !Update.end(true)) {
    Update.abort();
    active_ = false;
    pct_ = -1;
    msg_ = "OTA: образ повреждён";
    Serial.printf("[OTA] pull failed at %u/%d\n", (unsigned)written, len);
    return;
  }
  progress(100, "готово, перезапуск");
  Serial.println("[OTA] pull ok — restarting");
  delay(400);
  esp_restart();
}
