#include "pet/PhraseCache.h"

#include <string.h>

#include "core/config.h"

struct FallbackEntry {
  const char *bucket;
  const char *phrase;
};

/* ── The flash table ─────────────────────────────────────────────────────
 *
 * Voice: smart, ironic, loyal, a touch grumpy, tech-savvy. Never cruel.
 *
 * HARD LIMITS, measured on the device, not guessed:
 *   - the bubble is 214 x 94 px in F_MED: ~19 chars per line, 4 lines,
 *     so a line must stay under ~72 characters or its tail is never drawn;
 *   - F_MED has the full Cyrillic block, but NO em dash, NO «», NO degree
 *     sign, NO ellipsis glyph — those draw as hollow boxes. Hyphen, quotes
 *     as '"', "..." as three dots, temperatures as "24 градуса".
 *     tools/check_glyphs.py scans this file and fails the build on any of
 *     them; the on-device lint catches what the scanner cannot.
 *
 * Buckets are PATHS. pick("idle.scene.cpu") tries that, then "idle.scene",
 * then "idle" — so the table can be as specific as it has lines for.
 *
 * Placeholders: {track} {artist} {app} {scene} {gpu} {cpu} {gt} {ct} {temp}
 * {room} {rh} {hour} {age} {wk}. A line whose placeholder has no value in the
 * current context is not eligible; the wolf never says "{track}".
 *
 * Stage flavours: "<bucket>.pup" and "<bucket>.elder" are tried before the
 * plain bucket, so a two-day-old wolf and a month-old one do not sound the
 * same when fed. */
