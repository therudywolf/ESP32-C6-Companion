#include "pet/PhraseCache.h"

#include <SD.h>

struct FallbackEntry {
  const char *bucket;
  const char *phrase;
};

/* Flash fallback: the wolf is never speechless. Russian, ≤90 chars. */
static const FallbackEntry kFallback[] = {
    {"hunger", "Миска пуста, как твой список дел на выходные."},
    {"hunger", "Корми волка, а то начну грызть провода."},
    {"hunger", "Я бы сейчас съел целый килобайт корма."},
    {"bored", "Скучно. Даже багов никто не пишет."},
    {"bored", "Поиграй со мной, клавиатура подождёт."},
    {"sleepy", "Глаза слипаются... уйду в спящий режим."},
    {"sleep", "Не буди. Дефрагментирую сны."},
    {"wake", "Проснулся. Что я пропустил, кроме твоих дедлайнов?"},
    {"fed", "Вкусно! Почти как свежий кэш L1."},
    {"fed", "Сытый волк — стабильная система."},
    {"played", "Отличная охота! Давай ещё раз."},
    {"played", "Весело! Теперь я доволен, как FPS без просадок."},
    {"talk", "Я слежу за твоим железом. И за тобой. Шутка. Нет."},
    {"talk", "Скажи честно: я твой лучший процесс."},
    {"alert", "ТРЕВОГА! Железо горит, хозяин!"},
    {"alert", "Красная зона! Сделай что-нибудь, я волк, а не пожарный."},
    {"event", "В лесу серверов что-то зашевелилось..."},
    {"claude", "Токены тают быстрее, чем мой ужин."},
    {"media", "Неплохой трек. Под него удобно выть."},
    {"weather", "Погода меняется. Шерсть уже в курсе."},
    {"idle", "Тишина... подозрительно. Проверь бэкапы."},
    {"idle", "Сижу, смотрю на твои температуры. Залипает."},
    {"faint", "Без сил... всё темнеет..."},
    {"revive", "Я вернулся! Волки так просто не выключаются."},
    {"boot", "Система поднята. Волк на посту."},
    {"boot", "О, новое логово! Уже метит территорию... в логах."},
    {"gaming", "GPU воет как стая на луну. Удачной охоты в игре!"},
    {"gaming", "Тяжёлая графика пошла. Дай угадаю — опять гонки?"},
    {"cpuwork", "Процессор пашет. Сборка? Рендер? Слежу за температурой."},
    {"back", "С возвращением, хозяин. Я всё видел."},
    {"away", "Хозяин ушёл. Стерегу логово и температуры."},
    {"race", "Гонки! Дави на газ, телеметрия за мной!"},
};

String PhraseCache::pickFromSd(const char *bucket) {
  if (!sd_ || !sd_->ok()) return "";
  String path = String("/wolf/cache/") + bucket + ".jsonl";
  String tail;
  if (!sd_->readLastLines(path.c_str(), 50, tail) || tail.length() == 0)
    return "";
  /* count lines, pick a random one */
  int lines = 0;
  for (size_t i = 0; i < (size_t)tail.length(); i++)
    if (tail[i] == '\n') lines++;
  if (lines == 0) return "";
  int want = random(lines);
  int start = 0, n = 0;
  for (size_t i = 0; i < (size_t)tail.length(); i++) {
    if (tail[i] == '\n') {
      if (n == want) {
        String s = tail.substring(start, i);
        s.trim();
        return s;
      }
      n++;
      start = i + 1;
    }
  }
  return "";
}

String PhraseCache::pick(const char *bucket) {
  String fromSd = pickFromSd(bucket);
  if (fromSd.length() > 0) return fromSd;
  /* flash table: random among matching bucket */
  int matches = 0;
  for (auto &e : kFallback)
    if (strcmp(e.bucket, bucket) == 0) matches++;
  if (matches == 0) return String("...");
  int want = random(matches);
  for (auto &e : kFallback) {
    if (strcmp(e.bucket, bucket) == 0 && want-- == 0) return String(e.phrase);
  }
  return String("...");
}

void PhraseCache::remember(const char *bucket, const String &phrase) {
  if (!sd_ || !sd_->ok() || phrase.length() == 0) return;
  String path = String("/wolf/cache/") + bucket + ".jsonl";
  /* soft cap: stop growing past ~8 KB per bucket */
  File f = SD.open(path.c_str(), FILE_READ);
  if (f) {
    size_t sz = f.size();
    f.close();
    if (sz > 8192) return;
  }
  sd_->enqueueAppend(path.c_str(), phrase);
}
