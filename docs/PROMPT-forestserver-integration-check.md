# Промт: проверка интеграции forestserver ↔ Nocturne C6

Скопируй всё ниже в чат, который работает на стороне forestserver. Это
самодостаточное задание: контракт, что именно проверить, как отличить «работает»
от «молчит», и два известных открытых вопроса.

---

Ты работаешь на **forestserver** (Linux, Docker, стек `monitoring-*`:
Prometheus, Alertmanager, Grafana, blackbox, cadvisor, node-exporter, tgbot).
С другой стороны — Windows-ПК с `NocturneServer` (`monitor.py`) и плата
Nocturne C6 (ESP32-C6, экран 320×172), которая показывает то, что ей шлёт ПК.
ПК берёт данные с forestserver **только чтением**, по четырём каналам. Проверь
каждый канал сквозь всю цепочку и доложи по форме внизу. Ничего не выдумывай:
если чего-то нет — так и пиши, с точным ответом сервера.

## Контракт (что ПК читает и что ожидает увидеть)

### 1. `https://forestserver.ru/stats.json` — состояние узлов
Сводный JSON, который уже рисует дашборд. ПК качает его каждые 30 с и строит
из него экран **ЛЕС** и счётчик контейнеров. Ожидаемые блоки и поля:

| блок | обязательные поля | как читает ПК |
|---|---|---|
| `server` | `up, cpu, ram, disk` (проценты), `containers_up` | как есть |
| `pc` | `up, cpu, ram, disk` | как есть |
| `nb` | `up, cpu, ram, disk` | как есть; **пустая строка = «нет данных»** |
| `router` | `up, load1, cpu_count, mem_used_kb, mem_total_kb, wifi_clients, nwg1_clients` | `cpu = load1/cpu_count·100`, `ram = used/total·100`, диска нет |
| `vpn` | `awg_peers, awg_peers_active` | справочно |

Проверить: `curl -s https://forestserver.ru/stats.json | jq '.nb, .router'`.
Сейчас `nb` — все поля пустые строки. **Вопрос 1:** это потому что ноутбук
выключен, или потому что его экспортёр не настроен? Если второе — что нужно
поднять, чтобы `nb` наполнился.

### 2. Prometheus через прокси Grafana
`https://forestserver.ru/monitoring/api/datasources/proxy/uid/prometheus/api/v1/query`
ПК шлёт `GET ?query=<PromQL>` без авторизации. Используемые выражения:

```
probe_success{instance="https://forestserver.ru/"}
probe_success{instance="https://hati.su/"}
probe_success{instance="https://onetothree.ru/"}
probe_success{instance="https://forestserver.ru/versionbot/api/health"}
probe_success{instance="host.docker.internal:7373"}
probe_duration_seconds{...тех же instance...}
amnezia_wg_peers_active            # «на связи» при > 0  → экран СЕРВИСЫ, «VPN»
max(proton_interface_up)           # «на связи» при > 0  → «Proton VPN»
```
Проверить, что каждое возвращает ровно одну серию с числом. Если blackbox
переименовал `instance` или добавил новую цель, которую стоит показать на
плате, — перечисли актуальные `instance` из `probe_success`.

### 3. Реле квоты Claude
`GET https://rudywolf.ru/nocturne/payload` с заголовком
`Authorization: Bearer <claude_remote_token>` (токен в `config.json` ПК).
Ожидается JSON с блоком `claude`: `win` (% пятичасового окна), `wk` (% недели),
`rst`, `wrst` (минуты до сброса), `plan`. Это **единственный** источник этих
процентов: на ПК токен Claude Code не используется намеренно (см. память
проекта — второй обновляющий процесс ротирует refresh-токен и ломает первый).
Проверить: ответ 200 и `win`/`wk` совпадают с тем, что показывает
`monitoring-tgbot` (`claude_meter`).

### 4. Архив климата (ПК → forestserver)
`POST https://forestserver.ru/api/climate/upload`, заголовок
`X-Forest-Token: <climate_upload_token>`, тело — CSV
`timestamp,temp_c,humidity_pct,battery_pct,pressure_hpa`. Ответ вида
`{"ok":true,"rows":N,"received":N,"added":K,"rejected":0}`. Проверить, что
последняя строка на сервере не старше двух часов (датчик отчитывается
примерно раз в час) и `rejected` = 0.

### 5. Alertmanager → плата и → Telegram
ПК опрашивает `https://forestserver.ru/monitoring/alertmanager/api/v2/alerts`
и показывает активные алерты на экране **СОБЫТИЯ**. Telegram при этом
получает их напрямую от Alertmanager (integration `telegram`) — ПК в этом не
участвует.

**Вопрос 2 (главный):** за последние 7 дней
`alertmanager_notifications_failed_total{integration="telegram"}` вырос на 3,
и сработал алерт `AlertDeliveryFailing{integration="telegram",severity="critical"}`.
Владелец говорит, что алерты в Telegram перестали приходить. Выясни причину:

```
docker logs monitoring-alertmanager --since 168h 2>&1 | grep -i -E 'telegram|notify|level=error' | tail -50
docker exec monitoring-alertmanager amtool config routes show
# в alertmanager.yml: receivers[].telegram_configs — bot_token, chat_id, api_url
# проверить токен: curl -s "https://api.telegram.org/bot<token>/getMe"
# проверить, что chat_id ещё существует и бот в нём не заблокирован
```
Отдельно проверь, не идёт ли исходящий трафик к `api.telegram.org` через
Proton-шлюз: `proton_interface_up{gw="de"} = 0` уже неделю, `gw="usa"` жив.
Если маршрут к Telegram привязан к `de` — это и есть причина.

## Форма доклада

Для каждого из пяти каналов — одна строка: **работает / частично / нет**, и
доказательство (точный ответ или строка лога). Затем ответы на Вопрос 1 и
Вопрос 2 с конкретной причиной и что исправлено. Если что-то исправлял на
forestserver — приведи diff или команду. Не предлагай «перепроверить позже»:
либо факт, либо «не смог, потому что …».
