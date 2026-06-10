/*
 * Nocturne C6 — scene renderers. Each draws the CONTENT band (y 20..151)
 * into the framebuffer; SceneManager owns status bar, footer and overlays.
 */
#ifndef NOCT_SCENES_H
#define NOCT_SCENES_H

#include "ui/SceneIds.h"
#include "ui/Widgets.h"

namespace scenes {

const char *title(int scene);
/* Contextual long-press hint for the footer (nullptr = none). */
const char *actionHint(int scene, UiCtx &ui);

void drawDen(UiCtx &ui, int actionSel, bool actionMode);
void drawDash(UiCtx &ui);
void drawCpu(UiCtx &ui);
void drawGpu(UiCtx &ui);
void drawRam(UiCtx &ui);
void drawDisks(UiCtx &ui);
void drawFans(UiCtx &ui);
void drawMb(UiCtx &ui);
void drawNet(UiCtx &ui);
void drawMedia(UiCtx &ui);
void drawWeather(UiCtx &ui);
void drawClaude(UiCtx &ui);
void drawForest(UiCtx &ui);
void drawServices(UiCtx &ui);
void drawEvents(UiCtx &ui);
void drawHistory(UiCtx &ui);
void drawForza(UiCtx &ui);
void drawSysInfo(UiCtx &ui); /* overlay via menu, not in the ring */

/* "no data yet" placeholder used by telemetry scenes. */
void noSignal(UiCtx &ui);

} // namespace scenes

#endif
