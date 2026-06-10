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
} // namespace settings

#endif
