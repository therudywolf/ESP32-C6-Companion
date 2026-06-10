#include "net/TelemetryClient.h"

#include <ArduinoJson.h>

#include "ui/SceneIds.h"

static void copyStr(char *dst, size_t cap, const char *src) {
  if (!src) src = "";
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

void TelemetryClient::tryConnect(unsigned long now) {
  if (now - lastAttempt_ < NOCT_TCP_RECONNECT_INTERVAL_MS) return;
  lastAttempt_ = now;
  client_.stop();
  client_.setTimeout(NOCT_TCP_CONNECT_TIMEOUT_MS / 1000);
  Serial.printf("[NET] TCP connect %s:%u...\n", host_, port_);
  if (client_.connect(host_, port_)) {
    Serial.println("[NET] TCP connected");
    tcpConnected_ = true;
    firstData_ = false;
    connectTime_ = now;
    lastUpdate_ = now;
    lineLen_ = 0;
    lastSentScreen_ = -1;
    client_.print("HELO\n");
  } else {
    tcpConnected_ = false;
  }
}

void TelemetryClient::sendScreen(int n) {
  if (!tcpConnected_ || n == lastSentScreen_) return;
  lastSentScreen_ = n;
  client_.printf("screen:%d\n", n);
}

void TelemetryClient::sendCmd(const char *cmd) {
  if (!tcpConnected_) return;
  client_.printf("cmd:%s\n", cmd);
}

bool TelemetryClient::signalLost(unsigned long now) const {
  if (!tcpConnected_) return true;
  if (!firstData_) return (now - connectTime_) > NOCT_SIGNAL_GRACE_MS;
  return (now - lastUpdate_) > NOCT_SIGNAL_TIMEOUT_MS;
}

void TelemetryClient::tick(unsigned long now, bool wifiUp, AppState &state,
                           Graphs &graphs) {
  if (!wifiUp || !host_) {
    if (tcpConnected_) {
      client_.stop();
      tcpConnected_ = false;
    }
    state.link.tcpConnected = false;
    state.link.signalLost = true;
    return;
  }

  if (tcpConnected_ && !client_.connected()) {
    Serial.println("[NET] TCP dropped");
    client_.stop();
    tcpConnected_ = false;
  }
  if (!tcpConnected_) tryConnect(now);

  /* Drain the socket; parse complete lines. Bounded per tick to keep the
   * frame budget (a full payload is ~2 KB; 4 KB cap is plenty). */
  int budget = NOCT_TCP_LINE_MAX;
  while (tcpConnected_ && client_.available() && budget-- > 0) {
    char c = (char)client_.read();
    if (c == '\n') {
      if (lineLen_ > 0) {
        line_[lineLen_] = '\0';
        parsePayload(line_, lineLen_, state, graphs);
        lastUpdate_ = now;
        firstData_ = true;
      }
      lineLen_ = 0;
    } else if (lineLen_ < NOCT_TCP_LINE_MAX - 1) {
      line_[lineLen_++] = c;
    } else {
      lineLen_ = 0; /* oversized line: drop it whole */
    }
  }

  state.link.tcpConnected = tcpConnected_;
  state.link.signalLost = signalLost(now);
  /* scenes keep showing the last data through reconnects; only a long
   * silence blanks them */
  state.link.dataDead =
      !firstData_ || (now - lastUpdate_) > 30000UL;
}

void TelemetryClient::parsePayload(const char *line, size_t len,
                                   AppState &state, Graphs &graphs) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line, len);
  if (err) {
    Serial.printf("[NET] JSON error: %s\n", err.c_str());
    return;
  }

  HardwareData &hw = state.hw;
  hw.ct = doc["ct"] | hw.ct;
  hw.gt = doc["gt"] | hw.gt;
  hw.cl = doc["cl"] | hw.cl;
  hw.gl = doc["gl"] | hw.gl;
  hw.pw = doc["pw"] | hw.pw;
  hw.cc = doc["cc"] | hw.cc;
  hw.gh = doc["gh"] | hw.gh;
  hw.gv = doc["gv"] | hw.gv;
  hw.gclock = doc["gclock"] | hw.gclock;
  hw.vclock = doc["vclock"] | hw.vclock;
  hw.gtdp = doc["gtdp"] | hw.gtdp;
  hw.ru = doc["ru"] | hw.ru;
  hw.ra = doc["ra"] | hw.ra;
  hw.nd = doc["nd"] | hw.nd;
  hw.nu = doc["nu"] | hw.nu;
  hw.pg = doc["ping"] | (doc["pg"] | hw.pg);
  hw.cf = doc["cf"] | hw.cf;
  hw.s1 = doc["s1"] | hw.s1;
  hw.s2 = doc["s2"] | hw.s2;
  hw.gf = doc["gf"] | hw.gf;
  if (doc["fans"].is<JsonArray>()) {
    JsonArray fans = doc["fans"];
    for (int i = 0; i < NOCT_FAN_COUNT && i < (int)fans.size(); i++)
      hw.fans[i] = fans[i] | 0;
  }
  if (doc["fan_controls"].is<JsonArray>()) {
    JsonArray fc = doc["fan_controls"];
    for (int i = 0; i < NOCT_FAN_COUNT && i < (int)fc.size(); i++)
      hw.fan_controls[i] = fc[i] | 0;
  }
  if (doc["hdd"].is<JsonArray>()) {
    JsonArray hdd = doc["hdd"];
    for (int i = 0; i < NOCT_HDD_COUNT && i < (int)hdd.size(); i++) {
      copyStr(hw.hdd[i].name, sizeof(hw.hdd[i].name),
              hdd[i]["n"] | "");
      hw.hdd[i].used_gb = hdd[i]["u"] | 0.0f;
      hw.hdd[i].total_gb = hdd[i]["tot"] | 0.0f;
      hw.hdd[i].temp = hdd[i]["t"] | 0;
    }
  }
  hw.vu = doc["vu"] | hw.vu;
  hw.vt = doc["vt"] | hw.vt;
  hw.ch = doc["ch"] | hw.ch;
  hw.mb_sys = doc["mb_sys"] | hw.mb_sys;
  hw.mb_vsoc = doc["mb_vsoc"] | hw.mb_vsoc;
  hw.mb_vrm = doc["mb_vrm"] | hw.mb_vrm;
  hw.mb_chipset = doc["mb_chipset"] | hw.mb_chipset;
  hw.dr = doc["dr"] | hw.dr;
  hw.dw = doc["dw"] | hw.dw;

  if (!doc["wt"].isNull() || doc["wd"].is<const char *>()) {
    state.weather.temp = doc["wt"] | state.weather.temp;
    if (doc["wd"].is<const char *>())
      state.weather.desc = (const char *)doc["wd"];
    state.weather.wmoCode = doc["wi"] | state.weather.wmoCode;
    if (!state.weatherReceived)
      Serial.printf("[NET] weather: %s %dC wmo=%d\n",
                    state.weather.desc.c_str(), state.weather.temp,
                    state.weather.wmoCode);
    state.weatherReceived = true;
  }
  if (doc["wf"].is<JsonArray>()) {
    JsonArray wf = doc["wf"];
    state.weather.wfDays = 0;
    for (int i = 0; i < WeatherData::kMaxDays && i < (int)wf.size(); i++) {
      state.weather.wfMin[i] = wf[i][0] | 0;
      state.weather.wfMax[i] = wf[i][1] | 0;
      state.weather.wfCode[i] = wf[i][2] | 0;
      state.weather.wfDays++;
    }
  }

  if (doc["tp"].is<JsonArray>()) {
    JsonArray tp = doc["tp"];
    for (int i = 0; i < 3; i++) {
      if (i < (int)tp.size()) {
        state.process.cpuNames[i] = (const char *)(tp[i]["n"] | "");
        state.process.cpuPercent[i] = tp[i]["c"] | 0;
      } else {
        state.process.cpuNames[i] = "";
        state.process.cpuPercent[i] = 0;
      }
    }
  }
  if (doc["tr"].is<JsonArray>()) {
    JsonArray tr = doc["tr"];
    for (int i = 0; i < 2; i++) {
      if (i < (int)tr.size()) {
        state.process.ramNames[i] = (const char *)(tr[i]["n"] | "");
        state.process.ramMb[i] = tr[i]["r"] | 0;
      } else {
        state.process.ramNames[i] = "";
        state.process.ramMb[i] = 0;
      }
    }
  }

  if (doc["art"].is<const char *>() || doc["trk"].is<const char *>()) {
    state.media.artist = (const char *)(doc["art"] | "");
    state.media.track = (const char *)(doc["trk"] | "");
    state.media.isPlaying = doc["mp"] | false;
    state.media.isIdle = doc["idle"] | false;
    state.media.mediaStatus = (const char *)(doc["media_status"] | "PAUSED");
  }

  if (doc["claude"].is<JsonObject>()) {
    JsonObject c = doc["claude"];
    state.claude.available = c["ok"] | false;
    state.claude.plan = (const char *)(c["plan"] | "");
    state.claude.windowPct = c["win"] | -1;
    state.claude.weeklyPct = c["wk"] | -1;
    state.claude.resetsInMin = c["rst"] | -1;
    state.claude.weeklyResetMin = c["wrst"] | -1;
    state.claude.todayTokens = c["tok"] | 0L;
    state.claude.todayMsgs = c["msg"] | 0;
    state.claude.todayTools = c["tool"] | 0;
    state.claude.date = (const char *)(c["day"] | "");
    state.claude.stale = c["stale"] | false;
  }

  if (doc["events"].is<JsonObject>()) {
    JsonObject e = doc["events"];
    state.events.count = e["n"] | 0;
    copyStr(state.events.top, sizeof(state.events.top), e["top"] | "");
    copyStr(state.events.severity, sizeof(state.events.severity),
            e["sev"] | "");
    copyStr(state.events.text, sizeof(state.events.text), e["txt"] | "");
    JsonArray list = e["list"];
    for (int i = 0; i < EventsData::kMaxList; i++) {
      copyStr(state.events.list[i], sizeof(state.events.list[i]),
              (i < (int)list.size()) ? (const char *)list[i] : "");
    }
  }

  if (doc["forest"].is<JsonObject>()) {
    JsonObject f = doc["forest"];
    state.forest.count = f["n"] | 0;
    state.forest.up = f["up"] | 0;
    JsonArray nodes = f["nodes"];
    for (int i = 0; i < ForestData::kMaxNodes; i++) {
      ForestNode &nd = state.forest.nodes[i];
      if (i < (int)nodes.size()) {
        copyStr(nd.id, sizeof(nd.id), nodes[i]["id"] | "");
        copyStr(nd.name, sizeof(nd.name), nodes[i]["name"] | "");
        copyStr(nd.status, sizeof(nd.status), nodes[i]["st"] | "");
        nd.cpu = nodes[i]["cpu"] | -1;
        nd.ram = nodes[i]["ram"] | -1;
        nd.disk = nodes[i]["disk"] | -1;
        copyStr(nd.extra, sizeof(nd.extra), nodes[i]["extra"] | "");
      } else {
        nd = ForestNode();
      }
    }
  }

  if (doc["svc"].is<JsonObject>()) {
    JsonObject s = doc["svc"];
    state.services.count = s["n"] | 0;
    state.services.up = s["up"] | 0;
    JsonArray list = s["list"];
    for (int i = 0; i < ServiceData::kMaxServices; i++) {
      ServiceEntry &se = state.services.list[i];
      if (i < (int)list.size()) {
        copyStr(se.name, sizeof(se.name), list[i]["name"] | "");
        copyStr(se.status, sizeof(se.status), list[i]["st"] | "");
        se.ms = list[i]["ms"] | -1;
      } else {
        se = ServiceEntry();
      }
    }
  }
  if (doc["dock"].is<JsonObject>()) {
    state.services.dockTotal = doc["dock"]["n"] | 0;
    state.services.dockUp = doc["dock"]["up"] | -1;
  }

  state.pcIdleSec = doc["pidle"] | state.pcIdleSec;
  if (doc["clk"].is<const char *>())
    copyStr(state.pcClock, sizeof(state.pcClock), doc["clk"]);

  /* Remote control: act once per seq change (companion app). */
  if (doc["rc"].is<JsonObject>()) {
    JsonObject rc = doc["rc"];
    long seq = rc["seq"] | -1L;
    if (seq >= 0 && seq != state.rcSeq) {
      state.rcSeq = seq;
      state.rcNew = true;
      state.rcScreen = rc["screen"] | -1;
      state.rcSay = (const char *)(rc["say"] | "");
      state.rcTheme = rc["theme"] | -1;
      state.rcBright = rc["bright"] | -1;
      state.rcChromeR = state.rcChromeG = state.rcChromeB = -1;
      if (rc["chrome"].is<JsonArray>() && rc["chrome"].size() == 3) {
        state.rcChromeR = rc["chrome"][0] | -1;
        state.rcChromeG = rc["chrome"][1] | -1;
        state.rcChromeB = rc["chrome"][2] | -1;
      }
    }
  }

  const char *alert = doc["alert"] | "";
  state.alertActive = (strcmp(alert, "CRITICAL") == 0);
  if (state.alertActive) {
    state.alertTargetScene = sceneFromServerName(doc["target_screen"] | "");
    const char *m = doc["alert_metric"] | "";
    static const char *metrics[] = {"ct", "gt", "cl", "gl", "gv", "ram"};
    state.alertMetric = -1;
    for (int i = 0; i < 6; i++)
      if (strcmp(m, metrics[i]) == 0) state.alertMetric = i;
  } else {
    state.alertMetric = -1;
  }

  graphs.onPayload(hw);
}
