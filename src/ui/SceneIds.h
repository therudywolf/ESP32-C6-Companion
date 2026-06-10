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
  SCENE_FORZA,
  SCENE_COUNT
};

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
  };
  for (auto &m : map)
    if (strcmp(name, m.n) == 0) return m.s;
  return SCENE_DASH;
}

#endif
