#include "pet/PhraseCache.h"

#include "core/config.h"

struct FallbackEntry {
  const char *bucket;
  const char *phrase;
};

/* Flash fallback: the wolf is never speechless. Russian, <=90 chars, no U+00B7.
 * Used when offline / during games (cached, no GPU). Keep the voice: smart,
 * ironic, loyal, a touch grumpy, tech-savvy. */
static const FallbackEntry kFallback[] = {
    {"boot", "Система поднята. Волк на посту."},
    {"boot", "О, новое логово! Уже метит территорию... в логах."},
    {"boot", "Загрузился. Сенсоры в норме, хвост в боевой готовности."},
    {"boot", "Доброе утро, железо. Посчитаем тактовую частоту?"},
    {"boot", "Ядра прогрелись, я проснулся. Чем сегодня займёмся?"},
    {"fed", "Вкусно! Почти как свежий кэш L1."},
    {"fed", "Сытый волк - стабильная система."},
    {"fed", "Ммм, байты с хрустящей корочкой. Спасибо."},
    {"fed", "Брюхо полное, можно и за температурами последить."},
    {"fed", "Покормил - уважаю. Теперь работаю на полную."},
    {"played", "Отличная охота! Давай ещё раз."},
    {"played", "Весело! Теперь я доволен, как FPS без просадок."},
    {"played", "Хорошая разминка. Лапы быстрее твоего SSD."},
    {"played", "Поиграли - настроение в зелёной зоне."},
    {"played", "Ух, азарт! Чуть не сгрыз твою мышку."},
    {"pet", "М-м, чешется приятно. Ещё разок за ухом?"},
    {"pet", "Мур-р... то есть я же волк. Но всё равно приятно."},
    {"pet", "Ласка принята. Преданность плюс один."},
    {"pet", "Тёплая рука хозяина лучше любого радиатора."},
    {"pet", "Гладишь - таю, как термопаста на горячем ядре."},
    {"talk", "Я слежу за твоим железом. И за тобой. Шутка. Нет."},
    {"talk", "Скажи честно: я твой лучший процесс."},
    {"talk", "Спрашивай. Лапы свободны, ум острый."},
    {"talk", "Слушаю. Только не проси оптимизировать твой сон."},
    {"talk", "Я тут. Всегда на связи, в отличие от твоего VPN."},
    {"revive", "Я вернулся! Волки так просто не выключаются."},
    {"revive", "Перезагрузка прошла. Снова в строю."},
    {"revive", "Думал, потерял меня? Я живуч, как старый винчестер."},
    {"faint", "Без сил... всё темнеет..."},
    {"faint", "Падаю в спящий... не по своей воле..."},
    {"faint", "Кажется, я ухожу в свап..."},
    {"wake", "Проснулся. Что я пропустил, кроме твоих дедлайнов?"},
    {"wake", "О, новый день. Кэш снов сброшен."},
    {"wake", "Потянулся, зевнул. Готов сторожить дальше."},
    {"sleep", "Не буди. Дефрагментирую сны."},
    {"sleep", "Ушёл в спящий режим. Энергию надо беречь."},
    {"sleep", "Тс-с. Волк спит, питание экономит."},
    {"hunger", "Миска пуста, как твой список дел на выходные."},
    {"hunger", "Корми волка, а то начну грызть провода."},
    {"hunger", "Я бы сейчас съел целый килобайт корма."},
    {"hunger", "Урчит в животе громче твоего кулера. Покорми."},
    {"hunger", "Голодному волку даже бэкапы не в радость. Еды!"},
    {"bored", "Скучно. Даже багов никто не пишет."},
    {"bored", "Поиграй со мной, клавиатура подождёт."},
    {"bored", "От скуки считаю пиксели. Их много. Развлеки."},
    {"bored", "Хвост сам виляет от безделья. Займёмся чем-нибудь?"},
    {"bored", "Тоска. Брось мне хоть виртуальную палку."},
    {"sleepy", "Глаза слипаются... уйду в спящий режим."},
    {"sleepy", "Зеваю. Энергии меньше, чем заряда у телефона."},
    {"sleepy", "Клонит в сон. Ещё немного - и захраплю в логах."},
    {"alert", "ТРЕВОГА! Железо горит, хозяин!"},
    {"alert", "Красная зона! Сделай что-нибудь, я волк, а не пожарный."},
    {"alert", "Вою во всю глотку: температура зашкаливает!"},
    {"alert", "Аврал! Снижай нагрузку, пока не расплавилось."},
    {"event", "В лесу серверов что-то зашевелилось..."},
    {"event", "Сервер прислал весточку. И, кажется, не добрую."},
    {"event", "Чую тревогу на узлах. Принюхиваюсь к логам."},
    {"media", "Неплохой трек. Под него удобно выть."},
    {"media", "О, музыка. Лапа сама отбивает ритм."},
    {"media", "Звучит достойно. Добавил бы вой на припеве."},
    {"media", "Хороший выбор. Уши торчком, слушаю."},
    {"claude", "Токены тают быстрее, чем мой ужин."},
    {"claude", "Лимит на исходе. Береги слова, хозяин."},
    {"claude", "Claude почти выдохся. Дай ему передохнуть."},
    {"weather", "Погода меняется. Шерсть уже в курсе."},
    {"weather", "За окном что-то новое. Нос подсказывает."},
    {"weather", "Сменилась погода. Хвостом чую перемены."},
    {"gaming", "GPU воет как стая на луну. Удачной охоты в игре!"},
    {"gaming", "Тяжёлая графика пошла. Дай угадаю - опять гонки?"},
    {"gaming", "Видеокарта в ударе. Покажи им, хозяин!"},
    {"gaming", "Кадры летят, азарт кипит. Я болею за тебя!"},
    {"cpuwork", "Процессор пашет. Сборка? Рендер? Слежу за температурой."},
    {"cpuwork", "Ядра кипят работой. Уважаю трудолюбие."},
    {"cpuwork", "CPU вкалывает на все потоки. Не забудь про охлаждение."},
    {"away", "Хозяин ушёл. Стерегу логово и температуры."},
    {"away", "Опустело кресло. Поохраняю, пока тебя нет."},
    {"away", "Тебя нет, а я на посту. Волк не дремлет."},
    {"back", "С возвращением, хозяин. Я всё видел."},
    {"back", "О, вернулся! Без тебя логи были скучными."},
    {"back", "Ты дома. Доложить обстановку по железу?"},
    {"app", "О, новая программа. Любопытно, что задумал."},
    {"app", "Запустил что-то новенькое? Слежу за нагрузкой."},
    {"app", "Опять работа кипит. Я рядом, приглядываю."},
    {"app", "Новый процесс в стае. Принюхиваюсь."},
    {"race", "Гонки! Дави на газ, телеметрия за мной!"},
    {"race", "Старт! Волк в кокпите, держись, хозяин!"},
    {"race", "Рёв мотора - моя любимая музыка. Гони!"},
    {"latenight", "Три часа ночи. Даже я бы уже спал, а я волк."},
    {"latenight", "Ночь на дворе. Иди спать, железо подождёт."},
    {"latenight", "Хозяин, поздно. Утром будешь как разряженный аккумулятор."},
    {"morning", "Доброе утро. Кулеры уже прогрелись, ты тоже давай."},
    {"morning", "Проснулся? Я всю ночь сторожил, между прочим."},
    {"morning", "Утро. Хвостом машу, кофе не наливаю."},
    {"notice", "Я тут поглядел записи - кое-что мне не нравится."},
    {"notice", "Долго наблюдал и заметил неладное. Слушай сюда."},
    {"notice", "Веду записи не зря: цифры поползли не туда."},
    {"idle", "Тишина... подозрительно. Проверь бэкапы."},
    {"idle", "Сижу, смотрю на твои температуры. Залипает."},
    {"idle", "Лес тих, железо ровно дышит. Хорошо."},
    {"idle", "Бдю. Где-то в кулерах напевает ветер."},
    {"idle", "Спокойно на посту. Хвост на автопилоте."},
};

