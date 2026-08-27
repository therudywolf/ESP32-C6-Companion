/*
 * Nocturne C6 — wolf companion for Waveshare ESP32-C6-LCD-1.47.
 * Port of Nocturne OS PC Companion: color Flipper-style UI, LLM-backed
 * wolf pet, wire-compatible telemetry client.
 *
 * Concurrency: single main loop owns ALL SPI (LCD + SD). The LLM task does
 * network only. WiFi/lwIP run in their own IDF tasks.
 */
#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <esp_core_dump.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_phy_init.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "secrets.h"

#include "core/Graphs.h"
#include "core/HourHistory.h"
#include "core/SettingsStore.h"
#include "core/Types.h"
#include "core/config.h"
#include "input/Button.h"
#include "led/StatusLed.h"
#include "net/ForzaManager.h"
#include "net/LlmClient.h"
#include "net/CoverClient.h"
#include "net/LiteClient.h"
#include "net/ClimateAlert.h"
#include "net/ZbHub.h"
#include "net/TelemetryClient.h"
#include "net/WifiManager.h"
#include "pet/Achievements.h"
#include "pet/PetBrain.h"
#include "pet/PhraseCache.h"
#include "pet/WolfPet.h"
#include "pet/wolf_sprites.h"
#include "storage/Archive.h"
#include "storage/CardConfig.h"
#include "core/Barometer.h"
#include "core/ClimateAnalysis.h"
#include "storage/ClimateLog.h"
#include "storage/SdStore.h"
#include "ui/Display.h"
#include "ui/SceneManager.h"


static const WifiCred kWifiNets[] = WIFI_NETWORKS;
static const int kWifiCount = sizeof(kWifiNets) / sizeof(kWifiNets[0]);
static const char *kLlmEndpoints[] = LLM_ENDPOINTS;
static const int kLlmCount = sizeof(kLlmEndpoints) / sizeof(kLlmEndpoints[0]);

static Display display;
static WifiManager wifi;
static TelemetryClient tcp;
static CoverClient coverClient;
static LiteClient liteClient;
static ForzaManager forza;
static LlmClient llm;
static SdStore sd;
static PhraseCache phrases;
static WolfPet pet;
static PetBrain brain;
static StatusLed led;
static Graphs graphs;
static Histories histories;
static AppState state;
static SceneManager sceneMgr;
static ZbHub zb;
static ClimateAlert climate;
static CardConfig cardCfg;
static Archive archive;
static ClimateLog climateLog;
static ClimateLog::Series climateSeries;
static bool climateSeriesOk = false;
static Achievements ach;

/* What the board actually uses, after /nocturne.ini has had its say. secrets.h
 * supplies the defaults; the card overrides key by key. */
static const WifiCred *activeNets = kWifiNets;
static int activeNetCount = kWifiCount;
static const char *activeHost = PC_IP;
static uint16_t activePort = TCP_PORT;
static const char *llmEps[8];
static int llmEpCount = 0;
static InputSystem *input = nullptr;
static IntervalTimer frameTimer(NOCT_FRAME_MS);
static bool prevTcpConnected = false;
static bool shotFromConsole = false; /* who asked, so the reply goes back */
/* Accumulated inside the frame block, drained once a second into boardLoad. */
static unsigned long frameBusyUs = 0;
static int frameCount = 0;

/* One place decides what the backlight is: the user setting, folded with the
 * screensaver dim and quiet hours. Every other path just changes an input to
 * this — otherwise the menu, the RC block and the dim timer each drive the PWM
 * and the last writer wins at random. */
static void applyBacklight(bool dimmed, bool night) {
  int b = state.settings.brightness;
  if (dimmed && b > 90) b = 90;
  if (night && b > NOCT_NIGHT_BRIGHT) b = NOCT_NIGHT_BRIGHT;
  /* Thermal guard. The backlight is the biggest heat source on the board and
   * the reason for the NOCT_BRIGHT_MAX cap; now that the die temperature is
   * actually known, the cap can react instead of being a fixed guess. */
  static bool wasWarm = false;
  bool warm = state.boardTemp >= NOCT_BOARD_WARM_C;
  if (state.boardTemp >= NOCT_BOARD_HOT_C) {
    if (b > NOCT_BOARD_HOT_BRIGHT) b = NOCT_BOARD_HOT_BRIGHT;
  } else if (warm) {
    if (b > NOCT_BOARD_WARM_BRIGHT) b = NOCT_BOARD_WARM_BRIGHT;
  }
  if (warm != wasWarm) {
    wasWarm = warm;
    Serial.printf("[THERM] board %.1fC - backlight %s\n", state.boardTemp,
                  warm ? "limited" : "released");
  }
  /* A temporary override wins over the stored setting, and only while it
   * lasts — see Display::forceFor. */
  if (display.forcing()) b = display.forcedValue();
  /* The cap is part of the comparison, not just the value. Without it the
   * cached `applied` never changes when an override EXPIRES, so the panel
   * would keep the raised PWM for good and the automatic revert would be a
   * comment describing something that does not happen. */
  static int applied = -1;
  static int appliedCap = -1;
  int cap = (int)display.capNow();
  if (b != applied || cap != appliedCap) {
    applied = b;
    appliedCap = cap;
    display.setBrightness((uint8_t)b);
  }
  /* Publish what the backlight is ACTUALLY doing, so the panel can stop
   * guessing from the stored setting. The slider says what was asked for;
   * these say what happened to it — night mode, the idle dim, the thermal
   * guard and a running override all silently override the request, and
   * without this the owner sees 210 on screen and 90 on the glass with
   * nothing to connect the two. */
  state.blNow = (uint8_t)b;
  state.blCap = (uint8_t)cap;
  state.blThermal = state.boardTemp >= NOCT_BOARD_HOT_C    ? 2
                    : state.boardTemp >= NOCT_BOARD_WARM_C ? 1
                                                           : 0;
  state.blForceLeft = display.forcing()
                          ? (int)((display.forcedUntilMs() - millis()) / 1000UL)
                          : 0;
}

/* "YYYY-MM-DD" / "HH:MM" from the NTP clock; empty string when it has not
 * synced yet, which every caller treats as "don't write a dated record". */
static bool clockParts(char *date, size_t dcap, char *hm, size_t hcap) {
  time_t t = time(nullptr);
  struct tm tmv;
  if (t < 1700000000L || !localtime_r(&t, &tmv)) return false;
  if (date) strftime(date, dcap, "%Y-%m-%d", &tmv);
  if (hm) strftime(hm, hcap, "%H:%M", &tmv);
  return true;
}

/* One CSV row per minute in /logs/YYYY-MM-DD.csv. The in-RAM windows reach back
 * an hour and a day; this reaches back as far as the card does — ~30 B/row is
 * 43 KB a day, 16 MB a year, against 7.4 GB of card. That turns the board from
 * a monitor into a monitor with an archive: week-long graphs, day-to-day
 * comparisons, hardware slowly getting hotter. */
static void logTelemetryRow(int ct, int gt, int cl, int gl, int ram) {
  if (!sd.ok()) return;
  char date[12], hm[8];
  if (!clockParts(date, sizeof(date), hm, sizeof(hm))) return;
  char path[32];
  snprintf(path, sizeof(path), "/logs/%s.csv", date);
  if (!sd.exists(path)) /* a fresh day starts with its own header */
    sd.enqueueAppend(path, "time,cpu_c,gpu_c,cpu_pct,gpu_pct,ram_pct");
  char row[48];
  snprintf(row, sizeof(row), "%s,%d,%d,%d,%d,%d", hm, ct, gt, cl, gl, ram);
  sd.enqueueAppend(path, row);
  /* Same sample, second consumer: the rollup that turns a month of rows into
   * "your GPU idles hotter than it used to". */
  archive.onMinute(date, ct, gt, cl, gl, ram);
}

/* One line per boot in /logs/boot.jsonl. The reset reason and the counters
 * already live in NVS, but that is a single number: this is the timeline, which
 * is what tells a one-off self-heal apart from a reboot loop. Written once the
 * clock is real, so the record can be placed in time. */
static void logBootRecord() {
  if (!sd.ok()) return;
  char date[12], hm[8];
  if (!clockParts(date, sizeof(date), hm, sizeof(hm))) return;
  char line[160];
  snprintf(line, sizeof(line),
           "{\"t\":\"%s %s\",\"v\":\"%s\",\"reason\":\"%s\",\"boot\":%lu,"
           "\"faults\":%lu,\"heap\":%u,\"sd_hz\":%lu}",
           date, hm, NOCT_VERSION, state.boot.reasonText,
           (unsigned long)state.boot.bootCount,
           (unsigned long)state.boot.faultCount,
           (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned long)sd.clockHz());
  sd.enqueueAppend("/logs/boot.jsonl", line, NOCT_SD_DIARY_MAX);
}

/* Dump the framebuffer to /shots/NNN.bmp. A 16-bit BI_BITFIELDS BMP opens on
 * any machine with no conversion step, which is the whole point — the board is
 * usually the only thing that can see its own screen, and "what does it look
 * like right now" was previously unanswerable without a camera.
 *
 * Pixels come out through readPixel() rather than straight off the sprite
 * buffer: LovyanGFX stores 16-bit sprites in the panel's byte order, and
 * readPixel normalises that. 55k calls is slow, but this is a one-shot the
 * owner asked for, not something in the frame path. */
static bool saveScreenshot() {
  if (!sd.ok()) return false;
  char path[32];
  int idx = 0;
  do {
    snprintf(path, sizeof(path), "/shots/%03d.bmp", ++idx);
  } while (idx < 999 && sd.exists(path));

  const int W = NOCT_W, H = NOCT_H;
  const uint32_t rowBytes = (uint32_t)W * 2;      /* 640, already 4-aligned */
  const uint32_t pixBytes = rowBytes * H;
  const uint32_t offBits = 14 + 40 + 12;          /* file + info + RGB masks */
  uint8_t hdr[offBits] = {0};
  auto put16 = [&](int o, uint16_t v) { hdr[o] = v; hdr[o + 1] = v >> 8; };
  auto put32 = [&](int o, uint32_t v) {
    hdr[o] = v; hdr[o + 1] = v >> 8; hdr[o + 2] = v >> 16; hdr[o + 3] = v >> 24;
  };
  hdr[0] = 'B'; hdr[1] = 'M';
  put32(2, offBits + pixBytes);
  put32(10, offBits);
  put32(14, 40);                 /* BITMAPINFOHEADER */
  put32(18, W);
  put32(22, H);                  /* positive = bottom-up rows */
  put16(26, 1);
  put16(28, 16);
  put32(30, 3);                  /* BI_BITFIELDS */
  put32(34, pixBytes);
  put32(54, 0xF800);             /* R mask */
  put32(58, 0x07E0);             /* G */
  put32(62, 0x001F);             /* B */

  sd.syncBusNow();
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SHOT] cannot create %s\n", path);
    return false;
  }
  f.write(hdr, sizeof(hdr));
  static uint16_t row[NOCT_W];   /* 640 B, static: the heap is tight */
  for (int y = H - 1; y >= 0; y--) {
    for (int x = 0; x < W; x++) row[x] = display.fb.readPixel(x, y);
    f.write((const uint8_t *)row, rowBytes);
  }
  f.close();
  Serial.printf("[SHOT] %s (%lu B)\n", path, (unsigned long)(offBits + pixBytes));
  return true;
}

