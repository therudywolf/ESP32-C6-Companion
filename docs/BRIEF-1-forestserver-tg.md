# Задача: климат из ForestHome — на forestserver.ru, в алерты и в бота

Скопируй это целиком в Claude Code, запущенный в `~/Projects` (нужен доступ
к `forestserver-home/` и `monitoring/`).

---

## Контекст: что уже сделано и работает

Плата ESP32-C6 стала Zigbee-координатором и слушает датчик **Aqara WSDCGQ11LM**
(температура + влажность + барометр, CR2032). Показания идут: датчик → плата →
NocturneServer на PC-RUDYWOLF.

**Звено до Prometheus уже готово и проверено.** NocturneServer пишет
`C:\ProgramData\grafana-agent\textfile\forest_home.prom`, а windows_exporter
подхватывает этот каталог в течение минуты. Живой файл прямо сейчас:

```
forest_home_temp_celsius{sensor="ForestHome"} 23.0
forest_home_humidity_percent{sensor="ForestHome"} 56
forest_home_battery_percent{sensor="ForestHome"} 95
forest_home_pressure_hpa{sensor="ForestHome"} 994
forest_home_reading_age_seconds{sensor="ForestHome"} 0
forest_home_pressure_trend_3h_hpa 1.0
forest_home_temp_trend_3h_celsius -0.4
forest_home_humidity_trend_3h_percent 3
```

Три последние — **барическая тенденция за 3 часа**, посчитанная платой по
архиву на карте. Они без метки `sensor`: барометр один и атмосфера одна.

Метки: `job="integrations/windows_exporter"`, `host="PC-RUDYWOLF"`.
Ничего дописывать на стороне ПК **не надо** — эта часть закрыта.

### Почему прогноз строится ТОЛЬКО на давлении

Датчик отдаёт три величины, и лишь одна из них про улицу. Здание не
герметично, поэтому давление в комнате повторяет атмосферное с точностью до
десятых гПа — **барометр в комнате предсказывает погоду**. А термометр в
комнате описывает батарею отопления, гигрометр — душ и открытое окно.

Поэтому в правилах ниже погодные выводы сделаны из
`forest_home_pressure_trend_3h_hpa` и ни из чего больше. Не добавляй правил
вида «температура в комнате падает → похолодало на улице»: зимой это значит,
что кто-то открыл форточку.

**Почему тенденция, а не само давление.** Абсолютное значение говорит о твоей
высоте над морем, скорость — о том, что делает небо. Правило, написанное на
`forest_home_pressure_hpa`, требовало бы своего порога для каждого города.

**Почему три часа.** Это метеорологический стандарт: именно трёхчасовую
тенденцию передаёт METAR, и все опубликованные пороги приведены к этому
интервалу. Взять другое окно и оставить те же числа — значит занять у шкалы
авторитет, не взяв её единицы.

Шкала (гПа за 3 ч, полосы ВМО), уже реализованная на плате:

| Δ за 3 ч | что это | что будет |
|---|---|---|
| < −6.0 | очень резкое падение | фронт уже приходит |
| −6.0…−3.6 | резкое падение | непогода, ветер |
| −3.5…−1.6 | падение | вероятен дождь |
| −1.5…−0.5 | медленное падение | пасмурнее |
| ±0.5 | ровно | без перемен |
| +0.5…+1.5 | медленный рост | проясняется |
| +1.6…+3.5 | рост | ясная погода |
| > +3.6 | резкий рост | резкое похолодание, особенно ночью |

### Одна вещь, которую нельзя потерять

`forest_home_reading_age_seconds` — отдельная метрика намеренно.

WSDCGQ11LM — спящее устройство: оно шлёт отчёт при изменении (≈ ±0.5 °C, ±6 %
влажности) плюс контрольный примерно **раз в 50–60 минут**. Опрашивать его
бесполезно — оно слушает эфир только сразу после собственной передачи, а
прошивка Xiaomi игнорирует configure-reporting. Значит:

* Показание может законно быть часовой давности.
* Если датчик сдохнет (села батарейка, ушёл из сети), метрика **не исчезнет** —
  Prometheus будет отдавать последнее значение ещё пять минут, а потом серия
  просто пропадёт. Всё это время «23 °C» выглядит как свежая правда.

Поэтому **каждое правило алерта и каждый показ обязаны учитывать возраст.**
Температура без возраста — та же ложь, которую плата отказывается говорить на
своём экране. Дальше в правилах это зашито; не упрощай их, убрав guard.

---

## Задача A — блок «дом» на forestserver.ru

Репозиторий: `~/Projects/forestserver-home`

