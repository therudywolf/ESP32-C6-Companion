#include "net/ZbHub.h"

#include "core/config.h"
#include "storage/CardConfig.h"
#include "storage/SdStore.h"

#ifndef NOCT_ZIGBEE

/* No Zigbee in this build: everything degrades to nothing, exactly like the SD
 * card does when absent. The `zb` block still works — a server can fill it. */
bool ZbHub::begin(SdStore *, const CardConfig *) { return false; }
void ZbHub::tick(unsigned long, AppState &) {}
void ZbHub::permitJoin(int) {}
int ZbHub::deviceCount() const { return 0; }
void ZbHub::factoryReset() {}
void ZbHub::save() {}
void ZbHub::restore() {}

#else

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
  /* Do NOT open the network at boot. A coordinator that is permanently
   * joinable is one anyone can join; pairing is an explicit act (Меню -> Волк,
   * or `zb join` on the console). */
  Zigbee.setRebootOpenNetwork(0);
  if (!Zigbee.begin(ZIGBEE_COORDINATOR)) {
    Serial.println("[ZB] coordinator failed to start");
    return false;
  }
  running_ = true;
  Serial.printf("[ZB] coordinator up on endpoint %d\n", NOCT_ZB_ENDPOINT);
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

void ZbHub::tick(unsigned long now, AppState &st) {
  if (!running_) return;

  int n = 0;
  for (int i = 0; i < ZigbeeData::kMax; i++) {
    const Slot &s = gSlots[i];
    if (s.addr == 0xFFFF) continue;
    ZbSensor &out = st.zb.list[n];
    /* Names come from the card so a sensor can be "Спальня" rather than a
     * short address nobody can read. */
    const char *nm = cfg_ ? cfg_->zbName(n) : "";
    if (nm && *nm) snprintf(out.name, sizeof(out.name), "%s", nm);
    else {
      /* "датчик " is 13 B of UTF-8 in a 17 B field; n is 0..3 here but the
       * compiler assumes the whole int range, so bound it explicitly. */
      char idx[2] = {(char)('1' + (n & 3)), 0};
      snprintf(out.name, sizeof(out.name), "датчик %s", idx);
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
  st.zb.count = n;

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