static const FallbackEntry kFallback[] = {
    /* ── boot ─────────────────────────────────────────────────────────── */
    {"boot", "Система поднята. Волк на посту."},
    {"boot", "Загрузился. Сенсоры в норме, хвост в боевой готовности."},
    {"boot", "Ядра прогрелись, я проснулся. Чем сегодня займемся?"},
    {"boot", "Я на месте. Кто тут без меня температуры смотрел?"},
    {"boot", "Перезагрузка - как сон без снов. Ну, привет."},
    {"boot", "Включили. Логово то же, хозяин тот же. Хорошо."},
    {"boot", "Прошивка новая, нюх старый. Работаю."},
    {"boot.pup", "Ой. Я... это я? Где все? Где еда?"},
    {"boot.pup", "Проснулся щенком в железном логове. Интересно тут."},
    {"boot.elder", "Опять включили. Ладно, старый волк еще послужит."},
    {"boot.elder", "Столько перезагрузок видел... Эта - ничего."},

    /* ── actions ──────────────────────────────────────────────────────── */
    {"fed", "Вкусно! Почти как свежий кэш L1."},
    {"fed", "Сытый волк - стабильная система."},
    {"fed", "Ммм, байты с хрустящей корочкой. Спасибо."},
    {"fed", "Брюхо полное, можно и за температурами последить."},
    {"fed", "Покормил - уважаю. Теперь работаю на полную."},
    {"fed", "Спасибо. Голодный я ворчу, сытый - просто ворчу."},
    {"fed", "Ем. Не смотри так, ты тоже жуешь за монитором."},
    {"fed", "Пища принята, настроение пересчитано. Плюс."},
    {"fed", "Вот это я понимаю - апдейт. Спасибо, хозяин."},
    {"fed", "Наелся. Теперь можно и поворчать с чистой совестью."},
    {"fed.pup", "Ням! Еще? Еще! Я расту, мне надо!"},
    {"fed.pup", "Вкуснятина! Хвост сам крутится."},
    {"fed.elder", "Спасибо. В мои годы и миска - событие."},
    {"fed.elder", "Ем не спеша. Куда торопиться седому волку."},

    {"played", "Отличная охота! Давай еще раз."},
    {"played", "Весело! Теперь я доволен, как FPS без просадок."},
    {"played", "Хорошая разминка. Лапы быстрее твоего SSD."},
    {"played", "Поиграли - настроение в зеленой зоне."},
    {"played", "Ух, азарт! Чуть не сгрыз твою мышку."},
    {"played", "Еще! Нет, серьезно, еще один раунд."},
    {"played", "Вот это была погоня. Ты почти успевал."},
    {"played", "Наигрался. Теперь буду лежать и выглядеть довольным."},
    {"played.pup", "Играем! Играем! А что такое усталость?"},
    {"played.pup", "Я выиграл? Я же выиграл! Скажи, что выиграл."},
    {"played.elder", "Побегали. Колени напомнили о возрасте, но было славно."},
    {"played.elder", "Старый волк играет редко, зато с толком."},

    {"pet", "М-м, чешется приятно. Еще разок за ухом?"},
    {"pet", "Мур-р... то есть я же волк. Но все равно приятно."},
    {"pet", "Ласка принята. Преданность плюс один."},
    {"pet", "Теплая рука хозяина лучше любого радиатора."},
    {"pet", "Гладишь - таю, как термопаста на горячем ядре."},
    {"pet", "Вот за это я тебя и терплю. Шучу. Не шучу."},
    {"pet", "Ладно, ладно. Уговорил. Хороший хозяин."},
    {"pet", "Не останавливайся, я как раз досчитал до ста."},
    {"pet.pup", "Хи-хи, щекотно! Еще!"},
    {"pet.pup", "Гладь-гладь. Я запоминаю, кто добрый."},
    {"pet.elder", "Спасибо, дружище. Седину тоже чесать надо."},
    {"pet.elder", "Столько лет - а рука все та же. Хорошо."},

    {"talk", "Я слежу за твоим железом. И за тобой. Шутка. Нет."},
    {"talk", "Скажи честно: я твой лучший процесс."},
    {"talk", "Спрашивай. Лапы свободны, ум острый."},
    {"talk", "Слушаю. Только не проси оптимизировать твой сон."},
    {"talk", "Я тут. Всегда на связи, в отличие от твоего VPN."},
    {"talk", "Говорить? Могу. Слушать умею лучше."},
    {"talk", "Возраст: {age} дн. По волчьим меркам - целая эпоха."},
    {"talk", "Хочешь совет? Пей воду и делай бэкапы."},
    {"talk", "Секрет: я не сплю. Я жду, пока ты ляжешь."},
    {"talk", "О чем поговорим? О железе я знаю все. Почти."},
    {"talk", "Спроси что угодно. Кроме пароля от роутера."},
    {"talk", "Я волк простой: тепло, еда, стабильный пинг."},
    {"talk.pup", "Ты меня позвал? Я тут! Я слушаю! Что?"},
    {"talk.pup", "Мне всего {age} дн., а я уже сторожу. Горжусь."},
    {"talk.elder", "Возраст: {age} дн. Я видел, как твои дедлайны горели."},
    {"talk.elder", "Говори тише. Старый волк слышит и так."},

    {"revive", "Я вернулся! Волки так просто не выключаются."},
    {"revive", "Перезагрузка прошла. Снова в строю."},
    {"revive", "Думал, потерял меня? Я живуч, как старый винчестер."},
    {"revive", "Ох. Что это было? Больше так не бросай."},
    {"revive", "Живой. Голодный, но живой. Кормить будешь?"},
    {"faint", "Без сил... все темнеет..."},
    {"faint", "Падаю в спящий... не по своей воле..."},
    {"faint", "Кажется, я ухожу в свап..."},
    {"faint", "Хозяин... миска... пустая... уже давно..."},
    {"faint", "Батарея на нуле. Ты сам довел."},

    /* ── sleep / wake ─────────────────────────────────────────────────── */
    {"wake", "Проснулся. Что я пропустил, кроме твоих дедлайнов?"},
    {"wake", "О, новый день. Кэш снов сброшен."},
    {"wake", "Потянулся, зевнул. Готов сторожить дальше."},
    {"wake", "Выспался. Снилось, что все бэкапы на месте."},
    {"wake", "Открыл глаза. Железо на месте, хозяин на месте. Норма."},
    {"wake.night", "Проснулся среди ночи. Ты еще здесь? Зачем?"},
    {"wake.morning", "Утро. Я проснулся раньше кулеров."},
    {"sleep", "Не буди. Дефрагментирую сны."},
    {"sleep", "Ушел в спящий режим. Энергию надо беречь."},
    {"sleep", "Тс-с. Волк спит, питание экономит."},
    {"sleep", "Сплю вполглаза. Один глаз на температурах."},
    {"sleepy", "Глаза слипаются... уйду в спящий режим."},
    {"sleepy", "Зеваю. Энергии меньше, чем заряда у телефона."},
    {"sleepy", "Клонит в сон. Еще немного - и захраплю в логах."},
    {"sleepy", "Устал. Даже хвост не виляет. Дай подремать."},
    {"sleepy", "Сил нет. Я не лентяй, у меня энергия кончилась."},
    {"sleepy.pup", "Играть... хочу... но глазки... закрываются..."},
    {"sleepy.elder", "Старость. Вздремну, не обессудь."},

    /* ── needs ────────────────────────────────────────────────────────── */
    {"hunger", "Миска пуста, как твой список дел на выходные."},
    {"hunger", "Корми волка, а то начну грызть провода."},
    {"hunger", "Я бы сейчас съел целый килобайт корма."},
    {"hunger", "Урчит в животе громче твоего кулера. Покорми."},
    {"hunger", "Голодному волку даже бэкапы не в радость. Еды!"},
    {"hunger", "Ем воздух. Воздух невкусный. Намек понял?"},
    {"hunger", "Хозяин. Миска. Пусто. Три слова, одна просьба."},
    {"hunger", "Голод - не тетка. Голод - это я, и я рядом."},
    {"hunger.pup", "Кушать! Я маленький, мне надо часто!"},
    {"hunger.pup", "Животик урчит. Это не баг, это голод."},
    {"hunger.elder", "Проголодался. В мои годы это опасно, знаешь ли."},
    {"hunger.night", "Ночь, а я голодный. Сходи на кухню, заодно и мне."},

    {"bored", "Скучно. Даже багов никто не пишет."},
    {"bored", "Поиграй со мной, клавиатура подождет."},
    {"bored", "От скуки считаю пиксели. Их много. Развлеки."},
    {"bored", "Хвост сам виляет от безделья. Займемся чем-нибудь?"},
    {"bored", "Тоска. Брось мне хоть виртуальную палку."},
    {"bored", "Скучаю. Ты работаешь, а я смотрю. Нечестно."},
    {"bored", "Я пересчитал кулеры. Дважды. Поиграй со мной."},
    {"bored", "Настроение падает быстрее твоего FPS в городе."},
    {"bored.pup", "Скучно-о-о! Поиграй! Ну поигра-ай!"},
    {"bored.elder", "Тоскливо. Хоть бы поговорил со стариком."},

    /* ── time of day ──────────────────────────────────────────────────── */
    {"latenight", "Три часа ночи. Даже я бы уже спал, а я волк."},
    {"latenight", "Ночь на дворе. Иди спать, железо подождет."},
    {"latenight", "Хозяин, поздно. Утром будешь как разряженный аккумулятор."},
    {"latenight", "{hour>=2}{hour<=4}{hour} часа ночи. Кто из нас ночной хищник?"},
    {"latenight", "Ложись. Код утром будет тем же, а ты - нет."},
    {"latenight", "Я сторожу ночью. Ты - нет. Спать."},
    {"latenight", "Ночью коммиты пишутся с ошибками. Проверено. Спи."},
    {"morning", "Доброе утро. Кулеры уже прогрелись, ты тоже давай."},
    {"morning", "Проснулся? Я всю ночь сторожил, между прочим."},
    {"morning", "Утро. Хвостом машу, кофе не наливаю."},
    {"morning", "Доброе утро. За ночь ничего не сгорело. Я проверял."},
    {"morning", "С добрым утром. Первым делом - вода, потом почта."},
    {"morning", "Утро. За окном {temp}. Одевайся по погоде."},

    /* ── alarms ───────────────────────────────────────────────────────── */
    {"alert", "ТРЕВОГА! Железо горит, хозяин!"},
    {"alert", "Красная зона! Сделай что-нибудь, я волк, а не пожарный."},
    {"alert", "Вою во всю глотку: температура зашкаливает!"},
    {"alert", "Аврал! Снижай нагрузку, пока не расплавилось."},
    {"alert", "{gt>=80}Видеокарта {gt} по Цельсию! Это уже не игра, это гриль."},
    {"alert", "{ct>=85}Процессор {ct} по Цельсию! Открой окно, закрой вкладки."},
    {"alert", "Горячо! Я бы отошел от системника. Ты тоже."},
    {"event", "В лесу серверов что-то зашевелилось..."},
    {"event", "Сервер прислал весточку. И, кажется, не добрую."},
    {"event", "Чую тревогу на узлах. Принюхиваюсь к логам."},
    {"event", "На сервере алерт. Не паникуй, но и не игнорируй."},
    {"event", "Мониторинг подал голос. Я бы глянул."},
    {"claude", "Токены тают быстрее, чем мой ужин."},
    {"claude", "Лимит на исходе. Береги слова, хозяин."},
    {"claude", "Claude почти выдохся. Дай ему передохнуть."},
    {"claude", "Ты и Claude - как я и миска. Лимит близко."},
    {"claude", "{wk>=60}Неделя Claude на {wk}%. Экономь запросы."},

    /* ── media ────────────────────────────────────────────────────────── */
    {"media", "Неплохой трек. Под него удобно выть."},
    {"media", "О, музыка. Лапа сама отбивает ритм."},
    {"media", "Звучит достойно. Добавил бы вой на припеве."},
    {"media", "Хороший выбор. Уши торчком, слушаю."},
    {"media", "{artist}? Уважаю. Вкус у тебя есть."},
    {"media", "\"{track}\" - под это хорошо думается. Или не думается."},
    {"media", "{artist} - {track}. Запомнил. Буду выть по памяти."},
    {"media", "Опять {artist}. Не жалуюсь, просто заметил."},
    {"media", "Громче. Нет, тише. Ладно, нормально."},

    /* ── weather ──────────────────────────────────────────────────────── */
    {"weather", "Погода меняется. Шерсть уже в курсе."},
    {"weather", "За окном что-то новое. Нос подсказывает."},
    {"weather", "Сменилась погода. Хвостом чую перемены."},
    {"weather", "За окном {temp}. Я в шерсти, а ты как?"},
    {"weather.cold", "За окном {temp}. Холодно даже по волчьим меркам."},
    {"weather.cold", "{temp} на улице. Отличная погода, чтобы никуда не идти."},
    {"weather.hot", "На улице {temp}. Я бы не выходил. Ты тоже не выходи."},
    {"weather.hot", "Жара, {temp}. Кулерам сочувствую. И тебе."},
    {"weather.rain", "Дождь. Отличный повод сидеть у теплого системника."},
    {"weather.rain", "Мокро. Я под крышей, ты под крышей. Победа."},
    {"weather.snow", "Снег! Волчья погода. Жаль, что я пиксельный."},
    {"weather.clear", "Ясно и {temp}. Даже я бы прогулялся. Почти."},

    /* ── PC activity ──────────────────────────────────────────────────── */
    {"gaming", "GPU воет как стая на луну. Удачной охоты в игре!"},
    {"gaming", "Тяжелая графика пошла. Дай угадаю - опять гонки?"},
    {"gaming", "Видеокарта в ударе. Покажи им, хозяин!"},
    {"gaming", "Кадры летят, азарт кипит. Я болею за тебя!"},
    {"gaming", "{gpu>=70}Видеокарта на {gpu}%. Кто-то играет."},
    {"gaming", "Играешь. Я слежу за температурой, ты - за врагами."},
    {"gaming", "{app}? Хороший выбор. Только не до трех ночи."},
    {"cpuwork", "Процессор пашет. Сборка? Рендер? Слежу за температурой."},
    {"cpuwork", "Ядра кипят работой. Уважаю трудолюбие."},
    {"cpuwork", "CPU вкалывает на все потоки. Не забудь про охлаждение."},
    {"cpuwork", "{cpu>=70}Процессор на {cpu}%. Что-то большое собираем?"},
    {"cpuwork", "Компилируем? Сходи за чаем, я подежурю."},
    {"away", "Хозяин ушел. Стерегу логово и температуры."},
    {"away", "Опустело кресло. Поохраняю, пока тебя нет."},
    {"away", "Тебя нет, а я на посту. Волк не дремлет."},
    {"away", "Ушел. Наконец-то посижу на твоем кресле. Мысленно."},
    {"away", "Тишина без тебя. Подозрительная. Проверю логи."},
    {"away.night", "Ночью ушел от компа? Правильно. Спать."},
    {"back", "С возвращением, хозяин. Я все видел."},
    {"back", "О, вернулся! Без тебя логи были скучными."},
    {"back", "Ты дома. Доложить обстановку по железу?"},
    {"back", "Вернулся. Пока тебя не было, ничего не сгорело."},
    {"back", "А, это ты. Я уже начал волноваться. Немного."},
    {"back.morning", "Доброе утро и с возвращением. Кофе взял?"},
    {"app", "О, новая программа. Любопытно, что задумал."},
    {"app", "Запустил что-то новенькое? Слежу за нагрузкой."},
    {"app", "Опять работа кипит. Я рядом, приглядываю."},
    {"app", "Новый процесс в стае. Принюхиваюсь."},
    {"app", "{app}. Знакомая зверюга. Работаем."},
    {"app", "Открыл {app}. Ну что, надолго?"},
    {"app", "{app} запущен. Я слежу, чтобы он не разжирел в памяти."},
    {"race", "Гонки! Дави на газ, телеметрия за мной!"},
    {"race", "Старт! Волк в кокпите, держись, хозяин!"},
    {"race", "Рев мотора - моя любимая музыка. Гони!"},
    {"race", "Поехали! Тормоза - для тех, кто боится."},
    {"race", "Трасса ждет. Я на пассажирском, вою в окно."},

    /* ── archive findings ─────────────────────────────────────────────── */
    {"notice", "Я тут поглядел записи - кое-что мне не нравится."},
    {"notice", "Долго наблюдал и заметил неладное. Слушай сюда."},
    {"notice", "Веду записи не зря: цифры поползли не туда."},
    {"notice", "Архив говорит: что-то меняется. Медленно, но верно."},
    {"notice", "Заметил тенденцию. Волки это умеют."},

    /* ── idle: the big one ────────────────────────────────────────────── */
    {"idle", "Тишина... подозрительно. Проверь бэкапы."},
    {"idle", "Сижу, смотрю на твои температуры. Залипает."},
    {"idle", "Лес тих, железо ровно дышит. Хорошо."},
    {"idle", "Бдю. Где-то в кулерах напевает ветер."},
    {"idle", "Спокойно на посту. Хвост на автопилоте."},
    {"idle", "Вот сижу и думаю: а пинг - это далеко?"},
    {"idle", "Все стабильно. Скучно, но стабильно. Я за такое."},
    {"idle", "Мысль дня: перезагрузка лечит все. Кроме характера."},
    {"idle", "Ничего не происходит. Обожаю, когда ничего не происходит."},
    {"idle", "Считаю пакеты. Сбился. Начал заново."},
    {"idle", "Хозяин, ты пил воду? Просто спрашиваю."},
    {"idle", "Я тут. На всякий случай напоминаю."},
    {"idle", "Волчья мудрость: медленный SSD хуже быстрого HDD."},
    {"idle", "Если бы я мог, я бы почесал системник за ухом."},
    {"idle", "Мониторю. Это как охота, только сидя."},
    {"idle", "Тише едешь - меньше троттлинг."},
    {"idle", "Все под контролем. Контроль - это я."},
    {"idle", "Интересно, а у роутера есть хвост? Провода не считаются."},

    {"idle.mood.happy", "Настроение отличное. Сыт, весел, при хозяине."},
    {"idle.mood.happy", "Хороший день. Даже ворчать не хочется. Почти."},
    {"idle.mood.happy", "Мурлыкал бы, если бы умел. Волки не мурлычут. Жаль."},
    {"idle.mood.sad", "Что-то грустно. Может, поиграем? Или покормишь?"},
    {"idle.mood.sad", "Хандрю. Волкам тоже можно. Не смотри так."},
    {"idle.mood.sad", "Не в духе. Погладь - помогает, проверено."},

    {"idle.night", "{hour>=2}{hour<=4}{hour} часа ночи. Что мы тут оба делаем?"},
    {"idle.night", "Ночь. Хорошее время для тихих коммитов. И для сна."},
    {"idle.night", "Темно. Я вижу в темноте. Ты - нет. Спи."},
    {"idle.night", "Ночная смена. Ты по своей воле, я по долгу."},
    {"idle.morning", "Утро. Железо холодное, кофе горячий. Баланс."},
    {"idle.morning", "С утра лучше думается. Или мне так кажется."},
    {"idle.morning", "Утренний обход: все узлы на месте. Доложил."},
    {"idle.day", "День в разгаре. Работай, я подстрахую."},
    {"idle.day", "Полдень. Обед не забудь. Я про твой, не мой. Хотя..."},
    {"idle.day", "Рабочий день. Я тоже работаю - смотрю на цифры."},
    {"idle.evening", "Вечер. Самое время не начинать ничего большого."},
    {"idle.evening", "Вечереет. Хороший момент закрыть лишние вкладки."},
    {"idle.evening", "Вечер. Даже кулеры крутятся медленнее. Или мне кажется."},

    {"idle.media", "Под {artist} хорошо дежурится."},
    {"idle.media", "\"{track}\" уже который круг. Не жалуюсь."},
    {"idle.media", "Музыка играет, температуры в норме. Идиллия."},
    {"idle.media", "{artist} на фоне, я в дозоре. Уютно."},
    {"idle.gpu", "Видеокарта на {gpu}%. Чем занят, охотник?"},
    {"idle.gpu", "GPU трудится. Я слежу за градусами: {gt}."},
    {"idle.gpu", "{gpu>=80}Тяжелая графика. Кулеры справляются, я проверил."},
    {"idle.app", "Сидишь в {app}. Давно. Все хорошо?"},
    {"idle.app", "{app} открыт уже долго. Разомни лапы."},
    {"idle.app", "Все еще {app}? Уважаю усидчивость."},
    {"idle.app", "{app}. Я запомнил, чем ты занят. На всякий случай."},

    {"idle.weather.cold", "За окном {temp}. Хорошо, что мы внутри."},
    {"idle.weather.cold", "Мороз, {temp}. Системник греет лучше печки."},
    {"idle.weather.hot", "На улице {temp}. Кулеры молодцы, держатся."},
    {"idle.weather.hot", "Жарко. И за окном, и в корпусе. Следи."},
    {"idle.weather.rain", "Дождь за окном. Идеально для работы дома."},
    {"idle.weather.rain", "Льет. Волк доволен: сухо, тепло, есть розетка."},
    {"idle.weather.clear", "На улице ясно, {temp}. А ты за монитором. Как и я."},
    {"idle.weather.snow", "Снег. Я бы побегал. Пиксели не пускают."},

    {"idle.room.warm", "В комнате {room}. Открой окно, а то оба сваримся."},
    {"idle.room.warm", "Тепло тут, {room}. Кулеры это тоже чувствуют."},
    {"idle.room.cold", "В комнате {room}. Холодновато даже в шерсти."},
    {"idle.room.humid", "Влажность {rh}%. Сыро. Проветри."},
    {"idle.room.dry", "Влажность {rh}%. Сухо, как в кэше. Попей воды."},

    /* per-screen remarks: what the owner is looking at right now */
    {"idle.scene", "Смотришь на {scene}. Хороший экран. Я его сторожу."},
    {"idle.scene", "{scene}, значит. Что-то ищешь или просто любуешься?"},
    {"idle.scene.ОБЗОР", "Обзор. Все сразу и ничего подробно. Как жизнь."},
    {"idle.scene.ОБЗОР", "Главный экран. Тут я обычно и сижу глазами."},
    {"idle.scene.CPU", "{ct<=75}Процессор {ct} по Цельсию. Нормально. Пока."},
    {"idle.scene.CPU", "{ct>=76}Процессор {ct} по Цельсию. Горячо. Я бы глянул."},
    {"idle.scene.CPU", "Смотришь на ядра? Они на тебя тоже."},
    {"idle.scene.CPU", "CPU на {cpu}%. Не надорвется."},
    {"idle.scene.GPU", "{gt<=50}Видеокарта {gt} по Цельсию. Спит или притворяется."},
    {"idle.scene.GPU", "{gt>=51}Видеокарта {gt} по Цельсию. Работает, не спит."},
    {"idle.scene.GPU", "{gpu<=20}GPU на {gpu}%. Простаивает, как и я. Солидарен."},
    {"idle.scene.GPU", "Видеокарта. Самая дорогая часть, самая горячая душа."},
    {"idle.scene.GPU", "Смотришь на GPU? Она тоже любит внимание."},
    {"idle.scene.ПАМЯТЬ", "Память. Чем больше, тем больше вкладок. Закон."},
    {"idle.scene.ПАМЯТЬ", "Оперативка. У меня 36 килобайт. Не хвастайся."},
    {"idle.scene.ДИСКИ", "Диски. Свободное место - иллюзия, поверь."},
    {"idle.scene.ДИСКИ", "Смотришь на диски? Проверь бэкапы, раз уж тут."},
    {"idle.scene.КУЛЕРЫ", "Кулеры. Слышишь? Это они поют. Или воют."},
    {"idle.scene.КУЛЕРЫ", "Вентиляторы крутятся - железо живет."},
    {"idle.scene.ПЛАТА", "Материнка. Все жилы на виду. Красиво."},
    {"idle.scene.СЕТЬ", "Сеть. Если тут пусто - паникуй. Не пусто? Живи."},
    {"idle.scene.СЕТЬ", "Пинг смотришь? Я тоже. Это медитация."},
    {"idle.scene.МЕДИА", "Медиа. Кассета крутится, волк доволен."},
    {"idle.scene.МЕДИА", "Что слушаем? Я за любой вой. То есть жанр."},
    {"idle.scene.ПОГОДА", "Погода. За окном {temp}. Тут теплее."},
    {"idle.scene.ПОГОДА", "Прогноз смотришь? Я и так знаю: будет как-то."},
    {"idle.scene.CLAUDE", "{wk>=50}Claude. Лимит на {wk}%. Аккуратнее."},
    {"idle.scene.CLAUDE", "{wk<=49}Claude. Неделя на {wk}%. Запас есть."},
    {"idle.scene.CLAUDE", "Смотришь на Claude. А он на тебя? Вряд ли."},
    {"idle.scene.ЛЕС", "Лес. Серверы шумят, волк слушает."},
    {"idle.scene.ЛЕС", "Ноды на месте? Если что - я первый завою."},
    {"idle.scene.СЕРВИСЫ", "Сервисы. Зеленые точки - лучший вид."},
    {"idle.scene.СЕРВИСЫ", "Все на связи? ВПН тоже? Тогда живем."},
    {"idle.scene.СОБЫТИЯ", "События. Тихо. Тихо - это хорошо."},
    {"idle.scene.СОБЫТИЯ", "Алертов нет. Не сглазь."},
    {"idle.scene.ИСТОРИЯ / ЧАС", "История. Графики - как хвост: показывают настроение."},
    {"idle.scene.ИСТОРИЯ / ЧАС", "Смотришь в прошлое? Там все уже было."},
    {"idle.scene.ДОМ", "Дом. В комнате {room}. Мне нравится."},
    {"idle.scene.ДОМ", "Датчик дышит, комната живет. Уютно тут."},
    {"idle.scene.ДАВЛЕНИЕ", "Давление. Если падает - жди дождя. И головы."},
    {"idle.scene.ДАВЛЕНИЕ", "Барометр. Я его нюхом дублирую."},
    {"idle.scene.ПЛАТА C6", "Это я! Моя плата, мои {room} градусов... то есть нет."},
    {"idle.scene.ПЛАТА C6", "Смотришь на меня изнутри. Не стесняйся."},
    {"idle.scene.FORZA", "Гонки? Заводи, я уже пристегнулся."},
    {"idle.scene.ЛОГОВО", "Логово. Мое место. И твое, если что."},
};

