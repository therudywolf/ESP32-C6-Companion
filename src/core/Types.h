/*
 * Nocturne C6 — shared types. Ported from Nocturne OS Types.h: field names
 * mirror the server's 2-letter JSON keys (schema sv 1.0). Do not rename —
 * wire compatibility with the running PC server is a hard requirement.
 */
#ifndef NOCT_TYPES_H
#define NOCT_TYPES_H

#include <Arduino.h>

#define NOCT_HDD_COUNT 4
#define NOCT_FAN_COUNT 4

struct HddEntry {
  char name[3] = {'C', '\0'};
  float used_gb = 0.0f;
  float total_gb = 0.0f;
  int temp = 0;
};

struct HardwareData {
  int ct = 0, gt = 0, cl = 0, gl = 0;
  int cc = 0, pw = 0, gh = 0, gv = 0;
  int gclock = 0, vclock = 0, gtdp = 0;
  float ru = 0.0f, ra = 0.0f;
  int nd = 0, nu = 0, pg = 0;
  int cf = 0, s1 = 0, s2 = 0, gf = 0;
  int fans[NOCT_FAN_COUNT] = {0, 0, 0, 0};
  int fan_controls[NOCT_FAN_COUNT] = {0, 0, 0, 0};
  HddEntry hdd[NOCT_HDD_COUNT] = {};
  float vu = 0.0f, vt = 0.0f;
  int ch = 0;
  int mb_sys = 0, mb_vsoc = 0, mb_vrm = 0, mb_chipset = 0;
  int dr = 0, dw = 0;
};

struct WeatherData {
  int temp = 0;
  String desc = "";
  int wmoCode = 0;
  static const int kMaxDays = 5;
  int wfDays = 0;
  int wfMin[kMaxDays] = {0};
  int wfMax[kMaxDays] = {0};
  int wfCode[kMaxDays] = {0};
};

struct ProcessData {
  String cpuNames[3] = {"", "", ""};
  int cpuPercent[3] = {0};
  String ramNames[2] = {"", ""};
  int ramMb[2] = {0};
};

struct MediaData {
  String artist = "";
  String track = "";
  bool isPlaying = false;
  bool isIdle = false;
  String mediaStatus = "PAUSED"; /* "PLAYING" | "PAUSED" */
  long coverTok = 0;             /* "ctok": album cover changed -> refetch */
};

/** External events from Prometheus Alertmanager (server "events" block). */
struct EventsData {
  static const int kMaxList = 4;
  int count = 0;
  char top[21] = {0};
  char severity[12] = {0};
  char list[kMaxList][21] = {{0}};
  char text[61] = {0};
};

struct ForestNode {
  char id[8] = {0};
  char name[17] = {0};
  char status[6] = {0}; /* up | warn | down */
  int cpu = -1, ram = -1, disk = -1;
  char extra[17] = {0};
};

struct ForestData {
  static const int kMaxNodes = 6;
  int count = 0;
  int up = 0;
  ForestNode nodes[kMaxNodes];
};

struct ServiceEntry {
  char name[17] = {0};
  char status[6] = {0};
  int ms = -1;
};

struct ServiceData {
  static const int kMaxServices = 8;
  int count = 0;
  int up = 0;
  ServiceEntry list[kMaxServices];
  int dockTotal = 0;
  int dockUp = -1;
};

struct ClaudeData {
  bool available = false;
  String plan = "";
  int windowPct = -1;
  int weeklyPct = -1;
  int resetsInMin = -1;
  int weeklyResetMin = -1;
  long todayTokens = 0;
  int todayMsgs = 0;
  int todayTools = 0;
  String date = "";
  bool stale = false;
};