String PhraseCache::pickFromSd(const char *bucket) {
  if (!sd_ || !sd_->ok()) return "";
  String path = String("/wolf/cache/") + bucket + ".jsonl";
  String tail;
  /* Read the WHOLE cap, not an arbitrary 4 KB. The bucket rotates at
   * NOCT_SD_PHRASE_MAX, so reading less than that made the older half of every
   * full file unreachable - phrases were stored and then never drawn. */
  if (!sd_->readLastLines(path.c_str(), 200, tail, NOCT_SD_PHRASE_MAX) ||
      tail.length() == 0)
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
  /* Don't store a line the bucket already has. A small model asked the same
   * question repeatedly answers it the same way, and every duplicate doubles
   * that line's odds of being drawn - left alone, a bucket converges on one
   * phrase and the wolf starts repeating itself. Checking costs one read per
   * learned phrase, and phrases are learned at most once a minute. */
  String tail;
  if (sd_->readLastLines(path.c_str(), 200, tail, NOCT_SD_PHRASE_MAX) &&
      tail.indexOf(phrase) >= 0)
    return;
  /* ROTATE at the cap, don't freeze. Stopping the appends (the old behaviour)
   * pinned every bucket to the phrases it happened to learn first and never
   * refreshed them again. */
  sd_->enqueueAppend(path.c_str(), phrase, NOCT_SD_PHRASE_MAX);
}
