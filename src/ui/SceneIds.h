/*
 * Nocturne C6 — scene ring order. SHORT press cycles this ring.
 * Indices are also reported to the server as `screen:N`.
 */
#ifndef NOCT_SCENE_IDS_H
#define NOCT_SCENE_IDS_H

#include <cstring>

enum SceneId {
  SCENE_DEN = 0, /* home: the wolf */
  SCENE_DASH,
  SCENE_CPU,
  SCENE_GPU,
  SCENE_RAM,
  SCENE_DISKS,
  SCENE_FANS,
  SCENE_MB,
  SCENE_NET,
  SCENE_MEDIA,
  SCENE_WEATHER,
  SCENE_CLAUDE,
  SCENE_FOREST,
  SCENE_SERVICES,
  SCENE_EVENTS,
  SCENE_HISTORY,
  /* ДОМ sits last in the ring, right before FORZA. Appending here rather than
   * inserting mid-enum keeps every existing scene index — and therefore every
   * saved scnMask bit and the server's screen:N — exactly where it was. */
  SCENE_HOME,
  /* The board's own vitals. Appended before FORZA for the same reason ДОМ
   * was: every existing index, saved scnMask bit and screen:N stays put. */
  SCENE_BOARD,
  /* ДВИЖЕНИЕ. Appended before FORZA for the third time and the same reason:
   * every existing scene index, saved scnMask bit and server screen:N keeps
   * the number it already had. */
  SCENE_MOTION,
  /* АНАЛИЗ. Appended before FORZA for the same reason as the three before
   * it: every existing scene index, saved scnMask bit and server screen:N
   * keeps the number it already had. */
  SCENE_ANALYSIS,
  SCENE_FORZA,
  SCENE_COUNT
};

/* Does this scene need the PC to be worth looking at?
 *
 * The board is a companion, not a terminal: with the PC off it still has a
 * wolf, a paired climate sensor, its own vitals and an archive on the card.
 * Without this split, a dark PC turned every screen into the same blinking
 * "НЕТ СИГНАЛА" and the nav ring became fourteen identical blanks.
 *
 * ПОГОДА counts as autonomous on purpose: a forecast fetched this morning is
 * still a forecast this evening, so it keeps showing with its age rather than
 * being hidden. */
inline bool sceneNeedsPc(int s) {
  switch (s) {
  case SCENE_DEN:
  case SCENE_WEATHER:
  case SCENE_HISTORY:
  case SCENE_HOME:
  case SCENE_BOARD:
  /* Motion sensors are Zigbee: they keep reporting with the PC dark, which is
   * exactly when knowing whether anyone walked past matters most. */
  case SCENE_MOTION:
  /* Every number on it comes off the card and the Zigbee sensor. A dark PC
   * changes nothing about what the barometer did. */
  case SCENE_ANALYSIS:
  case SCENE_FORZA:
    return false;
  default:
    return true;
  }
}

/* Map the server's target_screen name to a scene (alert takeover). */
inline int sceneFromServerName(const char *name) {
  struct {
    const char *n;
    SceneId s;
  } static const map[] = {
      {"MAIN", SCENE_DASH},   {"CPU", SCENE_CPU},
      {"GPU", SCENE_GPU},     {"RAM", SCENE_RAM},
      {"DISKS", SCENE_DISKS}, {"MEDIA", SCENE_MEDIA},
      {"FANS", SCENE_FANS},   {"MOTHERBOARD", SCENE_MB},
      {"HOME", SCENE_HOME},   {"BOARD", SCENE_BOARD},
      {"MOTION", SCENE_MOTION}, {"ANALYSIS", SCENE_ANALYSIS},
  };
  for (auto &m : map)
    if (strcmp(name, m.n) == 0) return m.s;
  return SCENE_DASH;
}

#endif