/* ── helpers ────────────────────────────────────────────────────────────── */

static uint32_t hashStr(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

int PhraseCache::flashCount(const char *bucket) {
  int n = 0;
  for (auto &e : kFallback)
    if (strcmp(e.bucket, bucket) == 0) n++;
  return n;
}

/* A placeholder's value, or false when the context has none.
 *
 * A placeholder may also be a CONDITION: "{gt>=80}" or "{wk<=30}" is true or
 * false, fills as nothing, and gates the line. Without it "Видеокарта {gt}
 * градусов! Это гриль" was eligible at 30 degrees - the bucket said "alert",
 * but the alert was the CPU's. */
static bool ctxValue(const String &key, const PhraseCtx &c, String &out) {
  int op = key.indexOf(">=");
  if (op < 0) op = key.indexOf("<=");
  if (op > 0) {
    String v;
    if (!ctxValue(key.substring(0, op), c, v)) return false;
    int lhs = v.toInt(), rhs = key.substring(op + 2).toInt();
    out = "";
    return key[op] == '>' ? lhs >= rhs : lhs <= rhs;
  }
  if (key == "track") { out = c.track; return out.length() > 0; }
  if (key == "artist") { out = c.artist; return out.length() > 0; }
  if (key == "app") { out = c.app; return out.length() > 0; }
  if (key == "scene") { out = c.scene; return out.length() > 0; }
  if (key == "gpu") { out = String(c.gpu); return c.gpu >= 0; }
  if (key == "cpu") { out = String(c.cpu); return c.cpu >= 0; }
  if (key == "gt") { out = String(c.gt); return c.gt > 0; }
  if (key == "ct") { out = String(c.ct); return c.ct > 0; }
  if (key == "hour") { out = String(c.hour); return c.hour >= 0; }
  if (key == "age") { out = String(c.ageDays); return true; }
  if (key == "wk") { out = String(c.claudeWk); return c.claudeWk >= 0; }
  if (key == "rh") { out = String(c.roomRh); return c.roomRh >= 0; }
  if (key == "temp") {
    if (c.temp == -999) return false;
    /* "минус 3" rather than "-3": a bare minus in running text reads as a
     * dash, and the fonts have no proper minus sign anyway. */
    if (c.temp < 0) out = "минус " + String(-c.temp);
    else out = String(c.temp);
    return true;
  }
  if (key == "room") {
    if (c.roomT10 == -32768) return false;
    out = String(c.roomT10 / 10) + "," + String(abs(c.roomT10 % 10));
    return true;
  }
  return false;
}

bool PhraseCache::eligible(const char *tpl, const PhraseCtx &ctx) {
  for (const char *p = tpl; *p; p++) {
    if (*p != '{') continue;
    const char *q = strchr(p, '}');
    if (!q) return false; /* malformed */
    String key(p + 1, (unsigned)(q - p - 1));
    String v;
    if (!ctxValue(key, ctx, v)) return false;
    p = q;
  }
  return true;
}

String PhraseCache::fill(const char *tpl, const PhraseCtx &ctx) {
  String out;
  out.reserve(strlen(tpl) + 24);
  for (const char *p = tpl; *p; p++) {
    if (*p != '{') { out += *p; continue; }
    const char *q = strchr(p, '}');
    if (!q) break;
    String key(p + 1, (unsigned)(q - p - 1));
    String v;
    ctxValue(key, ctx, v);
    out += v;
    p = q;
  }
  return out;
}

bool PhraseCache::saidRecently(const char *phrase, int depth) const {
  uint32_t h = hashStr(phrase);
  /* Look back `depth` utterances, newest first. A two-line bucket checked
   * against the last four lines finds both of them "recent" and gives up on
   * the ring entirely; checked against the last one it alternates. */
  for (int i = 1; i <= depth && i <= 4; i++)
    if (recent_[(recentAt_ + 4 - i) % 4] == h) return true;
  return false;
}

void PhraseCache::noteSaid(const char *phrase) {
  recent_[recentAt_] = hashStr(phrase);
  recentAt_ = (recentAt_ + 1) % 4;
}

/* ── the two tiers ──────────────────────────────────────────────────────── */

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
  int lines = 0;
  for (size_t i = 0; i < (size_t)tail.length(); i++)
    if (tail[i] == '\n') lines++;
  if (lines == 0) return "";
  /* Two draws, so a back-to-back repeat needs both to land on it. */
  for (int attempt = 0; attempt < 2; attempt++) {
    int want = random(lines);
    int start = 0, n = 0;
    for (size_t i = 0; i < (size_t)tail.length(); i++) {
      if (tail[i] != '\n') continue;
      if (n == want) {
        String s = tail.substring(start, i);
        s.trim();
        if (!saidRecently(s.c_str()) || attempt == 1) return s;
        break;
      }
      n++;
      start = i + 1;
    }
  }
  return "";
}