### A1. `stats.sh` — забрать метрики из Prometheus

В скрипте уже есть хелпер `qp 'promql'` (POST в Prometheus, возвращает одно
значение строкой). Добавь рядом с остальными запросами, ДО блока
`cat > /usr/share/nginx/html/stats.json.tmp`:

```sh
  # ===== ForestHome (Zigbee-датчик на плате Nocturne C6) =====
  # Возраст тянем отдельно: датчик спит и отчитывается раз в ~50 минут, так
  # что часовое показание — норма, а не сбой. Без возраста фронт не сможет
  # отличить «в комнате 23» от «в комнате было 23 позавчера».
  HM_TEMP=$(qp 'forest_home_temp_celsius')
  HM_HUM=$(qp 'forest_home_humidity_percent')
  HM_BATT=$(qp 'forest_home_battery_percent')
  HM_PRESS=$(qp 'forest_home_pressure_hpa')
  HM_AGE=$(qp 'forest_home_reading_age_seconds')
```

В сам JSON добавь секцию (рядом с `"nb":{...}`):

```
  "home":{"temp":"${HM_TEMP:-}","hum":"${HM_HUM:-}","batt":"${HM_BATT:-}","press":"${HM_PRESS:-}","age_s":"${HM_AGE:--1}"},
```

Пустая строка, если метрики нет — так же, как сделано для ноутбука. Не подставляй
0: ноль это валидная температура, а «нет данных» — не ноль.

### A2. `index.html` — секция и карточки

Разметку клади после секции `NB-RUDYWOLF` (около строки 355), в том же стиле:

```html
  <h2 id="hmHead">дом <span class="meta" id="hmMeta">ForestHome · aqara WSDCGQ11LM · zigbee</span><a class="hlp" href="/help.html#home" title="Что это за раздел">?</a></h2>
  <div class="grid g-summary">
    <div class="card" id="hmTempCard"><div class="label">температура</div><div class="value"><span id="hTemp">—</span><small>°C</small></div></div>
    <div class="card" id="hmHumCard"><div class="label">влажность</div><div class="value"><span id="hHum">—</span><small>%</small></div><div class="bar"><i id="hHumBar"></i></div></div>
    <div class="card"><div class="label">давление</div><div class="value"><span id="hPress">—</span><small>мм рт.ст.</small></div></div>
    <div class="card" id="hmBattCard"><div class="label">батарея</div><div class="value"><span id="hBatt">—</span><small>%</small></div><div class="bar"><i id="hBattBar"></i></div></div>
    <div class="card" id="hmAgeCard"><div class="label">свежесть</div><div class="value"><span id="hAge">—</span><small id="hAgeLabel"></small></div></div>
  </div>
```

JS — рядом с блоком `// router`, используя существующие `setCard` / `fmtNum`:

```js
    // ===== дом =====
    const h = d.home || {};
    const hAge = parseFloat(h.age_s);
    // Больше часа — датчик пропустил свой интервал отчёта. Гасим ВСЮ секцию,
    // а не только возраст: показать бодрые «23 °C» рядом с тусклой пометкой
    // «2 часа назад» — значит предложить читателю не заметить её.
    const hStale = !Number.isFinite(hAge) || hAge > 3600;
    setCard(null, "hTemp",  null,      h.temp,  v => fmtNum(v, 1));
    setCard("hmHumCard", "hHum", "hHumBar",     h.hum,   v => fmtNum(v, 0));
    setCard("hmBattCard","hBatt","hBattBar",    h.batt,  v => fmtNum(v, 0));
    const press = parseFloat(h.press);
    // гПа на проводе, мм рт.ст. на экране — так пишут в прогнозе погоды
    $("hPress").textContent = Number.isFinite(press) ? (press * 0.750062).toFixed(0) : "—";
    $("hAge").textContent = Number.isFinite(hAge)
      ? (hAge < 90 ? "сейчас" : hAge < 3600 ? Math.round(hAge / 60) : Math.round(hAge / 3600))
      : "—";
    $("hAgeLabel").textContent = !Number.isFinite(hAge) ? "нет данных"
      : hAge < 90 ? "" : hAge < 3600 ? "мин назад" : "ч назад";
    $("hmAgeCard").classList.toggle("bad", hStale);
    const hmSection = $("hmHead");
    if (hmSection) hmSection.nextElementSibling.style.opacity = hStale ? ".45" : "";
    const hmMeta = $("hmMeta");
    if (hmMeta) hmMeta.textContent = Number.isFinite(hAge)
      ? "ForestHome · aqara WSDCGQ11LM · zigbee"
      : "ForestHome · датчик молчит, метрик нет";
```

