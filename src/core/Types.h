/*
 * Nocturne C6 — shared types. Ported from Nocturne OS Types.h: field names
 * mirror the server's 2-letter JSON keys (schema sv 1.0). Do not rename —
 * wire compatibility with the running PC server is a hard requirement.
 */
#ifndef NOCT_TYPES_H
#define NOCT_TYPES_H

#include <Arduino.h>

#include "core/ClimateAnalysis.h"

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
  int precip = 0; /* precipitation probability %, "wp" */
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
  int posSec = 0;            /* "mpos": elapsed position, seconds (snapshot) */
  int durSec = 0;           /* "mdur": track length, seconds (0 = unknown) */
  unsigned long posStamp = 0; /* millis() at parse, to interpolate while playing */
};

/** A Windows notification relayed by the server ("notif" block) — flies in over
 *  any scene. seq bumps per notification shown; pending = how many still queued. */
struct NotifData {
  long seq = 0;
  String app = "";   /* source app, e.g. "Telegram" */
  String title = ""; /* sender / headline */
  String body = "";  /* message text (may be empty in sender-only mode) */
  int pending = 0;   /* extra notifications still waiting behind this one */
  int durSec = 0;    /* "dur": how long to show the card (0 = default 7s) */
  bool led = true;   /* "led": flash the LED on arrival */
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

/** Zigbee / smart-home sensors, relayed by whatever owns the Zigbee radio.
 *  The board is deliberately NOT that coordinator: measured, the stack costs
 *  380 KB of flash and would take OTA with it, and Yandex Smart Home needs a
 *  cloud skill (OAuth2 + public HTTPS) that a device on a LAN cannot be. So a
 *  server holds the coordinator and forwards readings here — which works the
 *  same whether that server runs Zigbee2MQTT, Home Assistant or polls a
 *  Yandex hub's API. */
struct ZbSensor {
  char name[17] = {0};
  int temp10 = -32768; /* temperature x10 (23.6 C -> 236); -32768 = unknown */
  int humidity = -1;   /* % */
  int battery = -1;    /* % */
  int ageSec = -1;     /* since the sensor last reported; battery devices go
                          quiet for hours, so a reading with no age is a lie */
  /* Atmospheric pressure, hPa. -1 = this sensor has no barometer: the Aqara
   * WSDCGQ11LM carries one, the cheaper WSDCGQ01LM does not, so every consumer
   * must treat it as optional rather than assume zero. */
  int pressure = -1;
};

struct ZigbeeData {
  /* Five slots. One climate sensor is the concrete ask, and the spare four
   * cost 76 bytes each against hard-coding "1" the moment a second device
   * arrives. Motion sensors lived here too until they moved to their own
   * branch; the room is left because the reason for it did not change. */
  static const int kMax = 5;
  int count = 0;
  /* How many of the first `count` entries this board heard over its OWN
   * radio. The rest were relayed by the server or injected from the console,
   * and the distinction matters in exactly one place: what the board is
   * entitled to report back UP.
   *
   * A relayed sensor re-reported as local is the board laundering someone
   * else's data as its own observation - and once the server stores it under
   * that name it looks indistinguishable from a real device that has since
   * gone quiet. That is how a sensor nobody owns appeared in the panel and
   * stayed there. */
  int localCount = 0;
  ZbSensor list[kMax];
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
  bool notifShow = true;         /* "notifShow" show PC notification flyovers */
  int ledMode = 0;               /* "ledMode" idle LED: 0 mood/1 off/2 rainbow/3 candle */
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
  /* Quiet hours: between nightFrom:00 and nightTo:00 the panel drops to
   * NOCT_NIGHT_BRIGHT and the mood LED goes dark. Needs the NTP clock; a
   * button press suspends it briefly. Alerts always override. */
  bool nightMode = false;        /* "night"  */
  int nightFrom = 23;            /* "nightF" hour 0..23 */
  int nightTo = 8;               /* "nightT" hour 0..23 */
  /* Climate alerts on the Zigbee sensor. Off by default: a threshold nobody
   * chose is a threshold that cries wolf. Temperatures are whole degrees,
   * humidity whole percent; each bound can be disabled on its own with the
   * sentinel below, because "warn me if it gets cold" is a complete wish and
   * does not imply an upper bound. */
  bool zbAlert = false;          /* "zbAl"  master switch */
  int zbTempMin = -99;           /* "zbTmin" -99 = no lower bound */
  int zbTempMax = 99;            /* "zbTmax"  99 = no upper bound */
  int zbHumMin = -1;             /* "zbHmin"  -1 = no lower bound */
  int zbHumMax = 101;            /* "zbHmax" 101 = no upper bound */
  /* Silence after a battery warning fires, hours. The reading only moves a
   * few percent a week, so without this it would re-fire every report. */
  int zbBattMin = 15;            /* "zbBat" 0 = never warn */
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
  bool liteActive = false; /* PC down but the fallback endpoint is feeding data */
  bool nightActive = false; /* quiet hours in force right now */
  bool zbUp = false;        /* Zigbee coordinator formed a network */
};

/** Why the board last restarted, read once at boot from esp_reset_reason().
 *  An always-mounted device with a panic-triggering watchdog must be able to
 *  say what happened after it heals itself — otherwise a reboot loop is
 *  invisible. Persisted in NVS "nocturne" so the count survives the reset. */
struct BootInfo {
  const char *reasonText = "?"; /* human label, e.g. "watchdog" */
  int reason = 0;               /* raw esp_reset_reason() */
  uint32_t bootCount = 0;       /* total boots ever */
  uint32_t faultCount = 0;      /* boots after panic/watchdog/brownout */
  bool lastWasFault = false;    /* THIS boot followed an abnormal reset */
};

/** Single app state, filled by TelemetryClient from the server payload. */
struct AppState {
  HardwareData hw;
  WeatherData weather;
  MediaData media;
  NotifData notif;
  ProcessData process;
  ClaudeData claude;
  /* What the Р”РћРњ screen and the РџРћР“РћР”Рђ tile actually draw: the board's own
   * paired sensors first, then any the server relays. Composed once per tick
   * by ZbHub from zbRemote below вЂ” never written directly. */
  ZigbeeData zb;
  /* Sensors from the payload's `zb` block, i.e. someone ELSE's coordinator
   * (Zigbee2MQTT on the server, a Yandex-hub poller). Kept separate because
   * the hub rebuilds st.zb every frame, and merging beats one silently
   * clobbering the other вЂ” which is exactly what happened the first time. */
  ZigbeeData zbRemote;
  /* Seconds left in the Zigbee pairing window, 0 = closed. The ДОМ screen
   * counts it down so "press the button on the sensor" has a deadline. */
  int zbJoinSecs = 0;
  /* Barometric tendency over the last 3 hours, in tenths of a hPa, computed
   * from the card archive. `zbTrendOk` is false when there is no reading old
   * enough to compare against — "no trend yet" and "steady" are different
   * answers and the screen must not conflate them. */
  int zbPress10Delta3h = 0;
  bool zbTrendOk = false;
  int zbTemp10Delta3h = 0;
  int zbHumDelta3h = 0;
  /* ── multi-window climate analysis ──────────────────────────────────────
   * Recomputed off the card every five minutes. The three-hour numbers above
   * stay exactly as they were - they are the WMO standard and half the code
   * reads them - and these sit alongside rather than replacing them, because
   * a second window is only useful while the first is still there to
   * disagree with.
   *
   * Findings hold POINTERS to string literals, not copies: the strings live
   * in flash for the life of the program and copying them into this struct
   * would cost 600 bytes of RAM to say the same thing. */
  analysis::Windows zbWin;
  static const int kMaxFindings = 6;
  analysis::Finding zbFind[kMaxFindings];
  int zbFindCount = 0;
  int zbDewPoint10 = -9999; /* tenths of a degree, -9999 = not computable */
  int zbPressPct = -1;      /* percentile against the board's own archive */
  /* The same yardstick for the room's own two readings. "Warm for this flat"
   * is a fact about this flat; a threshold picked once by hand is a guess. */
  int zbTempPct = -1;
  int zbHumPct = -1;
  /* Water actually in the air, tenths of g/m3. Relative humidity is a ratio
   * against a capacity that doubles every ten degrees, so it moves when the
   * heating comes on and nothing has dried. This does not. */
  int zbAbsHum10 = -9999;
  /* Rows uploaded by the archive export, and how many days are still to go.
   * -1 in `zbExportLeft` means no export is running. */
  int zbExportRows = 0;
  int zbExportLeft = -1;
  /* The PC has been silent long enough that its numbers are no longer worth
   * showing, and no fallback is covering. Distinct from link.dataDead only in
   * that it also accounts for the lite endpoint: when THAT is feeding, the
   * data is real and every scene stays useful. */
  bool pcOffline = false;
  /* Seconds since the last payload, -1 = none this session. What a frozen
   * screen needs beside it to stop being a lie. */
  int payloadAgeSec = -1;
  EventsData events;
  ForestData forest;
  ServiceData services;
  Settings settings;
  LinkState link;
  BootInfo boot;
  /* OTA in progress: -1 idle, 0..100 percent. Drawn as a takeover screen so a
   * flash-in-progress is never mistaken for a hang. */
  int otaPct = -1;
  String otaError;
  /* The board's OWN die temperature (temperatureRead()). Everything else on
   * this screen is the PC's; this is the one number about the thing you are
   * holding. It also guards the backlight: the panel's thermal cliff is why
   * brightness is capped at NOCT_BRIGHT_MAX, and that cap has always been a
   * guess made once at room temperature. */
  float boardTemp = 0;
  float boardTempMax = 0;
  /* The board's own vitals, for the ПЛАТА C6 screen and the panel. Measured
   * rather than guessed: `boardLoad` is the share of each frame period the
   * render loop actually spent working, which is the only "CPU load" an
   * Arduino sketch can honestly report — there is no scheduler accounting to
   * ask, and a busy-idle counter would cost more than it tells. */
  int boardLoad = 0;      /* 0..100 %, share of the frame budget used */
  int boardFps = 0;       /* frames actually rendered in the last second */
  int heapFreeKb = 0;
  int heapMinKb = 0;      /* lowest ever seen — the number that matters */
  int heapLargestKb = 0;  /* biggest contiguous block: fragmentation shows here */
  /* What the backlight is ACTUALLY doing, as opposed to what was asked for.
   * Four different things silently override the request — night mode, the
   * idle dim, the thermal guard and a running override — and without these
   * the owner reads 210 on the slider while the glass sits at 90, with
   * nothing on either screen connecting the two. */
  uint8_t blNow = 0;      /* PWM actually driven, 0..255 */
  uint8_t blCap = 0;      /* ceiling in force right now */
  int blThermal = 0;      /* 0 cool, 1 warm-limited, 2 hot-limited */
  int blForceLeft = 0;    /* seconds left on a temporary override, 0 = none */
  unsigned long uptimeSec = 0;
  int cpuMhz = 0;
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
  /* Raise the backlight ceiling for a while: {value, minutes}. -1 = none.
   * A temporary FORCE, never a stored setting — see Display::forceFor. */
  int rcBlMax = -1;
  int rcBlMins = 15;
  /* Greyscale + no backdrop, for comparing screenshots. -1 = no change. */
  int rcMono = -1;
  /* Per-panel tone: channel gains in percent (0 = no change) and the black
   * point (-1 = no change). NOT one-shot on the server, so the values are
   * re-sent after a board reboot and the correction survives it. */
  int rcToneR = 0, rcToneG = 0, rcToneB = 0, rcToneK = -1;
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
  int rcNotif = -1;              /* 0/1 show PC notifications, -1 = none */
  int rcLedMode = -1;            /* 0..3 idle LED style, -1 = none */
  long rcUiElem = -1;            /* UI element bitmask, -1 = none */
  int rcColorRole = -1;         /* single-role edit: role 0..9, -1 = none */
  int rcColorR = 0, rcColorG = 0, rcColorB = 0;
  bool rcHasPalette = false;    /* full 10-role palette in rcPalette */
  uint16_t rcPalette[10] = {0};
  int rcPresetReset = -1;       /* 1 = drop custom, back to preset */
  int rcPin = -2;               /* pinned "home" scene; -1 = DEN, -2 = none */
  int rcSlot = -1;              /* active theme slot 0..2, -1 = none */
  int rcZbJoin = -1;   /* seconds to open the network, one-shot */
  int rcZbPoll = -1;   /* 1 = read the sensor now, one-shot */
  int rcZbInt = -1;    /* requested reporting cadence, seconds */
  /* Days of climate archive to upload, one-shot. The archive is the board's
   * alone while the PC sleeps, so this is the only way it ever reaches
   * anywhere it can be plotted or exported. */
  int rcZbDump = -1;
  int rcZbAlert = -1;
  int rcZbTempMin = -1000, rcZbTempMax = -1000;
  int rcZbHumMin = -1000, rcZbHumMax = -1000;
  int rcZbBattMin = -1000;
  int rcNight = -1;             /* quiet hours 0/1, -1 = none */
  int rcNightFrom = -1, rcNightTo = -1; /* quiet-hour bounds, -1 = none */
};

#endif