/** User settings persisted in NVS namespace "nocturne". */
struct Settings {
  bool ledEnabled = true;        /* "led"      WS2812 mood/alert light */
  bool carouselEnabled = false;  /* "carousel" auto-cycle scenes */
  int carouselIntervalSec = 10;  /* "carouselSec" 5/10/15 */
  int brightness = 200;          /* "bright"   backlight PWM 10..255 */
  int displayTimeoutSec = 0;     /* "dispTimeout" 0=off, 30/60 dim */
  int pinnedScene = -1;          /* "pinScene" home scene; -1 = DEN */
  int netSel = -1;               /* "netSel"   WiFi lock; -1 = auto */
  bool petLlm = true;            /* "petLlm"   wolf speaks via LLM */
  int wolfChatter = 2;           /* "wChat" idle talk: 0 off/1 rare/2 norm/3 often */
  int wolfTone = 0;              /* "wTone" 0 обычный/1 добрый/2 ворчун/3 дерзкий */
  uint16_t uiElements = 0xFFFF;  /* "uiElem" which optional widget classes show */
  bool flipped = false;          /* "flip"     rotate display 180 */
  int themePreset = 0;           /* "theme"    palette preset index */
  int bgStyle = 1;               /* "bgStyle"  0 solid/1 anim/2 grid */
  bool bgLight = false;          /* "bgLight"  light/white background */
  uint16_t custom[10] = {0};     /* "custom"   live hand-tuned palette (RGB565) */
  bool customActive = false;     /* "customOn" use custom palette, not preset */
  /* On-device saved themes: 3 slots the colour editor writes to and the
   * "Слот" menu item cycles between. */
  uint16_t slot[3][10] = {{0}};  /* "slot0/1/2" saved palettes */
  bool slotUsed[3] = {false, false, false}; /* "slotU0/1/2" */
  int activeSlot = 0;            /* "aslot"    slot the editor saves into */
  /* Which scenes appear in the nav ring / carousel (bit i = SceneId i). DEN
   * (bit 0) is forced on. Default: everything visible. */
  uint32_t sceneMask = 0xFFFFFFFFu; /* "scnMask" */
};

/** Connectivity/UI status shown in the status bar (not from the server). */
struct LinkState {
  bool wifiConnected = false;
  int rssi = 0;
  char ssid[33] = {0};
  bool tcpConnected = false;
  bool signalLost = false; /* payload stale >5s (status-bar dot) */
  bool dataDead = true;    /* no payload >30s — only then blank the scenes */
  bool sdOk = false;
  bool llmBusy = false;    /* request in flight (status bar spark) */
  bool llmOk = false;      /* last LLM call succeeded */
};

/** Single app state, filled by TelemetryClient from the server payload. */
struct AppState {
  HardwareData hw;
  WeatherData weather;
  MediaData media;
  ProcessData process;
  ClaudeData claude;
  EventsData events;
  ForestData forest;
  ServiceData services;
  Settings settings;
  LinkState link;
  bool weatherReceived = false;
  bool forzaLive = false; /* Forza telemetry within timeout window */
  bool alertActive = false;
  int alertTargetScene = 0;
  int alertMetric = -1; /* 0=ct 1=gt 2=cl 3=gl 4=gv 5=ram; -1=none */
  int pcIdleSec = -1;
  char pcClock[6] = {0}; /* "HH:MM" from server */

  /* Remote control (server "rc" block — companion web app). Acted on once
   * per seq change; rcNew is set by the parser, cleared by the consumer. */
  long rcSeq = -1;
  bool rcNew = false;
  int rcScreen = -1;             /* jump to scene, -1 = none */
  String rcSay;                  /* make the wolf say this, "" = none */
  int rcTheme = -1;              /* theme preset, -1 = none */
  int rcChromeR = -1, rcChromeG = -1, rcChromeB = -1; /* custom chrome */
  int rcAccentR = -1, rcAccentG = -1, rcAccentB = -1; /* custom accent */
  int rcBright = -1;             /* backlight 10..255, -1 = none */
  String rcAction;               /* "feed"|"play"|"talk", "" = none */
  int rcLed = -1;                /* 0/1, -1 = none */
  int rcCarousel = -2;           /* -1 off, 5/10/15 sec, -2 = none */
  int rcPetLlm = -1;             /* 0/1, -1 = none */
  int rcFlip = -1;               /* 0/1, -1 = none */
  int rcTimeout = -1;            /* 0/30/60 dim sec, -1 = none */
  int rcBgStyle = -1;            /* 0/1/2, -1 = none */
  int rcBgLight = -1;            /* 0/1, -1 = none */
  long rcSceneMask = -1;         /* visible-scene bitmask, -1 = none */
  int rcWolfChatter = -1;        /* 0..3, -1 = none */
  int rcWolfTone = -1;           /* 0..3, -1 = none */
  long rcUiElem = -1;            /* UI element bitmask, -1 = none */
  int rcColorRole = -1;         /* single-role edit: role 0..9, -1 = none */
  int rcColorR = 0, rcColorG = 0, rcColorB = 0;
  bool rcHasPalette = false;    /* full 10-role palette in rcPalette */
  uint16_t rcPalette[10] = {0};
  int rcPresetReset = -1;       /* 1 = drop custom, back to preset */
};

#endif