/* Every track the owner plays, once, in /logs/media.csv. The data is already
 * arriving every second; keeping it costs a line and turns into "what did I
 * listen to this week". */
static void logTrack(const String &artist, const String &track) {
  if (!sd.ok() || !track.length()) return;
  char date[12], hm[8];
  if (!clockParts(date, sizeof(date), hm, sizeof(hm))) return;
  String line = String(date) + "," + hm + ",\"" + artist + "\",\"" + track + "\"";
  if (!sd.exists("/logs/media.csv"))
    sd.enqueueAppend("/logs/media.csv", "date,time,artist,track");
  sd.enqueueAppend("/logs/media.csv", line);
  ach.bump(Achievements::ACH_TRACK);
}

/* Forza laps. The game streams at 60 Hz and every lap time was thrown away
 * when the session ended — the summary card was computed live and lost. One
 * row per completed lap makes a personal best that outlives the session, which
 * is the thing a racing HUD is actually for. */
static float forzaBest = 0; /* seconds, 0 = unknown */

static void loadForzaBest() {
  if (!sd.ok()) return;
  String txt;
  if (sd.readAll("/forza/best.txt", txt, 32)) forzaBest = txt.toFloat();
  if (forzaBest > 0) Serial.printf("[FORZA] personal best %.3f s\n", forzaBest);
}

static void logForzaLap(float seconds) {
  if (!sd.ok() || seconds <= 0.5f) return;
  char date[12], hm[8];
  if (!clockParts(date, sizeof(date), hm, sizeof(hm))) return;
  char line[64];
  snprintf(line, sizeof(line), "%s,%s,%.3f", date, hm, seconds);
  if (!sd.exists("/forza/laps.csv"))
    sd.enqueueAppend("/forza/laps.csv", "date,time,lap_seconds");
  sd.enqueueAppend("/forza/laps.csv", line);
  ach.bump(Achievements::ACH_LAP);
  if (forzaBest <= 0 || seconds < forzaBest) {
    forzaBest = seconds;
    char best[16];
    snprintf(best, sizeof(best), "%.3f", seconds);
    sd.writeBlob("/forza/best.txt", best, strlen(best));
    Serial.printf("[FORZA] new personal best %.3f s\n", seconds);
  }
}

/* A panic leaves an ELF core dump in the coredump partition (the partition
 * table has always had one; nothing ever read it). Copy it to the card and
 * erase it, so a crash can be decoded later with
 *   esp-coredump info_corefile -c <file> .pio/build/nocturne-c6/firmware.elf
 * without anyone having been holding a serial cable at the time. This board
 * reboots itself to heal, so crashes happen when nobody is watching by
 * definition. */
static void saveCoreDump() {
  if (!sd.ok()) return;
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) return;
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!part) return;

  char path[40];
  int idx = 0;
  do {
    snprintf(path, sizeof(path), "/logs/core%02d.elf", ++idx);
  } while (idx < 99 && sd.exists(path));

  sd.syncBusNow();
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  uint8_t buf[512];
  size_t off = 0;
  bool good = true;
  while (off < size) {
    size_t n = size - off > sizeof(buf) ? sizeof(buf) : size - off;
    if (esp_partition_read(part, off, buf, n) != ESP_OK) { good = false; break; }
    if (f.write(buf, n) != n) { good = false; break; }
    off += n;
  }
  f.close();
  if (good) {
    Serial.printf("[CORE] crash dump saved to %s (%u B)\n", path,
                  (unsigned)size);
    /* Erase it: leaving it would copy the same crash again on every boot. */
    esp_core_dump_image_erase();
  } else {
    Serial.println("[CORE] dump copy failed - left in flash for next time");
    sd.remove(path);
  }
}

/* ── USB console ──────────────────────────────────────────────────────────
 * A line-oriented command shell on the same serial port the logs come out of.
 * The board has exactly one button and one screen, so until now the only way
 * to poke at a running device was to change the firmware and reflash — and
 * most of a debugging session is answering questions the board could just be
 * asked. Non-blocking: one character per loop pass at most.
 */
static void consoleHelp() {
  Serial.println(F(
      "commands:\n"
      "  info            state summary (link, heap, temps, card)\n"
      "  ls [dir]        list a card directory (default /logs)\n"
      "  cat <path>      tail a file from the card\n"
      "  ach             achievement counters\n"
      "  scene <n>       jump to scene n\n"
      "  bright <n>      backlight 30..210\n"
      "  theme <n>       theme preset\n"
      "  say <text>      make the wolf say something\n"
      "  feed|play|pet|talk\n"
      "  shot            screenshot to the card\n"
      "  zb [join|reset]  Zigbee coordinator\n"
      "  reboot"));
}