Ключевые решения, не переделывай их «проще»:

* **Гасится вся секция**, а не только карточка возраста. Бодрые цифры рядом с
  тусклой пометкой «2 часа назад» читаются как свежие.
* **Секция не прячется**, когда данных нет. Спрятанный блок читается как
  «датчика не существует», а он существует — просто молчит. Состояние пишем
  в подзаголовок, как уже сделано для ноутбука.
* Давление переводим в мм рт. ст. на фронте: на проводе гПа (так отдаёт ZCL).

Не забудь дописать якорь `#home` в `help.html` — на него ссылается «?».

---

## Задача B — алерты в Telegram

Репозиторий: `~/Projects/monitoring`

### B1. Правила: `prometheus/rules/alerts.yml`

Новая группа в конец файла:

```yaml
  # ============================================================
  # ДОМ — Zigbee-датчик ForestHome (Aqara WSDCGQ11LM) через плату Nocturne C6
  # ============================================================
  - name: home-climate
    interval: 60s
    rules:
      # Пороги — свои, не «стандартные»: это жилая комната, а не серверная.
      # Guard по возрасту обязателен в КАЖДОМ правиле: датчик спит и отчитывается
      # раз в ~50 минут, поэтому Prometheus ещё пять минут отдаёт последнее
      # значение после того, как датчик замолчал. Без guard мы будем будить
      # человека показанием, которому час.
      - alert: HomeTooHot
        expr: forest_home_temp_celsius > 27 and forest_home_reading_age_seconds < 5400
        for: 10m
        labels: { severity: warning }
        annotations:
          summary: "Дома жарко: {{ $value | printf \"%.1f\" }} °C ({{ $labels.sensor }})"
          description: "Порог 27 °C. Показанию меньше полутора часов."

      - alert: HomeTooCold
        expr: forest_home_temp_celsius < 17 and forest_home_reading_age_seconds < 5400
        for: 10m
        labels: { severity: warning }
        annotations:
          summary: "Дома холодно: {{ $value | printf \"%.1f\" }} °C ({{ $labels.sensor }})"
          description: "Порог 17 °C."

      - alert: HomeTooDamp
        expr: forest_home_humidity_percent > 65 and forest_home_reading_age_seconds < 5400
        for: 30m
        labels: { severity: warning }
        annotations:
          summary: "Дома сыро: {{ $value }} % ({{ $labels.sensor }})"
          description: "Порог 65 %. Держится полчаса — это уже про плесень, а не про душ."

      - alert: HomeTooDry
        expr: forest_home_humidity_percent < 25 and forest_home_reading_age_seconds < 5400
        for: 30m
        labels: { severity: warning }
        annotations:
          summary: "Дома сухо: {{ $value }} % ({{ $labels.sensor }})"
          description: "Порог 25 %."

      # Таблетка CR2032 теряет проценты неделями. Тут не нужен `for` — если
      # значение упало ниже порога, это не флап, а факт на ближайший месяц.
      - alert: HomeSensorBatteryLow
        expr: forest_home_battery_percent < 20
        for: 1h
        labels: { severity: warning }
        annotations:
          summary: "Батарейка датчика {{ $labels.sensor }}: {{ $value }} %"
          description: "CR2032. Меняй, пока датчик не замолчал молча."

      # Датчик замолчал. Два интервала отчёта — это уже не «спит», это «пропал».
      # absent() ловит случай, когда серия исчезла совсем (плата перезагрузилась
      # без датчика, координатор упал), а age — когда серия есть, но мёртвая.
      - alert: HomeSensorSilent
        expr: forest_home_reading_age_seconds > 7200
        for: 15m
        labels: { severity: warning }
        annotations:
          summary: "Датчик {{ $labels.sensor }} молчит больше двух часов"
          description: "Проверь батарейку и привязку: Меню → Система → Подключить датчик."

      # ---- погода по барометру ----
      # Пороги — полосы ВМО, приведённые к 3 часам. Не меняй числа, не поменяв
      # окно: они заявлены именно для этого интервала.
      #
      # `for` здесь длиннее, чем у комнатных правил, и намеренно: тенденция
      # пересчитывается раз в 5 минут по часовым отсчётам, поэтому одиночный
      # выброс невозможен физически — а вот стрелка, зависшая на пороге, вполне
      # возможна. Полчаса удержания отсекают её.
      - alert: WeatherStormComing
        expr: forest_home_pressure_trend_3h_hpa <= -3.6
        for: 30m
        labels: { severity: warning }
        annotations:
          summary: "Давление резко падает: {{ $value | printf \"%.1f\" }} гПа/3ч"
          description: >-
            Идёт непогода: ветер, осадки. На таких перепадах метеочувствительные
            люди жалуются на головную боль — это констатация связи с давлением,
            а не диагноз.

      - alert: WeatherRainLikely
        expr: forest_home_pressure_trend_3h_hpa <= -1.6 and forest_home_pressure_trend_3h_hpa > -3.6
        for: 45m
        labels: { severity: warning }
        annotations:
          summary: "Давление падает: {{ $value | printf \"%.1f\" }} гПа/3ч"
          description: "Приближается циклон — вероятен дождь. Зонт не помешает."

      - alert: WeatherClearingAndColder
        expr: forest_home_pressure_trend_3h_hpa >= 3.6
        for: 45m
        labels: { severity: warning }
        annotations:
          summary: "Давление резко растёт: {{ $value | printf \"%.1f\" }} гПа/3ч"
          description: >-
            Строится антициклон: небо расчистится. Ясной ночью тепло уходит
            излучением, так что жди похолодания — зимой заметного.

      - alert: HomeSensorGone
        expr: absent(forest_home_reading_age_seconds)
        for: 30m
        labels: { severity: warning }
        annotations:
          summary: "Метрик климата нет вовсе"
          description: "Либо NocturneServer не пишет forest_home.prom, либо ПК выключен."
```

