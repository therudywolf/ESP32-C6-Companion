/*
 * Nocturne C6 — over-the-air firmware update. Two doors, both on the loop task
 * so the update screen can draw itself while flash is being written:
 *
 *  PUSH  ArduinoOTA on port 3232 — `pio run -t upload --upload-port <ip>`.
 *        Password-protected when OTA_PASSWORD is set in secrets.h.
 *  PULL  an `ota` URL in the server's rc block: the device downloads the image
 *        itself. The URL is NOT trusted blindly — see urlAllowed(): telemetry
 *        arrives over unauthenticated plain TCP, so an unchecked URL there is a
 *        remote-code-execution hole. Only the configured telemetry host, or a
 *        private-range literal, is accepted.
 *
 * Requires an OTA-capable partition table (min_spiffs.csv: two 1.875 MB app
 * slots). NVS keeps its offset from the old huge_app table, so the wolf's saved
 * state survives the switch.
 */
#ifndef NOCT_OTA_MANAGER_H
#define NOCT_OTA_MANAGER_H

#include <Arduino.h>

#include <functional>

class OtaManager {
public:
  /* `trustedHost` is the telemetry server (PC_IP): the only non-private host a
   * pull URL may name. Pass nullptr to allow private ranges only. */
  void begin(const char *hostname, const char *password,
             const char *trustedHost);
  /* Call every loop: services push OTA and runs a queued pull. */
  void tick(bool wifiUp);
  /* Queue a pull-OTA. Returns false (and sets message()) if the URL is
   * rejected — the caller should surface that, not retry. */
  bool requestPull(const String &url);

  /* Recovery path: flash /firmware.bin straight off the card at boot, then
   * rename it so it cannot loop. This is the door that still works when WiFi
   * is broken and the board is nowhere near a USB port — copy a file onto the
   * card from any machine and power-cycle. Update() writes to the INACTIVE
   * slot and only switches partitions once esp_ota_end validates the image, so
   * a truncated or foreign file leaves the running firmware untouched.
   * Returns true if an image was installed (the board reboots immediately). */
  bool installFromCard(class SdStore *sd);

  bool active() const { return active_; }
  int pct() const { return pct_; }
  const char *message() const { return msg_.c_str(); }
  /* Drawn once per progress step: (percent, message). The callback runs on the
   * loop task, so it may touch SPI — and must feed the task watchdog. */
  void setUiCallback(std::function<void(int, const char *)> cb) { ui_ = cb; }

  /* Exposed for tests/inspection: is `url` an acceptable image source? */
  static bool urlAllowed(const String &url, const char *trustedHost);

private:
  void runPull(const String &url);
  void progress(int pct, const char *msg);

  bool started_ = false;
  bool active_ = false;
  int pct_ = -1;
  String msg_;
  String pendingUrl_;
  const char *trustedHost_ = nullptr;
  std::function<void(int, const char *)> ui_;
};

#endif