static void consoleExec(String line) {
  line.trim();
  if (!line.length()) return;
  String cmd = line, arg = "";
  int sp = line.indexOf(' ');
  if (sp > 0) {
    cmd = line.substring(0, sp);
    arg = line.substring(sp + 1);
    arg.trim();
  }
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") {
    consoleHelp();
  } else if (cmd == "info") {
    Serial.printf("v%s  up %lus  heap %u KB (min %u)  board %.1fC\n",
                  NOCT_VERSION, millis() / 1000UL,
                  (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getMinFreeHeap() / 1024), state.boardTemp);
    Serial.printf("wifi %s %ddBm  ip %s  tcp %d  payload %lus ago\n",
                  state.link.ssid, state.link.rssi, WiFi.localIP().toString().c_str(),
                  state.link.tcpConnected,
                  tcp.hasData() ? (millis() - tcp.lastPayloadMs()) / 1000UL : 9999UL);
    Serial.printf("sd %d @%lu Hz  scene %d  wolf %s %lud %s\n", sd.ok(),
                  (unsigned long)sd.clockHz(), sceneMgr.currentScene(),
                  pet.stageName(), (unsigned long)pet.ageDays(),
                  pet.statusText());
    Serial.printf("pc ct=%d gt=%d cl=%d gl=%d\n", state.hw.ct, state.hw.gt,
                  state.hw.cl, state.hw.gl);
  } else if (cmd == "ls") {
    sd.logDir(arg.length() ? arg.c_str() : "/logs");
  } else if (cmd == "cat") {
    String body;
    if (arg.length() && sd.readAll(arg.c_str(), body, 2048))
      Serial.printf("--- %s (tail) ---\n%s\n--- end ---\n", arg.c_str(),
                    body.c_str());
    else
      Serial.println("no such file");
  } else if (cmd == "ach") {
    for (int i = 0; i < Achievements::ACH_COUNT; i++) {
      Achievements::Id id = (Achievements::Id)i;
      /* no %-16s: printf pads by BYTES and every label here is 2 B/char */
      Serial.printf("  %s: %lu (lvl %d)\n", Achievements::name(id),
                    (unsigned long)ach.get(id),
                    Achievements::level(id, ach.get(id)));
    }
  } else if (cmd == "scene") {
    sceneMgr.requestScene(arg.toInt());
  } else if (cmd == "bright") {
    int b = arg.toInt();
    if (b >= 30 && b <= NOCT_BRIGHT_MAX) {
      state.settings.brightness = b;
      settings::save(state.settings);
      Serial.printf("brightness %d\n", b);
    } else {
      Serial.println("range 30..210");
    }
  } else if (cmd == "theme") {
    int t = arg.toInt();
    state.settings.themePreset = t % theme::presetTotal();
    state.settings.customActive = false;
    theme::applyPreset(state.settings.themePreset);
    theme::setBgLight(state.settings.bgLight);
    settings::save(state.settings);
    Serial.printf("theme %d (%s)\n", state.settings.themePreset,
                  theme::presetName(state.settings.themePreset));
  } else if (cmd == "say") {
    if (arg.length()) brain.sayNow(arg);
  } else if (cmd == "eat" || cmd == "play" || cmd == "pet" || cmd == "talk") {
    int a = cmd == "eat"    ? WolfPet::ACT_FEED
            : cmd == "play" ? WolfPet::ACT_PLAY
            : cmd == "pet"  ? WolfPet::ACT_PET
                            : WolfPet::ACT_TALK;
    pet.doAction(a);
    brain.onAction(a);
    Serial.printf("%s ok\n", cmd.c_str());
  } else if (cmd == "feed") {
    /* Push a raw payload line through the real parser. Anyone writing a
     * producer for this schema — a Zigbee2MQTT bridge, a Yandex poller — can
     * test against the board with no server in the middle, which is exactly
     * how the lite fallback already injects its own payloads. */
    if (arg.length()) {
      tcp.feedExternal(arg.c_str(), state, graphs);
      Serial.printf("fed %d B; zb.n=%d", arg.length(), state.zb.count);
      for (int i = 0; i < state.zb.count && i < ZigbeeData::kMax; i++)
        Serial.printf(" | %s %.1fC %d%% bat %d%% age %ds",
                      state.zb.list[i].name,
                      state.zb.list[i].temp10 / 10.0f, state.zb.list[i].humidity,
                      state.zb.list[i].battery, state.zb.list[i].ageSec);
      Serial.println();
    }
  } else if (cmd == "mem") {
    Serial.printf("heap %u KB free (min %u), largest block %u B\n",
                  (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getMinFreeHeap() / 1024),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    static const char *tn[] = {"loopTask", "llm", "lite", "cover",
                               "Zigbee_main"};
    for (auto n : tn) {
      TaskHandle_t h = xTaskGetHandle(n);
      if (h)
        Serial.printf("  %-12s stack headroom %u B\n", n,
                      (unsigned)uxTaskGetStackHighWaterMark(h));
    }
  } else if (cmd == "probe") {
    /* Plain HTTP GET to the control panel: a bounded, cheap RX truth-teller.
     * If this returns a code, WiFi receive is alive, whatever else claims. */
    HTTPClient http;
    char url[64];
    snprintf(url, sizeof(url), "http://%s:8899/", activeHost);
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    unsigned long t0 = millis();
    int code = -100;
    if (http.begin(url)) code = http.GET();
    http.end();
    Serial.printf("probe %s -> %d in %lu ms\n", url, code, millis() - t0);
  } else if (cmd == "lite") {
    /* fire the exact DNS+TLS path that used to panic the Zigbee build */
    liteClient.debugFetchNow();
    Serial.println("lite fetch forced - watch for [LITE]");
  } else if (cmd == "zb") {
    if (arg == "join") {
      if (zb.running()) {
        zb.permitJoin(NOCT_ZB_JOIN_SEC);
        Serial.printf("network open %d s - press pair on the sensor\n",
                      NOCT_ZB_JOIN_SEC);
      } else {
        Serial.println("coordinator not up yet (starts once WiFi connects)");
      }
    } else if (arg == "reset") {
      zb.factoryReset();
    } else {
      Serial.printf("coordinator %s, %d sensor(s), %s\n",
                    zb.running() ? "up" : "down", zb.deviceCount(),
                    zb.joining(millis()) ? "OPEN for joining" : "closed");
      for (int i = 0; i < state.zb.count; i++) {
        const ZbSensor &z = state.zb.list[i];
        /* Print what the device MEASURES, not every field of the struct. A
         * motion sensor has no thermometer, and rendering its -32768 marker
         * as "-3276.8C rh -1%" made a healthy sensor look broken — which is
         * exactly the wrong impression for the listing people reach for when
         * they suspect something is broken. */
        Serial.printf("  %s:", z.name);
        if (z.temp10 != -32768) Serial.printf(" %.1fC", z.temp10 / 10.0f);
        if (z.humidity >= 0) Serial.printf(" rh %d%%", z.humidity);
        if (z.pressure > 0) Serial.printf(" %d hPa", z.pressure);
        if (z.battery >= 0) Serial.printf(" bat %d%%", z.battery);
        Serial.printf(" age %ds\n", z.ageSec);
      }
      zb.debugSlots();
      Serial.printf("  local %d of %d listed\n", state.zb.localCount,
                    state.zb.count);
      Serial.println("  zb join | zb reset");
    }
  } else if (cmd == "dump") {
    /* Base64 a file out over the console. The board takes screenshots to the
     * card, and until now the only way to LOOK at one was to power down, pull
     * the card and find a reader - so nobody ever did, and "what does the
     * screen actually look like" stayed unanswerable. 110 KB at 115200 baud is
     * about ten seconds. */
    if (!sd.ok() || !arg.length()) {
      Serial.println("usage: dump /shots/001.bmp  (needs a card)");
    } else {
      sd.syncBusNow();
      File f = SD.open(arg.c_str(), FILE_READ);
      if (!f) {
        Serial.printf("dump: %s not found\n", arg.c_str());
      } else {
        static const char *b64 =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        Serial.printf("---BEGIN %s %u---\n", arg.c_str(), (unsigned)f.size());
        uint8_t in[3];
        char out[5];
        out[4] = 0;
        int col = 0;
        while (true) {
          int n = f.read(in, 3);
          if (n <= 0) break;
          uint32_t v = (uint32_t)in[0] << 16;
          if (n > 1) v |= (uint32_t)in[1] << 8;
          if (n > 2) v |= in[2];
          out[0] = b64[(v >> 18) & 63];
          out[1] = b64[(v >> 12) & 63];
          out[2] = n > 1 ? b64[(v >> 6) & 63] : '=';
          out[3] = n > 2 ? b64[v & 63] : '=';
          Serial.print(out);
          /* Wrapped, so a serial monitor stays usable and no line grows
           * unbounded on the receiving side. */
          if ((col += 4) >= 76) {
            col = 0;
            Serial.println();
          }
          /* The console runs on the render loop, which the watchdog watches. */
          if ((f.position() & 0xFFF) == 0) esp_task_wdt_reset();
        }
        f.close();
        Serial.printf("\n---END %s---\n", arg.c_str());
      }
    }
  } else if (cmd == "baro") {
    /* Feed a barometric tendency straight into the state the alerting reads.
     * The real trend comes off the card and moves on the weather's schedule,
     * which is no schedule at all to test against — this is the same door
     * `feed` opens for the sensor, and it exercises the identical code path
     * rather than a copy of it. The next real recompute overwrites it. */
    if (!arg.length()) {
      Serial.println("usage: baro <десятые гПа за 3ч>, напр. -45");
    } else {
      state.zbPress10Delta3h = arg.toInt();
      state.zbTrendOk = true;
      /* Fill the OTHER windows too, or the analysis screen keeps showing the
       * card's real (calm) numbers while the alerting sees the injected front
       * - which is exactly what the first screenshot of АНАЛИЗ showed, and it
       * makes the injection useless for testing the thing it was aimed at.
       *
       * The synthetic shape is a steady fall: each longer window scales with
       * its length, and the one-hour window matches the three-hour rate so no
       * accelerating/easing pattern fires by accident. Testing a SPECIFIC
       * shape is what `analyse` plus a hand-edited window would be for; this
       * is the plain case. */
      analysis::Windows &w = state.zbWin;
      const int d3 = state.zbPress10Delta3h;
      w.dP10_3h = d3;
      w.dP10_1h = d3 / 3;
      w.dP10_6h = d3 * 2;
      w.dP10_12h = d3 * 4;
      w.dP10_24h = d3 * 8;
      w.okP1 = w.okP3 = w.okP6 = w.okP12 = w.okP24 = true;
      w.okT1 = w.okT3 = w.okH1 = w.okH3 = true;
      {
        const ZbSensor &z0 = state.zb.list[0];
        state.zbDewPoint10 = analysis::dewPoint10(z0.temp10, z0.humidity);
        int hourLocal = -1;
        time_t nowT = time(nullptr);
        if (nowT > 1700000000L) {
          struct tm tmv;
          if (localtime_r(&nowT, &tmv)) hourLocal = tmv.tm_hour;
        }
        state.zbFindCount = analysis::analyse(
            w, z0.temp10, z0.humidity, hourLocal, state.zbPressPct,
            state.zbTempPct, state.zbHumPct, state.zbFind,
            AppState::kMaxFindings);
      }
      /* Push it up the real wire too. The point of this command is to
       * exercise the WHOLE path - board, TCP, server, textfile collector,
       * alert rule - on a synthetic front, because the weather does not
       * arrive on demand. Computing the findings and keeping them on the
       * device would test the half that was never in doubt. */
      tcp.sendZbTrend(state.zbPress10Delta3h, w.dT10_3h, w.dH_3h);
      tcp.sendClimatePatterns(state.zbFind, state.zbFindCount,
                              state.zbDewPoint10, state.zbPressPct,
                              state.zbTempPct, state.zbHumPct,
                              state.zbAbsHum10, w);
      Serial.printf("trend := %+d.%d hPa/3h -> %s (%d pattern(s))\n",
                    state.zbPress10Delta3h / 10,
                    abs(state.zbPress10Delta3h % 10),
                    barometer::forecast(
                        barometer::classify(state.zbPress10Delta3h, 3)),
                    state.zbFindCount);
      for (int i = 0; i < state.zbFindCount; i++)
        Serial.printf("  [%d] %s - %s\n", state.zbFind[i].severity,
                      state.zbFind[i].title, state.zbFind[i].detail);
    }
  } else if (cmd == "home") {
    /* The ДОМ trend window, same three rungs the long press cycles. Here too
     * because "does the card-backed graph read back" is a question worth
     * answering without a finger on the button. */
    sceneMgr.setHomeMode(arg.toInt());
    Serial.printf("home window = %d (0 live, 1 day, 2 week)\n",
                  sceneMgr.homeMode());
  } else if (cmd == "dumpcsv") {
    int days = arg.length() ? arg.toInt() : 7;
    if (days < 1) days = 1;
    if (!climateLog.exportBegin(days)) {
      Serial.println("no archive to upload (no card, or no clock)");
    } else if (!tcp.sendClimateCsvBegin(days)) {
      /* Without the begin line the receiver has no transfer open, and it
       * DISCARDS every row that arrives outside one - silently, by design,
       * because a row with no header is a row it cannot place. So a dropped
       * begin turns the whole upload into nothing while the board cheerfully
       * counts rows it sent into a void. Do not start the walk unless the
       * opening line actually left. */
      climateLog.exportAbort();
      Serial.println("no link to the server yet - nothing uploaded");
    } else {
      Serial.printf("uploading %d day(s) of archive\n", days);
    }
  } else if (cmd == "fontcard") {
    /* Measure the fonts instead of trusting their names — see drawFontCard. */
    long sec = arg.length() ? arg.toInt() : 60;
    if (sec <= 0) {
      sceneMgr.showFontCard(0);
      Serial.println("font card dismissed");
    } else {
      sceneMgr.showFontCard((unsigned long)sec * 1000UL);
      Serial.printf("font card up for %ld s\n", sec);
    }
  } else if (cmd == "testcard") {
    /* The only check this project cannot make on its own: screenshots come
     * from the sprite, and the panel's inversion, RGB order, gamma and
     * backlight all live downstream of it. */
    long sec = arg.length() ? arg.toInt() : 600;
    if (sec == 0) {
      sceneMgr.showTestCard(0);
      Serial.println("test card dismissed");
    } else {
      if (sec < 3) sec = 3;
      if (sec > 3600) sec = 3600;
      sceneMgr.showTestCard((unsigned long)sec * 1000UL);
      Serial.printf("test card up for %ld s (testcard 0 to dismiss) - look at "
                    "the GLASS, not a screenshot\n", sec);
    }
  } else if (cmd == "blmax") {
    /* blmax <211..255> [minutes] - lift the backlight cap temporarily.
     *
     * The cap is there because this panel blooms a dark blob at full PWM,
     * measured once, before the Zigbee radio added ~17 C to the die. Whether
     * 210 is still right is a question about this panel that only the owner's
     * eye can settle, so: real override, automatic revert. A cap left raised
     * and forgotten is how the bloom returns weeks later with nothing to
     * connect it to. */
    int sp = arg.indexOf(' ');
    int cap = (sp > 0 ? arg.substring(0, sp) : arg).toInt();
    long mins = sp > 0 ? arg.substring(sp + 1).toInt() : 15;
    if (mins < 1) mins = 1;
    if (mins > 120) mins = 120;
    if (cap < NOCT_BRIGHT_MAX || cap > 255) {
      Serial.printf("blmax <%d..255> [minutes]  -  current cap %d\n",
                    NOCT_BRIGHT_MAX, display.capNow());
    } else {
      display.forceFor((uint8_t)cap, (unsigned long)mins * 60000UL);
      Serial.printf("backlight forced to %d for %ld min, then back to %d\n",
                    cap, mins, NOCT_BRIGHT_MAX);
      Serial.println("watch for a dark blob spreading from the middle - that "
                     "is the reason the cap exists");
    }
  } else if (cmd == "analyse") {
    /* Print what the analyser currently makes of the room. The card is only
     * re-read every five minutes, so this shows the last verdict rather than
     * forcing a fresh walk on the render loop. */
    const analysis::Windows &w = state.zbWin;
    Serial.printf("dew point %d.%d C | abs humidity %d.%d g/m3\n",
                  state.zbDewPoint10 / 10, abs(state.zbDewPoint10 % 10),
                  state.zbAbsHum10 / 10, abs(state.zbAbsHum10 % 10));
    Serial.printf("percentiles vs own archive: temp %d, humidity %d, "
                  "pressure %d\n", state.zbTempPct, state.zbHumPct,
                  state.zbPressPct);
    Serial.printf("dP 1h %+d 3h %+d 6h %+d 12h %+d 24h %+d (tenths hPa)\n",
                  w.okP1 ? w.dP10_1h : 0, w.okP3 ? w.dP10_3h : 0,
                  w.okP6 ? w.dP10_6h : 0, w.okP12 ? w.dP10_12h : 0,
                  w.okP24 ? w.dP10_24h : 0);
    Serial.printf("dT 1h %+d 3h %+d 6h %+d 24h %+d (tenths C)\n",
                  w.okT1 ? w.dT10_1h : 0, w.okT3 ? w.dT10_3h : 0,
                  w.okT6 ? w.dT10_6h : 0, w.okT24 ? w.dT10_24h : 0);
    Serial.printf("dRH 1h %+d 3h %+d 6h %+d 24h %+d (percent)\n",
                  w.okH1 ? w.dH_1h : 0, w.okH3 ? w.dH_3h : 0,
                  w.okH6 ? w.dH_6h : 0, w.okH24 ? w.dH_24h : 0);
    if (!state.zbFindCount) Serial.println("no patterns match right now");
    for (int i = 0; i < state.zbFindCount; i++)
      Serial.printf("  [%d] %s - %s\n", state.zbFind[i].severity,
                    state.zbFind[i].title, state.zbFind[i].detail);
  } else if (cmd == "zbname") {
    /* zbname <1..5> <text> - name a paired sensor. The name is what makes a
     * motion reading useful: "движение 2 минуты назад" says nothing until you
     * know it was the hallway. Persisted to /nocturne.ini on the card, so it
     * survives both a reboot and a reflash. */
    int sp = arg.indexOf(' ');
    int slot = (sp > 0 ? arg.substring(0, sp) : arg).toInt();
    String nm = sp > 0 ? arg.substring(sp + 1) : String("");
    nm.trim();
    if (slot < 1 || slot > 5) {
      Serial.println("zbname <1..5> <name>  -  empty name clears the slot");
    } else {
      cardCfg.setZbName(&sd, slot - 1, nm);
      for (int i = 1; i <= 5; i++)
        Serial.printf("  %d: %s\n", i, cardCfg.zbName(i - 1));
    }
  } else if (cmd == "snooze") {
    unsigned long sec = arg.length() ? (unsigned long)arg.toInt() : 300;
    if (sec < 1) sec = 1;
    if (sec > 3600) sec = 3600;
    sceneMgr.snoozeAlert(sec * 1000UL);
    Serial.printf("alert takeover snoozed for %lu s\n", sec);
  } else if (cmd == "shot") {
    /* Route through the SAME request the menu uses, so the capture always
     * happens right after a complete draw()+push(). Taking it straight from
     * here read the sprite mid-frame — the console is serviced BEFORE the frame
     * block — and produced torn screenshots with two different frames stacked
     * in one image. Which is a nasty way to be misled: the tool you reach for
     * to check the layout is the one lying about it. */
    shotFromConsole = true;
    sceneMgr.requestShot();
  } else if (cmd == "phy") {
    /* RF calibration lives in NVS and is shared ground between WiFi and
     * 802.15.4. Running the Zigbee stack rewrote it, after which WiFi could
     * still SCAN but never associate — different PHY paths. Erasing it forces
     * a full recalibration on the next boot and touches nothing else, unlike
     * wiping NVS (which would take the wolf with it). */
    esp_err_t e = esp_phy_erase_cal_data_in_nvs();
    Serial.printf("phy calibration erased: %s - rebooting\n",
                  e == ESP_OK ? "ok" : esp_err_to_name(e));
    delay(200);
    esp_restart();
  } else if (cmd == "reboot") {
    Serial.println("restarting");
    delay(100);
    esp_restart();
  } else {
    Serial.printf("unknown: %s (try help)\n", cmd.c_str());
  }
}

