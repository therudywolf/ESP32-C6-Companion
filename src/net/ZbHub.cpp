#include "net/ZbHub.h"

#include "core/Graphs.h"
#include "core/config.h"
#include "storage/CardConfig.h"
#include "storage/SdStore.h"

#ifndef NOCT_ZIGBEE

/* No Zigbee in this build: everything degrades to nothing, exactly like the SD
 * card does when absent. The `zb` block still works — a server can fill it. */
bool ZbHub::begin(SdStore *, const CardConfig *) { return false; }
void ZbHub::tick(unsigned long, AppState &st, Graphs &) {
  /* No local hub in this build: whatever the server relayed IS the list. */
  st.zb = st.zbRemote;
}
void ZbHub::permitJoin(int) {}
int ZbHub::deviceCount() const { return 0; }
void ZbHub::factoryReset() {}
void ZbHub::save() {}
void ZbHub::restore() {}

#else

#include <Preferences.h>
#include <esp_coexist.h>
#include <esp_task_wdt.h>

#include "Zigbee.h"

namespace {

/* One slot per sensor. Keyed by short address + endpoint, because a device that
 * rejoins keeps its IEEE address but can be handed a new short one. */
struct Slot {
  uint16_t addr = 0xFFFF;
  uint8_t ep = 0;
  int temp10 = -32768;
  int humidity = -1;
  int battery = -1;
  unsigned long tempAt = 0; /* millis of the last report, 0 = never */
  unsigned long humAt = 0;
};

Slot gSlots[ZigbeeData::kMax];
volatile bool gDirty = false;

/* Reports arrive on the Zigbee task, not the loop task, so the slot table is
 * written there and only ever read here. The fields are word-sized and each is
 * written by exactly one producer, so a torn read would at worst show one
 * reading one tick late — not worth a mutex on the render path. */
Slot *slotFor(uint16_t addr, uint8_t ep) {
  for (auto &s : gSlots)
    if (s.addr == addr && s.ep == ep) return &s;
  for (auto &s : gSlots)
    if (s.addr == 0xFFFF) {
      s.addr = addr;
      s.ep = ep;
      return &s;
    }
  return nullptr; /* more sensors than slots: ignore the newcomer */
}

/* A coordinator endpoint that ACCEPTS measurements rather than producing them.
 * The library ships ZigbeeThermostat, which does this for temperature only and
 * keeps its binding private; a temperature+humidity sensor needs both clusters,
 * so the endpoint is built here with client-role clusters for each. */
class SensorSink : public ZigbeeEP {
public:
  explicit SensorSink(uint8_t endpoint) : ZigbeeEP(endpoint) {
    _device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID;

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    esp_zb_identify_cluster_cfg_t ident_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };

