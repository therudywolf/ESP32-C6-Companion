/*
 * Nocturne C6 — LovyanGFX device for Waveshare ESP32-C6-LCD-1.47.
 * Panel: LBS147TC-IF15 (ST7789V3) 172x320, INVON required, CGRAM column
 * offset 34 (172 centered in 240). Bus shared with the TF slot (MOSI 6 /
 * SCLK 7) — bus_shared=true so LGFX arbitrates against SD transactions.
 * Used in landscape (rotation 1): 320x172.
 */
#ifndef NOCT_DISPLAY_H
#define NOCT_DISPLAY_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "core/config.h"

class LGFX_C6 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

public:
  LGFX_C6() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000; /* above 62.5MHz spec; verified on this board.
                                    Drop to 40000000 if artifacts appear. */
      cfg.freq_read = 6000000;
      cfg.spi_3wire = true; /* no MISO wired to the panel */
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = NOCT_PIN_LCD_SCLK;
      cfg.pin_mosi = NOCT_PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = NOCT_PIN_LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = NOCT_PIN_LCD_CS;
      cfg.pin_rst = NOCT_PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 172;
      cfg.panel_height = 320;
      cfg.offset_x = 34;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;      /* INVON — mandatory for this panel */
      cfg.rgb_order = false;  /* swap if red/blue trade places */
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;  /* TF slot lives on the same SPI bus */
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = NOCT_PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 5000;
      cfg.pwm_channel = 0;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
};

/* Full-screen RGB565 framebuffer (320x172x2 = 110,080 B). Allocated FIRST at
 * boot — the largest single block; if this fails nothing else matters. */
class Display {
public:
  LGFX_C6 tft;
  LGFX_Sprite fb;

  Display() : fb(&tft) {}

  bool begin(uint8_t brightness) {
    tft.init();
    tft.setRotation(1); /* landscape 320x172, offsets auto-swap to 0/34 */
    tft.setBrightness(brightness);
    fb.setColorDepth(16);
    if (!fb.createSprite(NOCT_W, NOCT_H)) return false;
    fb.setTextWrap(false);
    return true;
  }

  void push() { fb.pushSprite(0, 0); }
  void setBrightness(uint8_t b) { tft.setBrightness(b); }
};

#endif