static void serviceConsole() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String line = buf;
      buf = "";
      consoleExec(line);
      return; /* one command per pass: never stall the frame on a paste */
    }
    if (buf.length() < 700) buf += c; /* a test payload is not a word */
  }
}

/* Quiet hours, from our own NTP clock. `from > to` wraps past midnight
 * (23 -> 8). Unknown clock = never night: guessing would be worse than not
 * dimming. A recent button press suspends it so you can just use the thing. */
static bool nightNow(unsigned long now) {
  const Settings &s = state.settings;
  if (!s.nightMode) return false;
  if (now - sceneMgr.lastInputMs() < NOCT_NIGHT_WAKE_MS) return false;
  time_t t = time(nullptr);
  if (t < 1700000000L) return false; /* no real clock yet */
  struct tm tmNow;
  if (!localtime_r(&t, &tmNow)) return false;
  int h = tmNow.tm_hour;
  if (s.nightFrom == s.nightTo) return false;
  return (s.nightFrom < s.nightTo) ? (h >= s.nightFrom && h < s.nightTo)
                                   : (h >= s.nightFrom || h < s.nightTo);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n[BOOT] Nocturne C6 v%s\n", NOCT_VERSION);

  settings::readBootInfo(state.boot); /* why we restarted, before anything else */
#ifdef NOCT_ZIGBEE
  /* A boot that FOLLOWS a session in which the coordinator ran cannot
   * associate with WiFi - reproduced on every such boot, six timeouts in a
   * row, all networks. The one cure that works every time is erasing the RF
   * calibration and REBOOTING; erasing without the extra reboot measurably
   * does not help, so whatever the 802.15.4 session leaves behind is not just
   * the NVS data. Do the cure preemptively: the session that starts the
   * coordinator sets a flag, and the next boot restarts itself in its first
   * second - one quick double-boot instead of two minutes of doomed attempts.
   * The 120 s auto-heal in the loop stays as the backstop. */
  {
    Preferences pp;
    pp.begin("nocturne", false);
    if (pp.getBool("zbRan", false)) {
      pp.putBool("zbRan", false);
      pp.end();
      Serial.println("[PHY] post-zigbee boot: erase calibration + quick restart");
      esp_phy_erase_cal_data_in_nvs();
      delay(50);
      esp_restart();
    }
    pp.end();
  }
#endif
  settings::load(state.settings);
  theme::bgStyle = state.settings.bgStyle;
  theme::bgLight = state.settings.bgLight;
  theme::applyPreset(state.settings.themePreset);
  if (state.settings.customActive) theme::applyPalette(state.settings.custom);
  /* What state is this board actually in? A hand-tuned palette whose TEXT role
   * collapsed onto BG, an emptied scene mask or a cleared element mask all look
   * like "the screen shows nothing" while the telemetry underneath is perfectly
   * healthy — and none of it is visible from the outside. Dump it once. */
  {
    const Settings &s = state.settings;
    uint16_t pal[theme::COLOR_ROLES];
    theme::getPalette(pal);
    Serial.printf("[BOOT] cfg: bright=%d theme=%d custom=%d slot=%d "
                  "bg=%d light=%d scnMask=%08lX uiElem=%04X night=%d(%02d-%02d)\n",
                  s.brightness, s.themePreset, s.customActive ? 1 : 0,
                  s.activeSlot, s.bgStyle, s.bgLight ? 1 : 0,
                  (unsigned long)s.sceneMask, (unsigned)s.uiElements,
                  s.nightMode ? 1 : 0, s.nightFrom, s.nightTo);
    Serial.printf("[BOOT] palette BG=%04X chrome=%04X TEXT=%04X DIM=%04X "
                  "PANEL=%04X%s\n",
                  pal[0], pal[1], pal[2], pal[3], pal[4],
                  pal[2] == pal[0] ? "  <-- TEXT == BG, nothing will be legible"
                                   : "");
  }
  Serial.println("[BOOT] settings loaded");

  /* Shared SPI2: claim it through the Arduino driver FIRST so both
   * LovyanGFX (bus_shared) and SD.h coordinate on the same bus. */
  SPI.begin(NOCT_PIN_LCD_SCLK, NOCT_PIN_SD_MISO, NOCT_PIN_LCD_MOSI,
            NOCT_PIN_SD_CS);
  Serial.println("[BOOT] spi up");

  /* SD first, on a virgin bus — card init (CMD0 at 400 kHz) is the touchy
   * part; once initialised it tolerates the shared-bus traffic. */
  state.link.sdOk = sd.begin();
  /* Card overrides before anything reads a network or a host. */
  cardCfg.load(&sd);
  if (cardCfg.wifiCount() > 0) {
    activeNets = cardCfg.wifiNets();
    activeNetCount = cardCfg.wifiCount();
  }
  loadForzaBest();
  archive.begin(&sd);
  climateLog.begin(&sd);
  if (cardCfg.skin()[0]) wolfLoadSkin(&sd, cardCfg.skin());
  /* Card themes must exist before the stored preset index is applied, or a
   * board set to a file theme would fall back to preset 0 on every boot. */
  theme::loadCardThemes(&sd);
  theme::applyPreset(state.settings.themePreset);
  if (state.settings.customActive) theme::applyPalette(state.settings.custom);
  if (cardCfg.host()[0]) activeHost = cardCfg.host();
  if (cardCfg.port()) activePort = cardCfg.port();
  /* A card endpoint goes FIRST and the compiled ones stay as fallbacks, so a
   * wrong line in the ini degrades to the old behaviour instead of muting the
   * wolf. */
  if (cardCfg.llmEndpoint()[0]) llmEps[llmEpCount++] = cardCfg.llmEndpoint();
  for (int i = 0; i < kLlmCount && llmEpCount < (int)(sizeof(llmEps) / sizeof(llmEps[0])); i++)
    llmEps[llmEpCount++] = kLlmEndpoints[i];
  Serial.println("[BOOT] sd phase done");

  /* Framebuffer — the largest single allocation (110 KB). */
  if (!display.begin(state.settings.brightness, state.settings.flipped)) {
    /* a transient OOM can clear on a fresh boot — shout for ~10 s so a late
     * monitor sees it, then reboot instead of hanging forever on this
     * always-mounted device */
    for (int i = 0; i < 10; i++) {
      Serial.println("[BOOT] FATAL: framebuffer alloc failed");
      rgbLedWrite(NOCT_PIN_RGB, 0, 80, 0); /* red (RGB-ordered LED) */
      delay(500);
      rgbLedWrite(NOCT_PIN_RGB, 0, 0, 0);
      delay(500);
    }
    esp_restart();
  }
  Serial.printf("[BOOT] display up, free heap %u KB\n",
                (unsigned)(ESP.getFreeHeap() / 1024));

  /* From here on the LCD shares SPI2 with the card, and its framebuffer push is
   * a DMA transfer that outlives the call. Every SD access must drain it first
   * or the card's chip-select lands mid-transfer ("Select Failed") and the card
   * behaves as if it were read-only. */
  sd.setBusSync([] { display.syncBus(); });

  led.begin();
  led.setEnabled(state.settings.ledEnabled);
  input = new InputSystem(NOCT_PIN_BUTTON);

  pet.begin();
  ach.begin();
  histories.attach(&sd); /* graphs survive a reboot when the card is present */
  histories.setOnCommit(logTelemetryRow); /* ...and the card keeps the archive */
  phrases.begin(&sd);
  llm.begin(llmEps, llmEpCount, cardCfg.llmKey()[0] ? cardCfg.llmKey() : LLM_API_KEY,
            cardCfg.llmModel()[0] ? cardCfg.llmModel() : LLM_MODEL,
            cardCfg.llmModel()[0] ? cardCfg.llmModel() : LLM_MODEL_BIG);
  brain.begin(&pet, &llm, &phrases, &sd);

  wifi.begin(activeNets, activeNetCount, state.settings.netSel);
  tcp.setServer(activeHost, activePort);
  /* Zigbee starts LATER, from the loop, once WiFi has associated. Espressif's
   * own WiFi+Zigbee gateway example brings WiFi up first, and starting the
   * 802.15.4 stack ahead of it left WiFi unable to associate at all on this
   * board. See ZbHub. */
  coverClient.begin(activeHost,
                    cardCfg.panelPort() ? cardCfg.panelPort() : 8899);
#if defined(LITE_URL) && defined(LITE_TOKEN)
  liteClient.begin(LITE_URL, LITE_TOKEN); /* always-on fallback when PC is off */
#endif

  SceneManager::Deps deps{};
  deps.disp = &display;
  deps.wifi = &wifi;
  deps.tcp = &tcp;
  deps.led = &led;
  for (int i = 0; i < activeNetCount && i < NOCT_WIFI_MAX_NETS; i++)
    deps.wifiNames[i] = activeNets[i].ssid;
  deps.wifiCount = activeNetCount;
  sceneMgr.begin(deps);

  /* Card recovery, in the order that matters: a crash dump from the run that
   * just died is copied off before anything can overwrite it, and only then do
   * we consider replacing the firmware. */
  saveCoreDump();

  UiCtx ui{display.fb, state, graphs, pet, brain, millis()};
  sceneMgr.bootAnimation(ui);

  Serial.printf("[BOOT] ready, free heap %u KB\n",
                (unsigned)(ESP.getFreeHeap() / 1024));

  /* Task watchdog: reboot if the main render loop ever wedges (display stall, a
   * mutex deadlock) — this device is always mounted, so a hang must self-heal.
   * ONLY the loop task is watched: the LLM/cover/lite net tasks legitimately
   * block on slow fetches (a cold model load is ~25 s) and would false-trip. */
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = 15000;
  wdt.idle_core_mask = 0;
  wdt.trigger_panic = true;
  /* The core usually starts the TWDT itself, so init() returns INVALID_STATE and
   * we just retune it. That path is normal — but init() logs an ERROR line on
   * the way out ("TWDT already initialized"), which looked like a fault in every
   * boot log. Mute the tag across the probe, then restore it. */
  esp_log_level_set("task_wdt", ESP_LOG_NONE);
  if (esp_task_wdt_init(&wdt) == ESP_ERR_INVALID_STATE)
    esp_task_wdt_reconfigure(&wdt); /* core already started it — just retune */
  esp_log_level_set("task_wdt", ESP_LOG_ERROR);
  esp_task_wdt_add(NULL);
}