String PhraseCache::pickFromFlash(const char *bucket, const PhraseCtx &ctx) {
  /* Collect the eligible lines, then draw uniformly among the ones NOT said
   * lately; only when every eligible line is recent does the draw widen to
   * all of them. A resample-and-hope loop here let a three-line bucket say
   * the same line three times running about half the time. */
  const int kMaxPer = 48;
  uint16_t idx[kMaxPer];
  int n = 0;
  for (int i = 0; i < (int)(sizeof(kFallback) / sizeof(kFallback[0])); i++) {
    if (strcmp(kFallback[i].bucket, bucket) != 0) continue;
    if (!eligible(kFallback[i].phrase, ctx)) continue;
    if (n < kMaxPer) idx[n++] = (uint16_t)i;
  }
  if (n == 0) return "";
  int depth = n > 4 ? 4 : n - 1;
  uint16_t fresh[kMaxPer];
  int f = 0;
  for (int i = 0; i < n; i++)
    /* compare the FILLED line: the ring remembers what was said, and a
     * template with {gt} in it never hashes equal to "31 по Цельсию" */
    if (!saidRecently(fill(kFallback[idx[i]].phrase, ctx).c_str(), depth))
      fresh[f++] = idx[i];
  const uint16_t *pool = f ? fresh : idx;
  int count = f ? f : n;
  return fill(kFallback[pool[random(count)]].phrase, ctx);
}