    _cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(
        _cluster_list, esp_zb_basic_cluster_create(&basic_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(
        _cluster_list, esp_zb_identify_cluster_create(&ident_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    /* CLIENT role on both measurement clusters: that is what makes this end of
     * the link the listener, and what lets a sensor bind to us. */
    esp_zb_cluster_list_add_temperature_meas_cluster(
        _cluster_list, esp_zb_temperature_meas_cluster_create(NULL),
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    esp_zb_cluster_list_add_humidity_meas_cluster(
        _cluster_list, esp_zb_humidity_meas_cluster_create(NULL),
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    esp_zb_cluster_list_add_power_config_cluster(
        _cluster_list, esp_zb_power_config_cluster_create(NULL),
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    _ep_config = {.endpoint = _endpoint,
                  .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
                  .app_device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
                  .app_device_version = 0};

    /* Name the hub on the network. Has to come after the cluster list exists —
     * the setter writes into the basic cluster's attributes — so the board is
     * "Nocturne ForestHome" to anything that interviews it, not an anonymous
     * coordinator. */
    setManufacturerAndModel(NOCT_ZB_VENDOR, NOCT_ZB_NET_NAME);
  }

  /* Every attribute report and read response lands here. */
  void zbAttributeRead(uint16_t cluster_id,
                       const esp_zb_zcl_attribute_t *attribute,
                       uint8_t src_endpoint,
                       esp_zb_zcl_addr_t src_address) override {
    if (!attribute) return;
    Slot *s = slotFor(src_address.u.short_addr, src_endpoint);
    if (!s) return;

    if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT &&
        attribute->id == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID &&
        attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_S16 &&
        attribute->data.value) {
      /* ZCL reports hundredths; the payload schema carries tenths. */
      int16_t raw = *(int16_t *)attribute->data.value;
      if (raw != 0x8000) { /* 0x8000 is the ZCL "invalid" marker */
        s->temp10 = raw / 10;
        s->tempAt = millis();
        gDirty = true;
      }
    } else if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT &&
               attribute->id ==
                   ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID &&
               attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_U16 &&
               attribute->data.value) {
      uint16_t raw = *(uint16_t *)attribute->data.value;
      if (raw != 0xFFFF) {
        s->humidity = raw / 100;
        s->humAt = millis();
        gDirty = true;
      }
    } else if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_BASIC &&
               attribute->id == 0xFF01 && attribute->data.value &&
               (attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING ||
                attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING)) {
      /* Aqara/Xiaomi: battery does NOT come via PowerConfig. It rides in a
       * proprietary TLV blob on the basic cluster, attr 0xFF01: a length-
       * prefixed string of [tag u8][zcl-type u8][payload] records. Battery is
       * tag 0x01, type 0x21 (u16 LE), in millivolts. */
      const uint8_t *p = (const uint8_t *)attribute->data.value;
      uint8_t blen = p[0];
      const uint8_t *end = p + 1 + blen;
      p += 1;
      while (p + 2 <= end) {
        uint8_t tag = p[0], type = p[1];
        p += 2;
        int tl;
        switch (type) { /* only the sizes Xiaomi actually uses */
        case 0x10: case 0x20: case 0x28: tl = 1; break;
        case 0x21: case 0x29: tl = 2; break;
        case 0x23: case 0x2B: case 0x39: tl = 4; break;
        case 0x24: tl = 5; break;
        case 0x25: tl = 6; break;
        default: tl = -1; break;
        }
        if (tl < 0 || p + tl > end) break; /* unknown type: stop, don't guess */
        if (tag == 0x01 && type == 0x21) {
          uint16_t mv = (uint16_t)(p[0] | (p[1] << 8));
          /* CR2032 discharge curve, the mapping Zigbee2MQTT settled on:
           * ~3.0 V fresh, ~2.7 V dead. Linear between is honest enough. */
          int pct = ((int)mv - 2700) * 100 / 300;
          if (pct < 0) pct = 0;
          if (pct > 100) pct = 100;
          s->battery = pct;
          gDirty = true;
        }
        p += tl;
      }
    } else if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG &&
               attribute->data.value) {
      /* Battery percentage is reported in HALF percent, which trips everyone
       * who assumes 0..100 and then sees 200. */
      /* 0x0021 = BatteryPercentageRemaining. Spelled as the number because the
       * esp_zb header only names the mains attributes; the zboss header has the
       * enum but is not the one this library builds against. */
      if (attribute->id == 0x0021 &&
          attribute->data.type == ESP_ZB_ZCL_ATTR_TYPE_U8) {
        uint8_t raw = *(uint8_t *)attribute->data.value;
        if (raw != 0xFF) {
          s->battery = raw / 2;
          gDirty = true;
        }
      }
    }
  }
};

SensorSink *gSink = nullptr;

/* On-card snapshot: a battery sensor can be silent for an hour, so without this
 * every reboot blanks the screen until it next speaks. */
const char *kStatePath = "/logs/zb.bin";
const uint32_t kMagic = 0x5A424831; /* "ZBH1" */
struct StateBlob {
  uint32_t magic;
  uint16_t size;
  uint16_t reserved;
  uint32_t savedAt; /* unix seconds, so age survives the reboot too */
  Slot slots[ZigbeeData::kMax];
};

} // namespace

bool ZbHub::begin(SdStore *sd, const CardConfig *cfg) {
  sd_ = sd;
  cfg_ = cfg;
  restore();

  gSink = new SensorSink(NOCT_ZB_ENDPOINT);
  Zigbee.addEndpoint(gSink);
  /* Form the network on 802.15.4 channel 25 (2.475 GHz): above WiFi channel 11
   * and clear of 1-11 entirely, so the two stacks stop sitting on the same
   * frequency while they time-share one antenna. Only affects a NEW formation;
   * a network persisted in zb_storage keeps its original channel. */
  Zigbee.setPrimaryChannelMask(1UL << NOCT_ZB_CHANNEL);
  /* Do NOT open the network at boot. A coordinator that is permanently
   * joinable is one anyone can join; pairing is an explicit act (Меню -> Волк,
   * or `zb join` on the console). */
  Zigbee.setRebootOpenNetwork(0);
  /* Formation scans channels and can take seconds - more when the radio is
   * shared with a busy WiFi. This runs on the loop task, which the 15 s task
   * watchdog is watching, so step out of the watchdog for the duration rather
   * than gamble on the scan being quick. */
  /* Arm the WiFi + 802.15.4 coexistence scheme. NOBODY else does: not the
   * Arduino Zigbee library, not the zboss port, not the ieee802154 driver -
   * in Espressif's own coexistence example this call sits in the app's main,
   * and without it the 15.4 radio simply squats on the medium. Measured here:
   * TCP stayed "connected" while not one payload arrived for five minutes,
   * because WiFi RX never got the antenna back. */
  esp_err_t coex = esp_coex_wifi_i154_enable();
  Serial.printf("[ZB] wifi+154 coexistence: %s\r\n",
                coex == ESP_OK ? "armed" : esp_err_to_name(coex));
  esp_task_wdt_delete(NULL);
  bool ok = Zigbee.begin(ZIGBEE_COORDINATOR);
  esp_task_wdt_add(NULL);
  if (!ok) {
    Serial.println("[ZB] coordinator failed to start");
    return false;
  }
  running_ = true;
  Serial.printf("[ZB] coordinator up on endpoint %d, channel %d\n",
                NOCT_ZB_ENDPOINT, (int)esp_zb_get_current_channel());
  /* Mark the session: the NEXT boot must run the erase-and-restart cure before
   * it wastes two minutes failing to associate (see main.cpp setup). */
  {
    Preferences pp;
    pp.begin("nocturne", false);
    pp.putBool("zbRan", true);
    pp.end();
  }
  return true;
}

void ZbHub::permitJoin(int seconds) {
  if (!running_) return;
  if (seconds < 1) seconds = 1;
  if (seconds > 254) seconds = 254;
  Zigbee.openNetwork((uint8_t)seconds);
  joinUntil_ = millis() + (unsigned long)seconds * 1000UL;
  Serial.printf("[ZB] network open for %d s - put the sensor in pairing mode\n",
                seconds);
}

int ZbHub::deviceCount() const {
  int n = 0;
  for (const auto &s : gSlots)
    if (s.addr != 0xFFFF) n++;
  return n;
}

void ZbHub::factoryReset() {
  Serial.println("[ZB] forgetting the network and every paired device");
  if (sd_) sd_->remove(kStatePath);
  Zigbee.factoryReset(true); /* reboots */
}

void ZbHub::tick(unsigned long now, AppState &st, Graphs &g) {
  if (!running_) return;

  int n = 0;
  for (int i = 0; i < ZigbeeData::kMax; i++) {
    const Slot &s = gSlots[i];
    if (s.addr == 0xFFFF) continue;
    ZbSensor &out = st.zb.list[n];
    /* Names come from the card so a sensor can be "Спальня" rather than a
     * short address nobody can read. */
    const char *nm = cfg_ ? cfg_->zbName(n) : "";
    if (nm && *nm) {
      snprintf(out.name, sizeof(out.name), "%s", nm);
    } else if (n == 0) {
      /* The first sensor IS the house, so it carries the network's name. */
      snprintf(out.name, sizeof(out.name), "%s", NOCT_ZB_NET_NAME);
    } else {
      /* n is 1..3 here, but the compiler assumes the whole int range and warns
       * about the field width, so bound it explicitly. */
      char idx[2] = {(char)('1' + (n & 3)), 0};
      snprintf(out.name, sizeof(out.name), "%s %s", NOCT_ZB_NET_NAME, idx);
    }
    out.temp10 = s.temp10;
    out.humidity = s.humidity;
    out.battery = s.battery;
    /* Age from the freshest of the two measurements: a sensor that reports
     * temperature but not humidity is still alive. */
    unsigned long newest = s.tempAt > s.humAt ? s.tempAt : s.humAt;
    out.ageSec = newest ? (int)((now - newest) / 1000UL) : -1;
    n++;
  }
  /* Then top up from the server's own coordinator, skipping anything we
   * already hear ourselves. A sensor both relayed AND paired locally is one
   * sensor; listing it twice would be worse than either source alone. */
  for (int i = 0; i < st.zbRemote.count && n < ZigbeeData::kMax; i++) {
    const ZbSensor &r = st.zbRemote.list[i];
    bool dup = false;
    for (int j = 0; j < n; j++)
      if (strncmp(st.zb.list[j].name, r.name, sizeof(r.name)) == 0) dup = true;
    if (!dup) st.zb.list[n++] = r;
  }
  st.zb.count = n;

  /* Sparkline history for whichever sensor sits first, LOCAL OR RELAYED. This
   * used to hang off the local-report flag, which meant a server-fed sensor
   * got a screen but never a graph. Sampling on CHANGE (not on a timer) suits
   * a battery device that speaks every 20-60 minutes: 32 samples then reach
   * back most of a day, which is the whole reason this screen exists rather
   * than just the ПОГОДА tile. */
  if (n > 0) {
    const ZbSensor &z0 = st.zb.list[0];
    if (z0.temp10 != -32768 && z0.temp10 != lastTemp_) {
      lastTemp_ = z0.temp10;
      g.zbTemp.push(z0.temp10);
    }
    if (z0.humidity >= 0 && z0.humidity != lastHum_) {
      lastHum_ = z0.humidity;
      g.zbHum.push(z0.humidity);
    }
  }

  if (n > knownCount_) {
    knownCount_ = n;
    newSensor_ = true; /* consumed by main: toast + wolf */
  }

  if (gDirty) {
    gDirty = false;
    dirty_ = true;
    /* One line per report on the card: the sensor's own history, which is the
     * whole reason for having a card. */
    if (sd_ && sd_->ok() && now - lastLog_ > 5000) {
      lastLog_ = now;
      for (int i = 0; i < st.zb.count; i++) {
        const ZbSensor &z = st.zb.list[i];
        if (z.temp10 == -32768) continue;
        char line[96];
        snprintf(line, sizeof(line), "%lu,%s,%.1f,%d,%d", now / 1000UL, z.name,
                 z.temp10 / 10.0f, z.humidity, z.battery);
        if (!sd_->exists("/logs/zb.csv"))
          sd_->enqueueAppend("/logs/zb.csv", "uptime_s,name,temp_c,rh,bat");
        sd_->enqueueAppend("/logs/zb.csv", line, NOCT_SD_DIARY_MAX);
      }
    }
  }
  if (dirty_ && now - lastSave_ > 60000UL) {
    lastSave_ = now;
    dirty_ = false;
    save();
  }
}

void ZbHub::save() {
  if (!sd_ || !sd_->ok()) return;
  StateBlob b{};
  b.magic = kMagic;
  b.size = (uint16_t)sizeof(StateBlob);
  b.savedAt = (uint32_t)time(nullptr);
  memcpy(b.slots, gSlots, sizeof(b.slots));
  sd_->writeBlob(kStatePath, &b, sizeof(b));
}

void ZbHub::restore() {
  if (!sd_ || !sd_->ok()) return;
  StateBlob b{};
  if (!sd_->readBlob(kStatePath, &b, sizeof(b))) return;
  if (b.magic != kMagic || b.size != (uint16_t)sizeof(StateBlob)) return;
  memcpy(gSlots, b.slots, sizeof(gSlots));
  /* The saved millis stamps mean nothing after a reboot. Rebase them so the
   * restored readings show their REAL age: pretend they arrived that many
   * seconds before now, and if the clock is unusable, mark them unknown. */
  time_t t = time(nullptr);
  long gap = (t > 1700000000L && b.savedAt) ? (long)t - (long)b.savedAt : -1;
  int restored = 0;
  for (auto &s : gSlots) {
    if (s.addr == 0xFFFF) continue;
    restored++;
    if (gap < 0) {
      s.tempAt = s.humAt = 0; /* unknown age is honest; a fake one is not */
    } else {
      unsigned long back = (unsigned long)gap * 1000UL;
      s.tempAt = s.tempAt ? (millis() > back ? millis() - back : 1) : 0;
      s.humAt = s.humAt ? (millis() > back ? millis() - back : 1) : 0;
    }
  }
  if (restored)
    Serial.printf("[ZB] restored %d sensor(s) from the card (gap %lds)\n",
                  restored, gap);
}

#endif /* NOCT_ZIGBEE */