**Роутинг уже есть — проверено.** В `alertmanager/alertmanager.yml` маршрут
`severity = warning` ведёт на receiver `tgbot`
(`http://monitoring-tgbot:8080/alert`). Все правила выше помечены `warning`,
поэтому доедут сами. **В `alertmanager/` менять ничего не надо.**

Почему не `critical`: критический уровень идёт ещё и в `telegram_direct`
и подавляет warning-и через `inhibit_rules`. Сырость в комнате — это не
разбудить ночью, это сказать утром.

### B2. Перечитать правила без рестарта

```bash
ssh rudywolf@forestserver.ru
docker compose -f ~/monitoring/docker-compose.yml exec prometheus \
  wget -qO- --post-data='' http://127.0.0.1:9090/-/reload
```

---

### B3. Тенденция на странице и в боте

В `stats.sh` добавь к остальным:

```sh
  HM_TREND=$(qp 'forest_home_pressure_trend_3h_hpa')
```

и в JSON — поле `"trend_3h":"${HM_TREND:-}"` внутри секции `home`.

В `index.html` — ещё одна карточка в ряд «дом»:

```html
    <div class="card" id="hmTrendCard"><div class="label">барометр 3ч</div><div class="value"><span id="hTrend">—</span><small id="hTrendHint"></small></div></div>
```

и в JS:

```js
    // Стрелка + следствие, а не голое число: «−4.2» никому ничего не говорит,
    // «резко падает» говорит всё.
    const tr = parseFloat(h.trend_3h);
    const hint = !Number.isFinite(tr) ? ""
      : tr <= -3.6 ? "↓↓ к непогоде"
      : tr <= -1.6 ? "↓ к дождю"
      : tr <= -0.5 ? "↓ пасмурнее"
      : tr <   0.5 ? "= без перемен"
      : tr <   1.6 ? "↑ проясняется"
      : tr <   3.6 ? "↑ к ясной"
      :              "↑↑ похолодает";
    $("hTrend").textContent = Number.isFinite(tr) ? tr.toFixed(1) : "—";
    $("hTrendHint").textContent = hint;
    $("hmTrendCard").classList.toggle("bad",  Number.isFinite(tr) && tr <= -3.6);
    $("hmTrendCard").classList.toggle("good", Number.isFinite(tr) && tr >= 1.6);
```

В боте, в `_home_text()`, добавь строку после давления — с тем же переводом
числа в следствие.

## Задача C — кнопка «🏠 Дом» в боте

Файл: `~/Projects/monitoring/tgbot/app.py`

### C1. Кнопка в `MAIN_KEYBOARD` (около строки 150)

Добавь `KeyboardButton('🏠 Дом')` в ряд к `📊 Status` — это такой же
«посмотреть состояние», а не действие.

### C2. Обработчик

Диспетчер `on_text` матчит по нормализованной подписи без эмодзи, так что:

```python
    elif key in ('дом', 'home'):
        await update.message.reply_text(await _home_text(), parse_mode=ParseMode.HTML)
```

### C3. Сам текст — по образцу `_status_text()`

Читай тот же `{HOME_URL}/stats.json`, секцию `home`:

