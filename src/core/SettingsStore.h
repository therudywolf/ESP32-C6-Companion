/*
 * Nocturne C6 — settings persistence, NVS namespace "nocturne".
 * Key names ported from Nocturne OS where the meaning survived.
 */
#ifndef NOCT_SETTINGS_STORE_H
#define NOCT_SETTINGS_STORE_H

#include "core/Types.h"

namespace settings {
void load(Settings &s);
void save(const Settings &s);
/* Reset every user setting to its compiled default (the wolf's own NVS
 * namespace "wolfpet" is untouched — a settings reset must not kill the pet). */
void factoryReset(Settings &s);

/* Read esp_reset_reason(), fold it into the persisted boot counters and return
 * the result. Call once, early in setup(). */
void readBootInfo(BootInfo &b);
} // namespace settings

#endif
