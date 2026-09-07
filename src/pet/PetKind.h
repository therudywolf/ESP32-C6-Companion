/*
 * Nocturne C6 — PetKind: WHAT the companion is.
 *
 * The tamagotchi core (WolfPet) is numbers; the voice (PetBrain) is prompts
 * and a phrase table; the face is four XBM frames. All three assumed a wolf
 * by name — "Ноктюрн", "щенок / волк / седой", "мур-р-р... то есть я же
 * волк". This module is the one place that knows the species, so the rest
 * of the firmware asks it instead of hardcoding a wolf.
 *
 * A kind is: a display name, a species word for prompts and phrases, three
 * life-stage names, four toasts for the four actions, four sprite frames and
 * a colour lean. The owner picks it in Меню → Питомец → Вид, or from the web
 * panel; the choice is a setting (Settings::petKind), the pet's stats are
 * NOT touched by it — the same animal changes its coat, it does not die and
 * get replaced.
 *
 * "Фурревость" (Settings::furry) is a separate switch: it is not WHICH
 * animal but whether the animal leaks into the chrome at all — paw prints
 * on the scene OSD, the sprite in the boot animation and screensaver, the
 * "ням-ням!" toasts. Off, the device is a plain instrument that happens to
 * have a pet screen.
 */
#ifndef NOCT_PET_KIND_H
#define NOCT_PET_KIND_H

#include <Arduino.h>

namespace petkind {

enum Kind { PK_WOLF = 0, PK_DOG, PK_CAT, PK_FOX, PK_COUNT };

/* Select the species. Also re-points wolfFrame() at that species' frames
 * unless a card skin is loaded (a skin is the owner's own art and wins). */
void set(int kind);
int current();

/* A name from /nocturne.ini `[pet] name = ...` overrides the built-in one. */
void setName(const char *name);

const char *name();                  /* "НОКТЮРН" — as drawn on the glass */
const char *nameTitle();             /* "Ноктюрн" — as spoken in prompts */
const char *species();               /* "волк" */
const char *speciesLabel(int kind);  /* menu label, "Волк" */
const char *stageName(int stage);    /* WolfPet::Stage → "щенок" */
/* The toast under each action: feed / play / pet / talk. With furry off the
 * same call returns a plain confirmation ("готово"). */
const char *actionToast(int action, bool furry);
/* First sentence of the LLM system prompt: who the model is playing. */
const char *llmIdentity();
/* "по-волчьи" — how the pet answers a stroke, for the prompt. */
const char *inManner();

/* Does a canned phrase belong to THIS species? Lines that name the wolf are
 * kept out of a cat's mouth; lines with no species word suit everyone. */
bool phraseFits(const char *phrase);

} // namespace petkind

#endif