```python
async def _home_text() -> str:
    """Климат из ForestHome.

    Возраст показания идёт первой строкой, а не сноской: датчик спит и
    отчитывается раз в ~50 минут, поэтому «23 °C» без даты — это утверждение,
    за которое никто не отвечает.
    """
    try:
        async with aiohttp.ClientSession() as s:
            async with s.get(f'{HOME_URL}/stats.json', timeout=5) as r:
                j = await r.json()
        h = j.get('home', {})
        if not h or h.get('temp') in (None, ''):
            return '🏠 <b>Дом</b>\n\nДатчик молчит — метрик климата нет.'

        def f(x, nd=0, default='?'):
            try:
                return f'{float(x):.{nd}f}'
            except (TypeError, ValueError):
                return default

        try:
            age = int(float(h.get('age_s', -1)))
        except (TypeError, ValueError):
            age = -1
        if age < 0:
            age_s = 'давность неизвестна'
        elif age < 90:
            age_s = 'только что'
        elif age < 3600:
            age_s = f'{age // 60} мин назад'
        else:
            age_s = f'{age // 3600} ч назад'
        stale = age < 0 or age > 3600

        hum = f(h.get('hum'))
        try:
            hv = float(h.get('hum'))
            verdict = 'сухо' if hv < 30 else 'сыро' if hv > 60 else 'норма'
        except (TypeError, ValueError):
            verdict = '?'
        press = h.get('press')
        try:
            mmhg = f'{float(press) * 0.750062:.0f} мм рт.ст.'
        except (TypeError, ValueError):
            mmhg = '—'

        head = '🏠 <b>Дом</b> · ForestHome'
        if stale:
            head += '  ⚠️'
        return (
            f'{head}\n'
            f'<i>{age_s}</i>\n\n'
            f'🌡 <b>{f(h.get("temp"), 1)} °C</b>\n'
            f'💧 <b>{hum} %</b> — {verdict}\n'
            f'🔻 {mmhg}\n'
            f'🔋 батарея {f(h.get("batt"))} %'
        )
    except Exception as e:
        return f'🏠 <b>Дом</b>\n\nНе смог прочитать stats.json: {e}'
```

Почему возраст в шапке, а не в конце: у батарейного датчика показание
законно бывает часовым, и человек, читающий сообщение в чате, не увидит
контекста экрана. Дата — часть значения, а не примечание к нему.

---

## Проверка

1. **Метрика доехала:**
   ```bash
   curl -s --data-urlencode 'query=forest_home_temp_celsius' \
     http://forestserver.ru:9090/api/v1/query | jq .
   ```
   Если пусто — смотри, жив ли `NocturnePromExport` на ПК и лежит ли
   `C:\ProgramData\grafana-agent\textfile\forest_home.prom`.

2. **Страница:** `https://forestserver.ru/stats.json` содержит ключ `home`,
   на самой странице появилась секция «дом».

3. **Правила загрузились:** `https://forestserver.ru/monitoring` → Prometheus
   → Alerts → группа `home-climate` видна, состояние `inactive`.

4. **Алерт реально срабатывает.** Не жди погоды — временно опусти порог:
   поставь `HomeTooHot` на `> 20`, перечитай правила, дождись `for: 10m`,
   убедись, что сообщение пришло в Telegram, верни `> 27`.

5. **Кнопка:** `/menu` в боте → «🏠 Дом» → приходит карточка с возрастом
   в шапке.

## Деплой

```bash
ssh rudywolf@forestserver.ru
cd ~/forestserver-home && git pull && docker compose up -d --build
cd ~/monitoring       && git pull && docker compose up -d --build tgbot
docker compose -f ~/monitoring/docker-compose.yml exec prometheus \
  wget -qO- --post-data='' http://127.0.0.1:9090/-/reload
```

## Чего делать НЕ надо

* Не добавляй опрос датчика чаще. WSDCGQ11LM решает сам, и его каденция —
  то, что даёт CR2032 два года жизни.
* Не строй погодных выводов на комнатной температуре и влажности. Они про
  комнату, а не про улицу; единственный уличный сигнал здесь — давление.
* Не переписывай пороги тенденции «под себя», не поменяв окно: они заявлены
  для трёх часов и вне этого интервала не значат ничего.
* Не превращай заметку про головную боль в медицинское утверждение. Правило
  говорит, что сделало давление, и кто обычно это замечает. Дальше — не наше
  дело.
* Не убирай guard по возрасту из правил ради «читаемости».
* Не прячь секцию «дом», когда данных нет, и не подставляй 0 вместо пустоты.
* Не трогай сторону ПК: `forest_home.prom` уже пишется и работает.
