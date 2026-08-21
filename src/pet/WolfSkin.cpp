/* Nocturne C6 — the wolf's appearance, optionally from the card. */
#include "pet/wolf_sprites.h"

#include <Arduino.h>

#include "storage/SdStore.h"

namespace {
/* 4 frames x 128 bytes. Static, not heap: it is half a kilobyte and the heap
 * on this part is worth more than the convenience. */
uint8_t skinData[WOLF_FRAME_COUNT * (WOLF_SPR_W * WOLF_SPR_H / 8)];
bool skinLoaded = false;
char skinName[24] = {0};

const unsigned char *builtin(int id) {
  switch (id) {
  case WOLF_BLINK: return wolf_blink;
  case WOLF_AGGRO: return wolf_aggressive;
  case WOLF_FUNNY: return wolf_funny;
  default: return wolf_idle;
  }
}
} // namespace

const unsigned char *wolfFrame(int id) {
  if (id < 0 || id >= WOLF_FRAME_COUNT) id = WOLF_IDLE;
  if (!skinLoaded) return builtin(id);
  return skinData + id * (WOLF_SPR_W * WOLF_SPR_H / 8);
}

bool wolfLoadSkin(SdStore *sd, const char *name) {
  skinLoaded = false;
  skinName[0] = '\0';
  if (!sd || !sd->ok() || !name || !*name) return false;
  char path[48];
  snprintf(path, sizeof(path), "/skins/%s.wolf", name);
  /* readBlob insists on an exact size match, which is the whole validation a
   * raw frame dump needs: right length or not a skin. */
  if (!sd->readBlob(path, skinData, sizeof(skinData))) {
    Serial.printf("[SKIN] %s not usable (need exactly %u B) - built-in wolf\n",
                  path, (unsigned)sizeof(skinData));
    return false;
  }
  skinLoaded = true;
  strncpy(skinName, name, sizeof(skinName) - 1);
  Serial.printf("[SKIN] wearing '%s'\n", skinName);
  return true;
}

const char *wolfSkinName() { return skinName; }