void loop() {
  unsigned long now = millis();
  esp_task_wdt_reset(); /* feed the watchdog: the loop is alive */
  static bool settingsDirty = false;
  static unsigned long settingsDirtyAt = 0;

  /* background services */
  wifi.tick(now);
  state.link.wifiConnected = wifi.connected();
  state.link.rssi = wifi.rssi();
  strncpy(state.link.ssid, wifi.ssid(), sizeof(state.link.ssid) - 1);

  /* The board's own temperature. Cheap, no wiring, and the only reading here
   * that is about this device rather than the PC. */
  static unsigned long lastTemp = 0;
  if (now - lastTemp > 2000) {
    lastTemp = now;
    float t = temperatureRead();
    if (t > -40 && t < 150) { /* the API returns nonsense if the sensor is busy */
      state.boardTemp = t;
      if (t > state.boardTempMax) state.boardTempMax = t;
    }
  }

  /* own clock via NTP (MSK) so the time survives the PC being off — overrides
   * the server's "clk" once synced; the status bar/screensaver then run standalone */
  static bool ntpInit = false;
  if (wifi.connected() && !ntpInit) {
    configTime(NOCT_TZ_OFFSET_SEC, NOCT_TZ_DST_SEC, NOCT_NTP_PRIMARY,
               NOCT_NTP_SECONDARY);
    ntpInit = true;
    /* the address you point `--upload-port` at — also on the СИСТЕМА screen,
     * but having it in the boot log means OTA never needs the panel first */
    Serial.printf("[NET] ip %s\n", WiFi.localIP().toString().c_str());
  }
  /* The 802.15.4 stack rewrites the RF calibration WiFi shares, and a bad
   * one survives reflashing: the board scans networks but never associates,
   * indistinguishable from dead hardware. If WiFi has not associated once in
   * two minutes, erase the calibration and reboot - once, guarded by an NVS
   * flag so a genuine outage cannot loop it. */
  static bool phyHealDone = false;
  if (!phyHealDone) {
    if (wifi.connected()) {
      phyHealDone = true;
      Preferences hp;
      hp.begin("nocturne", false);
      if (hp.getBool("phyheal", false)) hp.putBool("phyheal", false);
      hp.end();
    } else if (now > 120000UL) {
      phyHealDone = true;
      Preferences hp;
      hp.begin("nocturne", false);
      bool tried = hp.getBool("phyheal", false);
      if (!tried) {
        hp.putBool("phyheal", true);
        hp.end();
        Serial.println("[PHY] no association in 120 s - erasing RF calibration "
                       "and rebooting (one shot)");
        esp_phy_erase_cal_data_in_nvs();
        delay(200);
        esp_restart();
      }
      hp.end();
    }
  }

  /* The boot record needs a real clock, so it waits for SNTP rather than being
   * written in setup(). One attempt: if the clock never arrives, no record. */
  static bool bootLogged = false;
  if (!bootLogged && ntpInit && clockParts(nullptr, 0, nullptr, 0)) {
    bootLogged = true;
    logBootRecord();
    sd.logDir("/logs"); /* the board is never near a card reader — say what it has */
  }
  if (ntpInit) {
    struct tm tmNow;
    if (getLocalTime(&tmNow, 0))
      snprintf(state.pcClock, sizeof(state.pcClock), "%02d:%02d", tmNow.tm_hour,
               tmNow.tm_min);
  }

  tcp.tick(now, wifi.connected(), state, graphs);
  coverClient.update(state.media.coverTok); /* refetch cover on track change */
  /* Card first: a repeated track is one SD read instead of an 18 KB download,
   * and it works the instant the scene appears. Both calls are loop-task only —
   * the fetch task must never touch SPI. */
  coverClient.serveFromCache(&sd);
  coverClient.storeToCache(&sd);

  /* standalone fallback: when the PC is unreachable, pull weather/forest/
   * services from the always-on lite endpoint and feed the same
   * parser, so those scenes stay alive with the PC off */
  bool pcDown = wifi.connected() && (!tcp.connected() || state.link.signalLost);
  liteClient.tick(now, pcDown);
  static unsigned long liteOkMs = 0;
  if (pcDown) {
    String lite;
    if (liteClient.take(lite)) {
      tcp.feedExternal(lite.c_str(), state, graphs);
      liteOkMs = now;
    }
  }
  /* "running on the fallback" if the PC is down but lite fed us recently */
  state.link.liteActive = pcDown && liteOkMs && (now - liteOkMs < 90000UL);
  if (tcp.connected() && !prevTcpConnected) led.setMode(StatusLed::BLIP_OK);
  prevTcpConnected = tcp.connected();

  /* remote control from the companion app (acted on once per seq change) */
  if (state.rcNew) {
    state.rcNew = false;
    bool persist = false;
    Settings &cfg = state.settings;
    if (state.rcTheme >= 0) {
      theme::applyPreset(state.rcTheme);
      cfg.themePreset = state.rcTheme;
      persist = true;
    }
    if (state.rcChromeR >= 0)
      theme::setChrome(state.rcChromeR, state.rcChromeG, state.rcChromeB);
    if (state.rcAccentR >= 0)
      theme::setAccent(state.rcAccentR, state.rcAccentG, state.rcAccentB);
    if (state.rcBright >= 10) {
      /* clamp BOTH ends before persisting so the stored value matches what
       * load() will produce (floor 30) and the panel/menu agree — otherwise a
       * panel-set 20-29 is saved raw then bumped to 30 on next boot */
      int b = state.rcBright < 30           ? 30
              : state.rcBright > NOCT_BRIGHT_MAX ? NOCT_BRIGHT_MAX
                                               : state.rcBright;
      cfg.brightness = b;
      persist = true; /* applyBacklight() picks it up on the next frame */
    }
    if (state.rcLed >= 0) {
      cfg.ledEnabled = state.rcLed != 0;
      led.setEnabled(cfg.ledEnabled);
      persist = true;
    }
    if (state.rcCarousel != -2) {
      if (state.rcCarousel < 0) {
        cfg.carouselEnabled = false;
      } else {
        cfg.carouselEnabled = true;
        cfg.carouselIntervalSec = state.rcCarousel;
      }
      persist = true;
    }
    if (state.rcPetLlm >= 0) {
      cfg.petLlm = state.rcPetLlm != 0;
      persist = true;
    }
    if (state.rcFlip >= 0) {
      cfg.flipped = state.rcFlip != 0;
      display.setFlipped(cfg.flipped);
      persist = true;
    }
    if (state.rcPin != -2) { /* pinned "home" scene, -1 = the den */
      cfg.pinnedScene = (state.rcPin >= 0 && state.rcPin < SCENE_FORZA)
                            ? state.rcPin
                            : -1;
      persist = true;
    }
    if (state.rcSlot >= 0 && state.rcSlot < 3) {
      cfg.activeSlot = state.rcSlot;
      if (cfg.slotUsed[cfg.activeSlot]) {
        memcpy(cfg.custom, cfg.slot[cfg.activeSlot], sizeof(cfg.custom));
        cfg.customActive = true;
        theme::applyPalette(cfg.custom);
        theme::setBgLight(cfg.bgLight);
      }
      persist = true;
    }
    /* One-shot sensor actions from the panel. Not persisted: "pair now" and
     * "poll now" are events, and re-running them on the next unrelated command
     * would re-open the network behind the owner's back. */
    if (state.rcZbJoin > 0) {
      zb.permitJoin(state.rcZbJoin);
      sceneMgr.toast(zb.running() ? "жду датчик" : "zigbee не запущен");
      state.rcZbJoin = -1;
    }
    if (state.rcZbPoll > 0) {
      bool asked = zb.pollNow();
      sceneMgr.toast(asked ? "опрашиваю датчик" : "некого опрашивать");
      state.rcZbPoll = -1;
    }
    if (state.rcBlMax > 0) {
      /* Same override the console's blmax drives, and the same automatic
       * revert. Refused below the standing cap: raising the ceiling to
       * something lower than it already is would silently do nothing, and a
       * control that does nothing is worse than one that is absent. */
      int v = state.rcBlMax, m = state.rcBlMins;
      if (m < 1) m = 1;
      if (m > 120) m = 120;
      if (v >= NOCT_BRIGHT_MAX && v <= 255) {
        display.forceFor((uint8_t)v, (unsigned long)m * 60000UL);
        char t[48];
        snprintf(t, sizeof(t), "подсветка %d на %d мин", v, m);
        sceneMgr.toast(t);
        Serial.printf("[BL] forced to %d for %d min (panel)\n", v, m);
      } else {
        sceneMgr.toast("вне диапазона");
      }
      state.rcBlMax = -1;
    }
    if (state.rcZbDump > 0) {
      /* Same rule as the console path: the walk starts only once the
       * receiver has been told a transfer is opening. */
      if (!climateLog.exportBegin(state.rcZbDump)) {
        sceneMgr.toast("архив недоступен");
      } else if (!tcp.sendClimateCsvBegin(state.rcZbDump)) {
        climateLog.exportAbort();
        sceneMgr.toast("нет связи с сервером");
      } else {
        sceneMgr.toast("выгружаю архив");
      }
      state.rcZbDump = -1;
    }
    if (state.rcZbInt > 0) {
      /* min = a tenth of the window, so the sensor may still report early on a real
       * change; max = what the owner asked for. */
      zb.setReportInterval(state.rcZbInt / 10 + 1, state.rcZbInt);
      state.rcZbInt = -1;
    }
    if (state.rcZbAlert >= 0) {
      cfg.zbAlert = state.rcZbAlert != 0;
      persist = true;
    }
    /* -1000 means "not in this command": every other value, including 0 and
     * negatives, is a legitimate threshold. */
    if (state.rcZbTempMin > -1000) { cfg.zbTempMin = state.rcZbTempMin; persist = true; }
    if (state.rcZbTempMax > -1000) { cfg.zbTempMax = state.rcZbTempMax; persist = true; }
    if (state.rcZbHumMin > -1000) { cfg.zbHumMin = state.rcZbHumMin; persist = true; }
    if (state.rcZbHumMax > -1000) { cfg.zbHumMax = state.rcZbHumMax; persist = true; }
    if (state.rcZbBattMin > -1000) { cfg.zbBattMin = state.rcZbBattMin; persist = true; }
    if (state.rcNight >= 0) {
      cfg.nightMode = state.rcNight != 0;
      persist = true;
    }
    if (state.rcNightFrom >= 0 && state.rcNightFrom <= 23) {
      cfg.nightFrom = state.rcNightFrom;
      persist = true;
    }
    if (state.rcNightTo >= 0 && state.rcNightTo <= 23) {
      cfg.nightTo = state.rcNightTo;
      persist = true;
    }
    if (state.rcTimeout >= 0) {
      cfg.displayTimeoutSec = state.rcTimeout;
      persist = true;
    }
    if (state.rcBgStyle >= 0) {
      cfg.bgStyle = state.rcBgStyle;
      theme::setBgStyle(state.rcBgStyle);
      persist = true;
    }
    if (state.rcBgLight >= 0) {
      cfg.bgLight = state.rcBgLight != 0;
      theme::setBgLight(cfg.bgLight);
      persist = true;
    }
    /* custom colour edits — snapshot the resulting palette as the saved
     * custom theme so it survives reboot */
    bool colorEdited = false;
    if (state.rcPresetReset == 1) {
      cfg.customActive = false;
      theme::applyPreset(cfg.themePreset);
      theme::setBgLight(cfg.bgLight);
      persist = true;
    }
    if (state.rcColorRole >= 0) {
      theme::setColorRole(state.rcColorRole, state.rcColorR, state.rcColorG,
                          state.rcColorB);
      colorEdited = true;
    }
    if (state.rcHasPalette) {
      theme::applyPalette(state.rcPalette);
      colorEdited = true;
    }
    if (colorEdited) {
      theme::getPalette(cfg.custom);
      cfg.customActive = true;
      persist = true;
    }
    if (state.rcAction.length()) {
      int a = state.rcAction == "feed"  ? WolfPet::ACT_FEED
              : state.rcAction == "play" ? WolfPet::ACT_PLAY
              : state.rcAction == "pet"  ? WolfPet::ACT_PET
                                         : WolfPet::ACT_TALK;
      pet.doAction(a);
      brain.onAction(a);
      led.setMode(StatusLed::BLIP_OK);
    }
    if (state.rcSceneMask >= 0) {
      cfg.sceneMask = (uint32_t)state.rcSceneMask | 1u; /* DEN always visible */
      persist = true;
    }
    if (state.rcWolfChatter >= 0) {
      cfg.wolfChatter = state.rcWolfChatter > 3 ? 3 : state.rcWolfChatter;
      persist = true;
    }
    if (state.rcWolfTone >= 0) {
      cfg.wolfTone = state.rcWolfTone > 3 ? 3 : state.rcWolfTone;
      persist = true;
    }
    if (state.rcNotif >= 0) {
      cfg.notifShow = state.rcNotif != 0;
      persist = true;
    }
    if (state.rcLedMode >= 0) {
      cfg.ledMode = state.rcLedMode > 3 ? 3 : state.rcLedMode;
      persist = true;
    }
    if (state.rcUiElem >= 0) {
      cfg.uiElements = (uint16_t)state.rcUiElem;
      persist = true;
    }
    if (state.rcScreen >= 0) sceneMgr.requestScene(state.rcScreen);
    if (state.rcSay.length()) brain.sayNow(state.rcSay);
    /* defer the NVS write: a live-dragged panel slider fires many rc edges/s;
     * coalesce them into one save ~1.2 s after the last change (flush below) */
    if (persist) {
      settingsDirty = true;
      settingsDirtyAt = now;
    }
    Serial.printf("[RC] seq=%ld screen=%d theme=%d action='%s' say='%s'\n",
                  state.rcSeq, state.rcScreen, state.rcTheme,
                  state.rcAction.c_str(), state.rcSay.c_str());
  }
  if (settingsDirty && now - settingsDirtyAt > 1200) {
    settingsDirty = false;
    settings::save(state.settings); /* coalesced RC settings write */
  }

  /* A sensor spoke for the first time: say so on the glass and let the wolf
   * react - pairing feedback is the whole difference between "is it working?"
   * and knowing. */
  if (zb.takeNewSensor()) {
    sceneMgr.alertCard(SceneManager::AL_SENSOR, "ДАТЧИК",
                       "на связи, показания пошли");
    brain.notice("к тебе привязали новый датчик климата - обнюхай и одобри");
  }

  /* Local sensors go upstream too, so the server — and through it a Yandex
   * skill — sees what the coordinator hears.
   *
   * On CHANGE, not only on a timer. A once-a-minute heartbeat meant that right
   * after pairing the panel showed the new sensor with empty readings for up
   * to a full minute — which reads as "it joined but is broken" at exactly the
   * moment the owner is watching to see whether it worked. The 5 s floor keeps
   * a twitchy reading from becoming a stream, and the 60 s heartbeat still goes
   * out unchanged so the server can tell "quiet" from "gone". */
  static unsigned long lastZbReport = 0;
  static int sentTemp = -32768, sentHum = -1, sentBat = -1, sentCount = -1;
  if (tcp.connected() && state.zb.count > 0) {
    const ZbSensor &z0 = state.zb.list[0];
    bool changed = z0.temp10 != sentTemp || z0.humidity != sentHum ||
                   z0.battery != sentBat || state.zb.count != sentCount;
    unsigned long gap = now - lastZbReport;
    if ((changed && gap > 5000UL) || gap > 60000UL) {
      lastZbReport = now;
      sentTemp = z0.temp10;
      sentHum = z0.humidity;
      sentBat = z0.battery;
      sentCount = state.zb.count;
      /* Only what this coordinator actually heard. `zbs:` means "what the
       * board hears" - that is what the server stores it as and what every
       * consumer downstream assumes. Sending relayed or console-injected
       * entries too made the board echo the server's own data back at it,
       * and a `feed` used for testing became a sensor that outlived the test
       * and sat in the panel as a device that does not exist. */
      for (int i = 0; i < state.zb.localCount; i++)
        tcp.sendZbSensor(state.zb.list[i]);
    }
  }

  /* The card's health, same cadence. Cheap, and it turns "why is the archive
   * short" into a number you can watch climb instead of a discovery made
   * months later. */
  /* The board's own vitals, every 15 s. */
  static unsigned long lastBoardReport = 0;
  if (tcp.connected() && now - lastBoardReport > 15000UL) {
    lastBoardReport = now;
    tcp.sendBoard(state.boardTemp, state.boardTempMax, state.boardLoad,
                  state.boardFps, state.heapFreeKb, state.heapMinKb,
                  state.heapLargestKb, state.uptimeSec, state.cpuMhz,
                  state.link.rssi, state.boot.bootCount, state.boot.faultCount,
                  state.boot.reasonText, state.blNow, state.blCap,
                  state.blThermal, state.blForceLeft);
  }

  /* Hub state upstream, so the panel's "check connection" shows the board's
   * answer rather than the server's guess. */
  static unsigned long lastZbStatus = 0;
  if (tcp.connected() && now - lastZbStatus > 15000UL) {
    lastZbStatus = now;
    tcp.sendZbStatus(zb.running(), zb.channel(), zb.joinSecsLeft(now),
                     zb.deviceCount(), zb.lastHeardSec(now));
  }

  sd.refreshUsage(now); /* self-rate-limited; fills in what the mount missed */
  static unsigned long lastSdReport = 0;
  if (tcp.connected() && now - lastSdReport > 60000UL) {
    lastSdReport = now;
    tcp.sendSdStats(sd.ok(), sd.clockHz(), sd.usedMB(), sd.totalMB(),
                    sd.writes(), sd.slowOps(), sd.failTotal(), sd.queueDepth(),
                    sd.lastOpMs());
  }

  /* report wolf state upstream every 2 s so the companion app can show it */
  static unsigned long lastWolfReport = 0;
  if (tcp.connected() && now - lastWolfReport > 2000) {
    lastWolfReport = now;
    tcp.sendWolf(pet.hunger(), pet.happy(), pet.energy(), pet.mood(),
                 pet.isAlive(), pet.isSleeping(), pet.ageDays());
    tcp.sendCfg(state.settings); /* mirror live settings to the web panel */
  }

  /* new track -> one row in the media log */
  static String lastLoggedTrack;
  if (state.media.track.length() && state.media.track != lastLoggedTrack) {
    lastLoggedTrack = state.media.track;
    logTrack(state.media.artist, state.media.track);
  }

  forza.tick(now, wifi.connected());
  /* a completed lap shows up as a change in lastLap */
  static float lastLoggedLap = 0;
  if (forza.state().lastLap > 0 && forza.state().lastLap != lastLoggedLap) {
    lastLoggedLap = forza.state().lastLap;
    logForzaLap(lastLoggedLap);
  }
  bool forzaLive = forza.connected(now);
  state.forzaLive = forzaLive;

  /* full radio power only while racing (UDP latency); else modem sleep —
   * which the WiFi/802.15.4 coexistence scheme also expects to see */
  static bool wifiFast = false;
  if (forzaLive != wifiFast && wifi.connected()) {
    wifiFast = forzaLive;
    WiFi.setSleep(!wifiFast);
  }

  /* Long-window history. accumulate() is a PER-PAYLOAD sample: calling it every
   * loop iteration (~1 kHz) burned cycles and weighted the minute average by
   * loop rate instead of by data. */
  static unsigned long lastHistPayload = 0;
  if (tcp.connected() && !state.link.signalLost &&
      tcp.lastPayloadMs() != lastHistPayload) {
    lastHistPayload = tcp.lastPayloadMs();
    histories.accumulate(state.hw);
  }
  histories.tick(now);

  pet.tick(now);
  brain.tick(now, state);

  /* achievement counters: the events all pass through here anyway */
  {
    int act = brain.takeActionEvent();
    if (act == WolfPet::ACT_FEED) ach.bump(Achievements::ACH_FEED);
    else if (act == WolfPet::ACT_PLAY) ach.bump(Achievements::ACH_PLAY);
    else if (act == WolfPet::ACT_PET) ach.bump(Achievements::ACH_PET);
    else if (act == WolfPet::ACT_TALK) ach.bump(Achievements::ACH_TALK);
    if (brain.takeJournalWritten()) ach.bump(Achievements::ACH_JOURNAL);
    static bool wasAlive = true;
    if (wasAlive && !pet.isAlive()) ach.bump(Achievements::ACH_FAINT);
    wasAlive = pet.isAlive();
    ach.raise(Achievements::ACH_DAYS, (uint32_t)pet.ageDays());
    ach.tick(now);
  }

  /* The archive closed a day: hand any finding to the wolf to phrase, and ask
   * it to write the day up. Both fire once a day, so the whole thing costs one
   * extra LLM call and one small file. */
  {
    String finding;
    if (archive.takeFinding(finding)) brain.notice(finding);
    static String journaledDay;
    const String &closed = archive.lastDate();
    if (archive.lastSummary().length() && closed.length() &&
        closed != journaledDay && !brain.journalBusy()) {
      journaledDay = closed;
      brain.writeJournal(closed, archive.lastSummary());
    }
  }

  /* tint the LED with the emotional tone of a fresh utterance — a brief accent
   * over the SPEAK glow (derived from the bucket, no extra model output) */
  switch (brain.takeSpeechTone()) {
  case PetBrain::TONE_HAPPY: led.flash(0, 90, 30, 350); break;  /* green */
  case PetBrain::TONE_TENSE: led.flash(160, 35, 0, 350); break; /* red */
  case PetBrain::TONE_LOW:   led.flash(0, 0, 110, 350); break;  /* blue */
  default: break;                                               /* neutral: none */
  }

  /* mood light: alert > forza > thinking > speaking > offline > warm > mood */
  if (state.alertActive) {
    led.setMode(StatusLed::ALERT);
  } else if (forzaLive && forza.state().raceOn) {
    const ForzaState &fz = forza.state();
    static unsigned long lastPosBlip = 0;
    if (fz.posChangeMs && fz.posChangeMs != lastPosBlip) {
      lastPosBlip = fz.posChangeMs; /* green blip on a gain, red on a loss */
      led.flash(fz.posGain > 0 ? 0 : 170, fz.posGain > 0 ? 170 : 0, 0, 650);
    }
    led.setForzaPct(fz.rpmPct());
    led.setMode(StatusLed::FORZA);
  } else if (state.link.llmBusy) {
    led.setMode(StatusLed::LLM);
  } else if (brain.bubbleVisible(now)) {
    led.setMode(StatusLed::SPEAK);
  } else if (!state.link.wifiConnected || !state.link.tcpConnected) {
    led.setMode(StatusLed::NOLINK);
  } else if (state.hw.ct >= 85 || state.hw.gt >= 80 || state.hw.cl >= 90 ||
             state.hw.gl >= 95) {
    led.setMode(StatusLed::WARNP);
  } else {
    uint8_t cr, cg, cb;
    if (state.media.isPlaying && state.settings.ledMode == 0 &&
        coverClient.dominant(cr, cg, cb)) {
      /* Music playing: the desk glows in the colour of the album art. The mood
       * colours below still apply the moment the music stops. */
      led.setMoodColor(cr, cg, cb);
    } else if (!pet.isAlive())
      led.setMoodColor(60, 60, 60); /* gray-out */
    else if (pet.isSleeping())
      led.setMoodColor(10, 10, 80); /* deep night blue */
    else if (pet.mood() == 2)
      led.setMoodColor(252, 238, 10); /* cyber yellow — happy */
    else if (pet.mood() == 1)
      led.setMoodColor(255, 120, 0); /* amber — meh */
    else
      led.setMoodColor(0, 40, 200); /* sad blue */
    led.setMode(StatusLed::BREATHE);
  }
  /* Alarm. One shot per day, and it does all three things this board can do:
   * wakes the screen, flashes the light and gets the wolf to say something. */
  if (cardCfg.alarmMinutes() >= 0) {
    time_t t = time(nullptr);
    struct tm tmv;
    static int firedOn = -1; /* yday, so it cannot fire twice in one day */
    if (t >= 1700000000L && localtime_r(&t, &tmv)) {
      int mins = tmv.tm_hour * 60 + tmv.tm_min;
      if (mins == cardCfg.alarmMinutes() && firedOn != tmv.tm_yday) {
        firedOn = tmv.tm_yday;
        Serial.printf("[ALARM] %02d:%02d\n", tmv.tm_hour, tmv.tm_min);
        sceneMgr.wakeScreen();
        led.flash(0, 160, 40, 4000);
        brain.notice("будильник хозяина зазвонил - подними его и не дай "
                     "выключить");
        sceneMgr.toast("подъем!");
      }
    }
  }

  /* quiet hours: dark LED and a dark panel, but a hardware ALERT still wins */
  bool night = nightNow(now);
  state.link.nightActive = night;
  led.setAmbient(night && !state.alertActive ? 1 /* off */
                                             : state.settings.ledMode);
  led.tick(now);
  applyBacklight(sceneMgr.screenDimmed(), night && !state.alertActive);

  /* Opening the archive view reads the card once, not every frame; leaving and
   * returning re-reads so a day that closed meanwhile shows up. */
  {
    static int lastHistMode = -1;
    int m = sceneMgr.historyMode();
    if (m == 2 && lastHistMode != 2) archive.loadSeries();
    lastHistMode = m;
  }

  serviceConsole();
  /* Bring the coordinator up only after WiFi has a link: with both stacks on
   * one radio, whichever starts first appears to keep it. */
  static bool zbStarted = false;
  if (!zbStarted && wifi.connected()) {
    zbStarted = true;
    zb.begin(&sd, &cardCfg);
  }
  zb.tick(now, state, graphs);
  state.link.zbUp = zb.running();
  state.zbJoinSecs = zb.joinSecsLeft(now);
  /* Thresholds the owner set in the panel; edge-triggered, hysteretic, and
   * deliberately silent while the sensor is stale. */
  climate.tick(now, state, brain, sceneMgr);
  /* One row per genuinely new reading, stamped with the wall clock. A
   * WSDCGQ11LM speaks on change plus roughly hourly, so this is 30-80 rows a
   * day — about 2 KB, under a megabyte a year. */
  if (zb.takeLogDue() && state.zb.count > 0) {
    char date[12], hm[8];
    if (clockParts(date, sizeof(date), hm, sizeof(hm)))
      climateLog.append(date, hm, state.zb.list[0]);
  }

  /* Barometric tendency, recomputed every five minutes. The sensor speaks
   * roughly hourly, so anything faster would re-read the card to learn the
   * same answer — and each read walks the card on the render loop. */
  {
    static unsigned long lastTrend = 0;
    if (state.zb.count > 0 && (lastTrend == 0 || now - lastTrend > 300000UL)) {
      lastTrend = now;
      int dT = 0, dH = 0, dP = 0;
      state.zbTrendOk = climateLog.trend(3, dT, dH, dP);
      state.zbTemp10Delta3h = dT;
      state.zbHumDelta3h = dH;
      state.zbPress10Delta3h = dP;
      if (state.zbTrendOk) {
        tcp.sendZbTrend(dP, dT, dH);
        auto t = barometer::classify(dP, 3);
        Serial.printf("[BARO] 3h: %+d.%d hPa - %s\n", dP / 10, abs(dP % 10),
                      barometer::forecast(t));
      }

      /* The other windows. Three hours is the WMO standard and stays exactly
       * where it was; these sit beside it so the windows can DISAGREE, which
       * is the whole point - a six-hour fall with the last hour already
       * rising is a trough that has passed, and no single window can say
       * that. Five card reads every five minutes; each walks at most two
       * daily files. */
      analysis::Windows &w = state.zbWin;
      w.okP1 = climateLog.trend(1, w.dT10_1h, w.dH_1h, w.dP10_1h);
      w.okT1 = w.okH1 = w.okP1;
      w.dP10_3h = dP;
      w.dT10_3h = dT;
      w.dH_3h = dH;
      w.okP3 = w.okT3 = w.okH3 = state.zbTrendOk;
      /* Every window keeps its temperature and humidity too. They used to be
       * thrown into a junk variable: the card read had already paid for them
       * and the room's own trends were discarded on the floor. */
      w.okP6 = climateLog.trend(6, w.dT10_6h, w.dH_6h, w.dP10_6h);
      w.okT6 = w.okH6 = w.okP6;
      int junkT = 0, junkH = 0;
      w.okP12 = climateLog.trend(12, junkT, junkH, w.dP10_12h);
      w.okP24 = climateLog.trend(24, w.dT10_24h, w.dH_24h, w.dP10_24h);
      w.okT24 = w.okH24 = w.okP24;

      const ZbSensor &z0 = state.zb.list[0];
      state.zbDewPoint10 = analysis::dewPoint10(z0.temp10, z0.humidity);
      /* Thirty days of the board's own readings as the yardstick. Absolute
       * pressure cannot be judged without an altitude nobody entered; the
       * room's own distribution needs no calibration. */
      climateLog.percentiles(30, z0.temp10, z0.humidity, z0.pressure,
                             state.zbTempPct, state.zbHumPct,
                             state.zbPressPct);
      state.zbAbsHum10 = analysis::absHumidity10(z0.temp10, z0.humidity);

      int hourLocal = -1;
      time_t nowT = time(nullptr);
      if (nowT > 1700000000L) {
        struct tm tmv;
        if (localtime_r(&nowT, &tmv)) hourLocal = tmv.tm_hour;
      }
      state.zbFindCount =
          analysis::analyse(w, z0.temp10, z0.humidity, hourLocal,
                            state.zbPressPct, state.zbTempPct, state.zbHumPct,
                            state.zbFind, AppState::kMaxFindings);
      for (int i = 0; i < state.zbFindCount; i++)
        Serial.printf("[PAT] %d %s - %s\n", state.zbFind[i].severity,
                      state.zbFind[i].title, state.zbFind[i].detail);
      tcp.sendClimatePatterns(state.zbFind, state.zbFindCount,
                              state.zbDewPoint10, state.zbPressPct,
                              state.zbTempPct, state.zbHumPct,
                              state.zbAbsHum10, w);
    }
  }

  /* Archive export, a handful of rows per loop iteration.
   *
   * Deliberately NOT a loop that drains the whole walk: a month is a couple
   * of thousand rows, and pushing them all inside one iteration is exactly
   * the blocking uplink that tripped the task watchdog once already. Thirty
   * rows at ~50 bytes is 1.5 KB per tick - one TCP segment - and a month
   * finishes in about a minute while the screen keeps drawing. */
  /* Push the archive up whenever the link comes back.
   *
   * The card keeps recording while the PC sleeps; this process only hears the
   * board while the PC is awake. So every stretch of PC downtime is a hole in
   * the mirror and no hole at all on the card — and until the archive was
   * pulled BY HAND, those hours existed nowhere else. A board that dies with
   * the only copy of its own history is not an archive, it is a single point
   * of failure with a CSV in it.
   *
   * Three days on reconnect covers an ordinary weekend away and costs about
   * 450 rows, three seconds of wire. Longer outages are a manual pull: the
   * board cannot know how long the other end was gone, and guessing large
   * would re-send a month every time the router blinks. The receiver merges
   * by timestamp, so re-sending what it already has is free of consequence -
   * only of a little traffic. */
  static bool wasLinked = false;
  static unsigned long lastAutoDump = 0;
  bool linked = tcp.connected();
  if (linked && !wasLinked && !climateLog.exportActive() &&
      (lastAutoDump == 0 || now - lastAutoDump > 3600000UL)) {
    if (climateLog.exportBegin(3) && tcp.sendClimateCsvBegin(3)) {
      lastAutoDump = now;
      Serial.println("[CSV] link back: mirroring 3 days of archive");
    } else {
      climateLog.exportAbort();
    }
  }
  wasLinked = linked;

  static unsigned long lastPump = 0;
  /* The row in flight, kept across iterations so a refused send can retry it. */
  static char exRow[80] = {0};
  if ((climateLog.exportActive() || exRow[0]) && now - lastPump >= 100) {
    lastPump = now;
    /* Rate limit, not just a burst cap. This block sits in the main loop,
     * which spins at frame rate - without the 100 ms gate the "30 rows per
     * call" ran thirty times a second, buried the socket, and sendLine tore a
     * line in half. Twenty rows per 100 ms is about 10 KB/s: a month of
     * archive in ten seconds, and the TCP buffer never fills. */
    int sent = 0;
    while (sent < 20) {
      /* Fetch only when the previous row is gone. exportNextRow ADVANCES the
       * cursor, so a row handed to a refused send was simply lost: the tick
       * that backed off dropped it and moved on. Holding it here means
       * back-pressure delays the transfer instead of punching holes in it. */
      if (!exRow[0] && !climateLog.exportNextRow(exRow, sizeof(exRow))) break;
      if (!tcp.sendClimateCsvRow(exRow)) break; /* socket full: keep the row */
      exRow[0] = 0;
      sent++;
    }
    state.zbExportRows = climateLog.exportRowsSent();
    state.zbExportLeft = climateLog.exportDaysLeft();
    /* Done only when the walk is finished AND the last row is out. */
    if (!climateLog.exportActive() && !exRow[0]) {
      /* The end line is retried on the same terms as a row. Dropping it
       * leaves the receiver holding an incomplete transfer for good - it
       * discards rather than merges, so the whole upload shows as nothing.
       * That is the state this actually produced: 660 rows sent, a server
       * stuck at "busy" with zero rows stored. */
      if (tcp.sendClimateCsvEnd(state.zbExportRows, true)) {
        Serial.printf("[CSV] archive uploaded: %d row(s)\n",
                      state.zbExportRows);
        sceneMgr.toast("архив выгружен");
        state.zbExportLeft = -1;
      }
    }
  }

  /* input */
  input->setRepeatEnabled(sceneMgr.wantsButtonRepeat());
  ButtonEvent ev = input->update();
  if (ev != EV_NONE) {
    UiCtx ui{display.fb, state, graphs, pet,        brain,
             now,        &forza.state(), forzaLive, &histories,
             coverClient.ready() ? coverClient.data() : nullptr,
             sceneMgr.historyMode(), &archive.series(), archive.seriesDays(),
             &ach};
    sceneMgr.handleInput(ev, ui);
  }

  /* Board vitals, once a second. `busyUs` is accumulated by the frame block
   * below; the ratio against a real elapsed second is the loop's duty cycle —
   * the honest answer to "how loaded is the board", and the one that goes red
   * when a scene starts costing more than the 40 ms budget allows. */
  {
    static unsigned long lastVitals = 0;
    if (now - lastVitals >= 1000) {
      unsigned long span = now - lastVitals;
      lastVitals = now;
      if (span > 0) {
        int pct = (int)((frameBusyUs / 10UL) / span); /* us->% over span ms */
        state.boardLoad = pct > 100 ? 100 : pct;
      }
      state.boardFps = frameCount;
      frameBusyUs = 0;
      frameCount = 0;
      state.heapFreeKb = (int)(ESP.getFreeHeap() / 1024);
      state.heapMinKb = (int)(ESP.getMinFreeHeap() / 1024);
      state.heapLargestKb =
          (int)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024);
      state.uptimeSec = now / 1000UL;
      state.cpuMhz = (int)getCpuFrequencyMhz();
      graphs.boardTemp.push((int)(state.boardTemp * 10.0f));
      graphs.boardLoad.push(state.boardLoad);
    }
  }

  /* frame */
  if (frameTimer.check(now)) {
    unsigned long frameT0 = micros();
    UiCtx ui{display.fb, state, graphs, pet,        brain,
             now,        &forza.state(), forzaLive, &histories,
             coverClient.ready() ? coverClient.data() : nullptr,
             sceneMgr.historyMode(), &archive.series(), archive.seriesDays(),
             &ach};
    /* Reload the climate series when the ДОМ window changes, and refresh an
     * open one every couple of minutes — a sensor that speaks every 50 minutes
     * does not warrant more, and each load walks the card. */
    {
      static int lastHomeMode = -1;
      static unsigned long lastClimLoad = 0;
      static ClimateSeriesView climView;
      int hm2 = sceneMgr.homeMode();
      if (hm2 != lastHomeMode || (hm2 > 0 && now - lastClimLoad > 120000UL)) {
        lastHomeMode = hm2;
        lastClimLoad = now;
        climateSeriesOk =
            hm2 > 0 && climateLog.loadSeries(hm2 == 1 ? 1 : 7, climateSeries);
      }
      climView = {climateSeries.temp10, climateSeries.hum, ClimateLog::kCols,
                  climateSeries.filled, climateSeries.rows};
      ui.homeMode = hm2;
      ui.climate = climateSeriesOk ? &climView : nullptr;
    }
    sceneMgr.draw(ui);
    display.push();
    frameBusyUs += micros() - frameT0;
    frameCount++;
    /* drain queued SD writes at most ~2x/sec — each is an open/append/close, so
     * doing it every frame at 25 fps was a periodic hitch ("тупнячки") */
    if (sceneMgr.takeZbJoinRequest()) {
      zb.permitJoin(NOCT_ZB_JOIN_SEC);
      sceneMgr.alertCard(SceneManager::AL_SENSOR, "ПРИВЯЗКА",
                         zb.running()
                             ? "жду датчик 3 минуты - жми кнопку на нем"
                             : "zigbee не запущен");
    }
    if (sceneMgr.takeShotRequest()) {
      bool okShot = saveScreenshot();
      if (okShot) ach.bump(Achievements::ACH_SHOT);
      sceneMgr.toast(okShot ? "снимок сохранен" : "снимок: нет карты");
      if (shotFromConsole) {
        shotFromConsole = false;
        Serial.println(okShot ? "saved" : "failed");
      }
    }

    static unsigned long lastFlush = 0;
    if (now - lastFlush > 500) {
      sd.flush(); /* bounded to NOCT_SD_FLUSH_MAX entries per call */
      lastFlush = now;
      /* The card can go away at runtime — SdStore gives up on it after a run of
       * failures, and the UI should say so rather than keep claiming a card. */
      state.link.sdOk = sd.ok();
    }

    static unsigned long lastHeapLog = 0;
    unsigned long logEvery = now < 120000 ? 10000 : 60000;
    if (now - lastHeapLog > logEvery) {
      lastHeapLog = now;
      /* "tcp 1" only means the socket is open — it says nothing about whether
       * payloads are still arriving, which is what actually blanks the scenes
       * (dataDead). Log the age of the last payload and a couple of parsed
       * values so "no data on the screen" can be diagnosed from the log alone:
       * fresh age + real numbers = a render problem, stale age = a feed one. */
      unsigned long age = tcp.hasData() ? (now - tcp.lastPayloadMs()) : 0;
      Serial.printf(
          "[SYS] heap %u KB (min %u), scene %d, tcp %d, llm %d | payload %lus "
          "ago, lost %d, dead %d | ct=%d gt=%d cl=%d gl=%d ram=%.1f/%.1f "
          "| board %.1fC (max %.1f)\n",
          (unsigned)(ESP.getFreeHeap() / 1024),
          (unsigned)(ESP.getMinFreeHeap() / 1024), sceneMgr.currentScene(),
          tcp.connected(), state.link.llmBusy,
          tcp.hasData() ? age / 1000UL : 9999UL, state.link.signalLost,
          state.link.dataDead, state.hw.ct, state.hw.gt, state.hw.cl,
          state.hw.gl, state.hw.ru, state.hw.ra, state.boardTemp,
          state.boardTempMax);
    }
  }

  delay(1); /* yield to WiFi/lwIP/LLM tasks */
}
