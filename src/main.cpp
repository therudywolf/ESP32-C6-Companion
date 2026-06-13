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
    for (;;) { /* keep shouting so a late-attached monitor sees it */
      Serial.println("[BOOT] FATAL: framebuffer alloc failed");
      rgbLedWrite(NOCT_PIN_RGB, 0, 80, 0); /* red (RGB-ordered LED) */
      delay(500);
      rgbLedWrite(NOCT_PIN_RGB, 0, 0, 0);
      delay(500);
    }
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
}

void loop() {
  unsigned long now = millis();

  /* background services */
  wifi.tick(now);
  state.link.wifiConnected = wifi.connected();
  state.link.rssi = wifi.rssi();
  strncpy(state.link.ssid, wifi.ssid(), sizeof(state.link.ssid) - 1);

  tcp.tick(now, wifi.connected(), state, graphs);
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
      cfg.brightness = state.rcBright;
      display.setBrightness(state.rcBright);
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
                                         : WolfPet::ACT_TALK;
      pet.doAction(a);
      brain.onAction(a);
      led.setMode(StatusLed::BLIP_OK);
    }
    if (state.rcScreen >= 0) sceneMgr.requestScene(state.rcScreen);
    if (state.rcSay.length()) brain.sayNow(state.rcSay);
    if (persist) settings::save(cfg);
    Serial.printf("[RC] seq=%ld screen=%d theme=%d action='%s' say='%s'\n",
                  state.rcSeq, state.rcScreen, state.rcTheme,
                  state.rcAction.c_str(), state.rcSay.c_str());
  }

  /* report wolf state upstream every 2 s so the companion app can show it */
  static unsigned long lastWolfReport = 0;
  if (tcp.connected() && now - lastWolfReport > 2000) {
    lastWolfReport = now;
    tcp.sendWolf(pet.hunger(), pet.happy(), pet.energy(), pet.mood(),
                 pet.isAlive(), pet.isSleeping(), pet.ageDays());
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

  /* mood light: alert > forza > thinking > speaking > offline > warm > mood */
  if (state.alertActive) {
    led.setMode(StatusLed::ALERT);
  } else if (forzaLive && forza.state().raceOn) {
    led.setForzaPct(forza.state().rpmPct());
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
  led.tick(now);

  /* input */
  ButtonEvent ev = input->update();
  if (ev != EV_NONE) {
    UiCtx ui{display.fb, state, graphs, pet,        brain,
             now,        &forza.state(), forzaLive, &histories};
    sceneMgr.handleInput(ev, ui);
  }

  /* frame */
  if (frameTimer.check(now)) {
    UiCtx ui{display.fb, state, graphs, pet,        brain,
             now,        &forza.state(), forzaLive, &histories};
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
