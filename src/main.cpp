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
  llm.begin(kLlmEndpoints, kLlmCount, LLM_API_KEY, LLM_MODEL);
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

  forza.tick(now, wifi.connected());
  bool forzaLive = forza.connected(now);
  state.forzaLive = forzaLive;

  /* full radio power only while racing (UDP latency); else modem sleep */
  static bool wifiFast = false;
  if (forzaLive != wifiFast && wifi.connected()) {
    wifiFast = forzaLive;
    WiFi.setSleep(!wifiFast);
  }

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
    UiCtx ui{display.fb, state, graphs, pet, brain, now,
             &forza.state(), forzaLive};
    sceneMgr.handleInput(ev, ui);
  }

  /* frame */
  if (frameTimer.check(now)) {
    UiCtx ui{display.fb, state, graphs, pet, brain, now,
             &forza.state(), forzaLive};
    sceneMgr.draw(ui);
    display.push();
    sd.flush(); /* queued SD writes ride the same task, after the push */

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
