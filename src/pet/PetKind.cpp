#include "pet/PetKind.h"

#include <string.h>

#include "pet/pet_sprites_gen.h"
#include "pet/wolf_sprites.h"

namespace petkind {

namespace {

struct KindDef {
  const char *label;      /* menu: "Волк" */
  const char *name;       /* glass: "НОКТЮРН" */
  const char *nameTitle;  /* prompt: "Ноктюрн" */
  const char *species;    /* "волк" */
  const char *stage[3];   /* pup / adult / elder */
  const char *toast[4];   /* feed / play / pet / talk */
  const char *identity;   /* first sentence of the system prompt */
  const char *manner;     /* "по-волчьи" */
  const unsigned char *frame[4];
};

/* Names: the wolf keeps its own. The dog is Хати — the owner's own service
 * already carries that name, so it is a name that lives in this house. */
const KindDef kKinds[PK_COUNT] = {
    {"Волк", "НОКТЮРН", "Ноктюрн", "волк",
     {"щенок", "волк", "седой"},
     {"ням-ням!", "поиграли!", "мур-р-р...", "..."},
     "Ты — Ноктюрн, домашний волк-компаньон.",
     "по-волчьи",
     {wolf_idle, wolf_blink, wolf_aggressive, wolf_funny}},
    {"Пёс", "ХАТИ", "Хати", "пёс",
     {"щенок", "пёс", "старый пёс"},
     {"гав! вкусно!", "апорт!", "виляет хвостом", "..."},
     "Ты — Хати, домашний пёс-компаньон, верный и весёлый.",
     "по-собачьи",
     {dog_idle, dog_blink, dog_aggressive, dog_funny}},
    {"Кот", "ТЕНЬ", "Тень", "кот",
     {"котенок", "кот", "старый кот"},
     {"мяу, наконец-то", "погоня!", "мур-р-р-р", "..."},
     "Ты — Тень, домашний кот-компаньон, независимый и язвительный.",
     "по-кошачьи",
     {cat_idle, cat_blink, cat_aggressive, cat_funny}},
    {"Лис", "РЫЖИК", "Рыжик", "лис",
     {"лисенок", "лис", "седой лис"},
     {"хрум!", "догонялки!", "тявкает довольно", "..."},
     "Ты — Рыжик, домашний лис-компаньон, хитрый и любопытный.",
     "по-лисьи",
     {fox_idle, fox_blink, fox_aggressive, fox_funny}},
};

int gKind = PK_WOLF;
char gName[24] = {0};      /* upper-case override from the card */
char gNameTitle[24] = {0}; /* the same, as typed */

const KindDef &def() { return kKinds[gKind]; }

} // namespace

void set(int kind) {
  if (kind < 0 || kind >= PK_COUNT) kind = PK_WOLF;
  gKind = kind;
  wolfSetBuiltin(def().frame[0], def().frame[1], def().frame[2],
                 def().frame[3]);
}

int current() { return gKind; }

void setName(const char *n) {
  if (!n || !*n) {
    gName[0] = gNameTitle[0] = 0;
    return;
  }
  strncpy(gNameTitle, n, sizeof(gNameTitle) - 1);
  gNameTitle[sizeof(gNameTitle) - 1] = 0;
  /* The glass shows the name in capitals like the wolf's; a Cyrillic
   * toupper is two bytes per letter, so it is done by hand on the UTF-8. */
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)n;
       *p && o + 2 < sizeof(gName);) {
    if (*p == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) { /* а..п → А..П */
      gName[o++] = (char)0xD0;
      gName[o++] = (char)(p[1] - 0x20);
      p += 2;
    } else if (*p == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) { /* р..я */
      gName[o++] = (char)0xD0;
      gName[o++] = (char)(p[1] + 0x20);
      p += 2;
    } else if (*p == 0xD1 && p[1] == 0x91) { /* ё → Ё */
      gName[o++] = (char)0xD0;
      gName[o++] = (char)0x81;
      p += 2;
    } else if (*p < 0x80) {
      gName[o++] = (char)toupper(*p);
      p++;
    } else {
      gName[o++] = (char)*p;
      p++;
    }
  }
  gName[o] = 0;
}

const char *name() { return gName[0] ? gName : def().name; }
const char *nameTitle() { return gNameTitle[0] ? gNameTitle : def().nameTitle; }
const char *species() { return def().species; }
const char *speciesLabel(int kind) {
  return (kind < 0 || kind >= PK_COUNT) ? "?" : kKinds[kind].label;
}
const char *stageName(int stage) {
  return def().stage[stage < 0 ? 0 : (stage > 2 ? 2 : stage)];
}

const char *actionToast(int action, bool furry) {
  if (!furry) {
    static const char *plain[4] = {"накормлен", "поиграли", "погладил",
                                   "..."};
    return plain[action < 0 ? 0 : (action > 3 ? 3 : action)];
  }
  return def().toast[action < 0 ? 0 : (action > 3 ? 3 : action)];
}

const char *llmIdentity() {
  /* A renamed pet keeps its species sentence but gets its own name: the
   * model plays "Хати" whether the card calls it that or the owner did. */
  static char buf[120];
  if (!gNameTitle[0]) return def().identity;
  snprintf(buf, sizeof(buf), "Ты — %s, домашний %s-компаньон.", gNameTitle,
           def().species);
  return buf;
}

const char *inManner() { return def().manner; }

bool phraseFits(const char *phrase) {
  if (gKind == PK_WOLF || !phrase) return true;
  /* Both cases of the root, and the adjective ("волчьим"). Cyrillic is two
   * bytes a letter, so a plain substring search works on the UTF-8. */
  return !strstr(phrase, "волк") && !strstr(phrase, "Волк") &&
         !strstr(phrase, "волч") && !strstr(phrase, "Волч");
}

} // namespace petkind