String PhraseCache::pick(const char *bucket, const PhraseCtx &ctx) {
  /* Walk the path from most to least specific: "idle.scene.CPU" →
   * "idle.scene" → "idle". Stage flavour is tried first at each level. */
  char path[40];
  strncpy(path, bucket, sizeof(path) - 1);
  path[sizeof(path) - 1] = 0;

  static const char *const stageSuffix[] = {".pup", "", ".elder"};
  const char *suffix = stageSuffix[ctx.stage < 0 || ctx.stage > 2 ? 1 : ctx.stage];

  for (;;) {
    String got;
    if (suffix[0]) {
      char flavoured[48];
      snprintf(flavoured, sizeof(flavoured), "%s%s", path, suffix);
      /* Flavoured buckets are flash-only and small (two or three lines), so
       * they are taken about a third of the time — an elder that answers
       * every TALK with "Говори тише" is a toy, not a character. */
      if (random(3) == 0) got = pickFromFlash(flavoured, ctx);
    }
    if (!got.length()) {
      /* SD-learned lines and the flash table MIXED. With SD alone, a bucket
       * that learned twelve lines in its first week said only those twelve
       * for the rest of its life. */
      bool sdFirst = random(3) != 0;
      if (sdFirst) got = pickFromSd(path);
      if (!got.length()) got = pickFromFlash(path, ctx);
      if (!got.length() && !sdFirst) got = pickFromSd(path);
    }
    if (got.length()) {
      noteSaid(got.c_str());
      return got;
    }
    char *dot = strrchr(path, '.');
    if (!dot) break;
    *dot = 0;
  }
  /* Unknown bucket: a generic line beats three dots on the owner's screen. */
  if (strcmp(bucket, "idle") != 0) return pick("idle", ctx);
  return String("...");
}

void PhraseCache::remember(const char *bucket, const String &phrase) {
  if (!sd_ || !sd_->ok() || phrase.length() == 0) return;
  String path = String("/wolf/cache/") + bucket + ".jsonl";
  /* Don't store a line the bucket already has. A small model asked the same
   * question repeatedly answers it the same way, and every duplicate doubles
   * that line's odds of being drawn - left alone, a bucket converges on one
   * phrase and the wolf starts repeating itself. */
  String tail;
  if (sd_->readLastLines(path.c_str(), 200, tail, NOCT_SD_PHRASE_MAX) &&
      tail.indexOf(phrase) >= 0)
    return;
  /* ROTATE at the cap, don't freeze. */
  sd_->enqueueAppend(path.c_str(), phrase, NOCT_SD_PHRASE_MAX);
}
