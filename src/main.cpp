/*
 * Nocturne C6 — wolf companion for Waveshare ESP32-C6-LCD-1.47.
 * Port of Nocturne OS PC Companion: color Flipper-style UI, LLM-backed
 * wolf pet, wire-compatible telemetry client.
 *
 * Concurrency: single main loop owns ALL SPI (LCD + SD). The LLM task does
 * network only. WiFi/lwIP run in their own IDF tasks.
 */
#include <Arduino.h>
#include <SPI.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

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
#include "net/TelemetryClient.h"
#include "net/WifiManager.h"
#include "pet/PetBrain.h"
#include "pet/PhraseCache.h"
#include "pet/WolfPet.h"
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
static InputSystem *input = nullptr;
static IntervalTimer frameTimer(NOCT_FRAME_MS);
static bool prevTcpConnected = false;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n[BOOT] Nocturne C6 v%s\n", NOCT_VERSION);

  settings::load(state.settings);
  theme::bgStyle = state.settings.bgStyle;
  theme::bgLight = state.settings.bgLight;
  theme::applyPreset(state.settings.themePreset);
  if (state.settings.customActive) theme::applyPalette(state.settings.custom);
  Serial.println("[BOOT] settings loaded");

  /* Shared SPI2: claim it through the Arduino driver FIRST so both
   * LovyanGFX (bus_shared) and SD.h coordinate on the same bus. */
  SPI.begin(NOCT_PIN_LCD_SCLK, NOCT_PIN_SD_MISO, NOCT_PIN_LCD_MOSI,
            NOCT_PIN_SD_CS);
  Serial.println("[BOOT] spi up");

  /* SD first, on a virgin bus — card init (CMD0 at 400 kHz) is the touchy
   * part; once initialised it tolerates the shared-bus traffic. */
  state.link.sdOk = sd.begin();
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

  led.begin();
  led.setEnabled(state.settings.ledEnabled);
  input = new InputSystem(NOCT_PIN_BUTTON);

  pet.begin();
  phrases.begin(&sd);
  llm.begin(kLlmEndpoints, kLlmCount, LLM_API_KEY, LLM_MODEL, LLM_MODEL_BIG);
  brain.begin(&pet, &llm, &phrases, &sd);

  wifi.begin(kWifiNets, kWifiCount, state.settings.netSel);
  tcp.setServer(PC_IP, TCP_PORT);
  coverClient.begin(PC_IP, 8899); /* album cover from the control panel */
#if defined(LITE_URL) && defined(LITE_TOKEN)
  liteClient.begin(LITE_URL, LITE_TOKEN); /* always-on fallback when PC is off */
#endif

  SceneManager::Deps deps{};
  deps.disp = &display;
  deps.wifi = &wifi;
  deps.tcp = &tcp;
  deps.led = &led;
  for (int i = 0; i < kWifiCount; i++) deps.wifiNames[i] = kWifiNets[i].ssid;
  deps.wifiCount = kWifiCount;
  sceneMgr.begin(deps);

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
  if (esp_task_wdt_init(&wdt) == ESP_ERR_INVALID_STATE)
    esp_task_wdt_reconfigure(&wdt); /* core already started it — just retune */
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

  /* own clock via NTP (MSK) so the time survives the PC being off — overrides
   * the server's "clk" once synced; the status bar/screensaver then run standalone */
  static bool ntpInit = false;
  if (wifi.connected() && !ntpInit) {
    configTime(3 * 3600, 0, "pool.ntp.org", "time.google.com");
    ntpInit = true;
  }
  if (ntpInit) {
    struct tm tmNow;
    if (getLocalTime(&tmNow, 0))
      snprintf(state.pcClock, sizeof(state.pcClock), "%02d:%02d", tmNow.tm_hour,
               tmNow.tm_min);
  }

  tcp.tick(now, wifi.connected(), state, graphs);
  coverClient.update(state.media.coverTok); /* refetch cover on track change */

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
      display.setBrightness(b);
      persist = true;
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

  /* report wolf state upstream every 2 s so the companion app can show it */
  static unsigned long lastWolfReport = 0;
  if (tcp.connected() && now - lastWolfReport > 2000) {
    lastWolfReport = now;
    tcp.sendWolf(pet.hunger(), pet.happy(), pet.energy(), pet.mood(),
                 pet.isAlive(), pet.isSleeping(), pet.ageDays());
    tcp.sendCfg(state.settings); /* mirror live settings to the web panel */
  }

  forza.tick(now, wifi.connected());
  bool forzaLive = forza.connected(now);
  state.forzaLive = forzaLive;

  /* full radio power only while racing (UDP latency); else modem sleep */
  static bool wifiFast = false;
  if (forzaLive != wifiFast && wifi.connected()) {
    wifiFast = forzaLive;
    WiFi.setSleep(!wifiFast);
  }

  /* long-window hour history (accumulate live data, commit one sample/min) */
  if (tcp.connected() && !state.link.signalLost) histories.accumulate(state.hw);
  histories.tick(now);

  pet.tick(now);
  brain.tick(now, state);

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
    if (!pet.isAlive())
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
  led.setAmbient(state.settings.ledMode); /* idle ambient style */
  led.tick(now);

  /* input */
  ButtonEvent ev = input->update();
  if (ev != EV_NONE) {
    UiCtx ui{display.fb, state, graphs, pet,        brain,
             now,        &forza.state(), forzaLive, &histories,
             coverClient.ready() ? coverClient.data() : nullptr};
    sceneMgr.handleInput(ev, ui);
  }

  /* frame */
  if (frameTimer.check(now)) {
    UiCtx ui{display.fb, state, graphs, pet,        brain,
             now,        &forza.state(), forzaLive, &histories,
             coverClient.ready() ? coverClient.data() : nullptr};
    sceneMgr.draw(ui);
    display.push();
    /* drain queued SD writes at most ~2x/sec — each is an open/append/close, so
     * doing it every frame at 25 fps was a periodic hitch ("тупнячки") */
    static unsigned long lastFlush = 0;
    if (now - lastFlush > 500) {
      sd.flush();
      lastFlush = now;
    }

    static unsigned long lastHeapLog = 0;
    unsigned long logEvery = now < 120000 ? 10000 : 60000;
    if (now - lastHeapLog > logEvery) {
      lastHeapLog = now;
      Serial.printf("[SYS] heap %u KB (min %u), scene %d, tcp %d, llm %d\n",
                    (unsigned)(ESP.getFreeHeap() / 1024),
                    (unsigned)(ESP.getMinFreeHeap() / 1024),
                    sceneMgr.currentScene(), tcp.connected(),
                    state.link.llmBusy);
    }
  }

  delay(1); /* yield to WiFi/lwIP/LLM tasks */
}
