#include "pet/PetBrain.h"

#include <time.h>

#include "core/config.h"

static const char *moodRu(int m) {
  return m == 0 ? "плохо" : (m == 1 ? "норм" : "отлично");
}

void PetBrain::begin(WolfPet *pet, LlmClient *llm, PhraseCache *cache,
                     SdStore *sd) {
  pet_ = pet;
  llm_ = llm;
  cache_ = cache;
  sd_ = sd;
  unsigned long now = millis();
  nextIdleChatter_ =
      now + NOCT_LLM_IDLE_CHATTER_MIN_MS + random(NOCT_LLM_IDLE_CHATTER_RND_MS);
  lastSleeping_ = pet->isSleeping();
  lastAlive_ = pet->isAlive();
  /* The wolf's long memory: last night's entry, kept alongside the dated
   * archive so it can be reloaded in one read after any reboot. */
  if (sd_ && sd_->ok()) sd_->readAll("/wolf/journal/last.txt", lastJournal_, 512);
}

void PetBrain::writeJournal(const String &dayDate, const String &summary) {
  if (journalPending_ || !llm_ || llm_->busy()) return;
  if (!sd_ || !sd_->ok()) return;
  String prompt = "Заверши день. Вот сводка по компьютеру хозяина, " + summary +
                  ". ";
  String mem;
  if (sd_->readLastLines("/wolf/memory.jsonl", 12, mem) && mem.length()) {
    mem.replace("\r", "");
    mem.replace("\n", "; ");
    prompt += "Что было в твоём дневнике: " + mem + ". ";
  }
  prompt += "Напиши 2-3 предложения от первого лица — что за день запомнилось "
            "и что ты думаешь о хозяине. Это запись в дневник, не реплика.";
  if (llm_->request(prompt, /*big=*/true, kTagJournal)) {
    journalPending_ = true;
    journalDate_ = dayDate;
    Serial.printf("[WOLF] journal for %s requested\n", dayDate.c_str());
  }
}

void PetBrain::diary(const char *ev) {
  if (!sd_ || !sd_->ok()) return;
  /* Plain dated text, not JSON. The only consumer is an LLM prompt, and a
   * model reads "2026-08-21 19:30 кормил" far better than a struct — it can
   * say "утром" or "вчера" off the back of it. The old {"age":..,"up":..}
   * form put raw braces in the context and told the wolf nothing it could
   * actually use. Falls back to uptime when the clock has not synced yet. */
  char stamp[24];
  time_t t = time(nullptr);
  struct tm tmv;
  if (t >= 1700000000L && localtime_r(&t, &tmv))
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", &tmv);
  else
    snprintf(stamp, sizeof(stamp), "+%luм", millis() / 60000UL);
  String line = String(stamp) + " " + ev;
  /* capped: the diary is append-only and only its tail is ever read back into
   * the prompt, so let it rotate instead of growing on the card forever */
  sd_->enqueueAppend("/wolf/memory.jsonl", line, NOCT_SD_DIARY_MAX);
}

String PetBrain::buildContext(const char *eventRu, AppState &st) {
  /* Natural prose grounds the small model far better than "состояние=N". */
  String c;
  c.reserve(420);
  c += "Ты сейчас ";
  c += pet_->stageName();
  c += ", тебе ";
  c += (unsigned long)pet_->ageDays();
  c += " дней. Сейчас происходит вот что: ";
  c += eventRu;
  c += ". Ты чувствуешь себя ";
  c += moodRu(pet_->mood());
  if (pet_->isSleeping()) c += ", тебя только что разбудили";
  else if (pet_->hunger() < 30) c += " и немного голоден";
  else if (pet_->energy() < 30) c += " и устал";
  c += ". ";

  if (st.link.tcpConnected && !st.link.signalLost) {
    if (st.alertActive) {
      c += "У хозяина перегревается железо — это тревожно. ";
    } else if (st.pcIdleSec >= 60) {
      c += "Хозяина нет за компьютером, всё тихо. ";
    } else if (st.media.isPlaying && st.media.track.length()) {
      c += "У хозяина играет музыка: ";
      c += st.media.artist;
      c += " — ";
      c += st.media.track;
      c += ". ";
    } else {
      c += "Компьютер хозяина работает спокойно, нагрузки умеренные. ";
    }
    if (st.events.count > 0 && st.events.top[0]) {
      c += "На серверах хозяина есть предупреждение: ";
      c += st.events.top;
      c += ". ";
    }
  } else {
    /* PC off: the board still pulls weather + forest/services + alerts from the
     * fallback endpoint, so ground the wolf in THAT instead of going blank. */
    c += "Связи с компьютером хозяина сейчас нет — ты приглядываешь сам. ";
    if (st.events.count > 0 && st.events.top[0]) {
      c += "На серверах хозяина тревога: ";
      c += st.events.top;
      c += ". ";
    } else if (st.forest.count > 0) {
      c += "Лес хозяина под присмотром: в строю ";
      c += st.forest.up;
      c += " из ";
      c += st.forest.count;
      c += ". ";
    }
  }
  if (st.weatherReceived) {
    c += "За окном ";
    c += st.weather.temp;
    c += " градусов. ";
  }
  /* diary tail → continuity ("ты кормил меня недавно, опять?"). Now that the
   * entries are dated the model can place them in time, so it is worth giving
   * it a few more of them, separated so they read as a list and not one run-on
   * sentence. */
  if (sd_ && sd_->ok()) {
    String mem;
    if (sd_->readLastLines("/wolf/memory.jsonl", 6, mem) && mem.length()) {
      mem.replace("\r", "");
      mem.replace("\n", "; ");
      mem.trim();
      while (mem.endsWith(";")) mem.remove(mem.length() - 1);
      c += "Твой дневник (что было и когда): ";
      c += mem;
      c += ". Сегодня ";
      c += st.pcClock;
      c += ". ";
    }
  }
  if (lastJournal_.length()) {
    String j = lastJournal_;
    j.replace("\r", " ");
    j.replace("\n", " ");
    j.trim();
    c += "Из твоей последней записи в дневнике: ";
    c += j;
    c += " ";
  }
  /* tone follows the "Характер" setting */
  switch (st.settings.wolfTone) {
  case 1: c += "Будь тёплым, ласковым и заботливым. "; break;
  case 2: c += "Будь ворчливым, язвительным и саркастичным. "; break;
  case 3: c += "Будь дерзким, нахальным и самоуверенным. "; break;
  default: break;
  }
  c += "Скажи свою короткую реплику.";
  return c;
}

int PetBrain::clockHour() {
  time_t t = time(nullptr);
  struct tm tmv;
  if (t >= 1700000000L && localtime_r(&t, &tmv)) return tmv.tm_hour;
  return -1;
}

static bool sysProcess(const String &app) {
  return app == "System Idle Process" || app == "Idle" ||
         app.startsWith("System") || app.startsWith("pythonw") ||
         app == "dwm.exe" || app == "Registry"; /* pythonw = monitor.py */
}

PhraseCtx PetBrain::makeCtx(AppState &st) {
  PhraseCtx c;
  c.ageDays = (int)pet_->ageDays();
  c.stage = pet_->stage();
  c.mood = pet_->mood();
  c.hour = clockHour();
  c.scene = viewSceneName_ ? viewSceneName_ : "";
  bool pc = st.link.tcpConnected && !st.link.signalLost;
  if (pc) {
    c.gpu = st.hw.gl;
    c.cpu = st.hw.cl;
    c.gt = st.hw.gt;
    c.ct = st.hw.ct;
    if (st.media.isPlaying && st.media.track.length()) {
      c.track = st.media.track.c_str();
      c.artist = st.media.artist.c_str();
    }
    const String &app = st.process.cpuNames[0];
    if (app.length() && !sysProcess(app)) {
      /* "Code.exe" reads as a file name, "Code" as a program */
      strncpy(appBuf_, app.c_str(), sizeof(appBuf_) - 1);
      appBuf_[sizeof(appBuf_) - 1] = 0;
      size_t n = strlen(appBuf_);
      if (n > 4 && !strcasecmp(appBuf_ + n - 4, ".exe")) appBuf_[n - 4] = 0;
      c.app = appBuf_;
    }
    if (st.claude.weeklyPct >= 0) c.claudeWk = st.claude.weeklyPct;
  }
  if (st.weatherReceived) c.temp = st.weather.temp;
  if (st.zb.count > 0) {
    c.roomT10 = st.zb.list[0].temp10;
    c.roomRh = st.zb.list[0].humidity;
  }
  return c;
}

String PetBrain::idleBucket(AppState &st, const PhraseCtx &c) {
  /* Ordered by how much the remark would reveal that the wolf is actually
   * looking: the screen the owner opened beats the weather. A coin flip at
   * each step keeps one loud fact (a track on repeat all evening) from owning
   * every idle line. */
  if (c.scene[0] && random(2) == 0) return String("idle.scene.") + c.scene;
  if (c.track[0] && random(2) == 0) return "idle.media";
  if (c.gpu >= 50) return "idle.gpu";
  if (c.app[0] && random(2) == 0) return "idle.app";
  if (c.roomT10 != -32768 && random(2) == 0) {
    if (c.roomT10 >= 270) return "idle.room.warm";
    if (c.roomT10 <= 180) return "idle.room.cold";
    if (c.roomRh >= 65) return "idle.room.humid";
    if (c.roomRh >= 0 && c.roomRh <= 30) return "idle.room.dry";
  }
  if (c.temp != -999 && random(2) == 0) {
    int w = st.weather.wmoCode;
    if ((w >= 71 && w <= 77) || w == 85 || w == 86) return "idle.weather.snow";
    if ((w >= 51 && w <= 67) || (w >= 80 && w <= 82)) return "idle.weather.rain";
    if (c.temp <= -5) return "idle.weather.cold";
    if (c.temp >= 28) return "idle.weather.hot";
    if (w <= 1) return "idle.weather.clear";
  }
  if (c.mood == 0 && random(2) == 0) return "idle.mood.sad";
  if (c.mood == 2 && random(3) == 0) return "idle.mood.happy";
  if (c.hour >= 0 && random(2) == 0) {
    if (c.hour < 6) return "idle.night";
    if (c.hour < 11) return "idle.morning";
    if (c.hour < 18) return "idle.day";
    return "idle.evening";
  }
  return "idle";
}

/* Tone is decided by the ROOT of the bucket path: "hunger.night" is as low
 * as "hunger", "idle.mood.sad" is still idle. */
static bool rootIs(const char *b, const char *root) {
  size_t n = strlen(root);
  return !strncmp(b, root, n) && (b[n] == 0 || b[n] == '.');
}

int PetBrain::toneForBucket(const char *b) {
  if (!b) return TONE_NEUTRAL;
  if (rootIs(b, "alert") || rootIs(b, "faint") || rootIs(b, "event"))
    return TONE_TENSE;
  if (rootIs(b, "fed") || rootIs(b, "played") || rootIs(b, "pet") ||
      rootIs(b, "wake") || rootIs(b, "revive"))
    return TONE_HAPPY;
  if (rootIs(b, "hunger") || rootIs(b, "bored") || rootIs(b, "sleepy"))
    return TONE_LOW;
  return TONE_NEUTRAL;
}

void PetBrain::show(const String &p, unsigned long now, int tone) {
  phrase_ = p;
  speechStart_ = now;
  /* hold long enough to read: 8 s + 60 ms/char */
  speechHold_ = 8000UL + (unsigned long)p.length() * 60UL;
  thinking_ = false;
  lastSpeech_ = now;
  speechTone_ = tone;
}

void PetBrain::trigger(const char *bucket, const char *eventRu,
                       unsigned long now, AppState &st, bool urgent,
                       bool forceLlm, bool solicited) {
  /* "Болтливость: выкл" (wolfChatter==0) is a true mute: the wolf says nothing
   * on its own — no ambient remark, no cached fallback, and no LLM/LM Studio
   * call from any telemetry/pet/idle edge. Only an explicit request (the owner
   * pressing "Говорить") is solicited and still speaks. */
  if (!solicited && st.settings.wolfChatter == 0) return;
  if (!urgent && now - lastSpeech_ < NOCT_LLM_COOLDOWN_MS) return;
  if (thinking_) return; /* one request in flight, latest-wins not needed */

  /* The LLM runs on the owner's GPU. The wolf comments on what the owner is
   * doing (browsing, coding, server events…) using the LLM while the load is
   * LIGHT, and falls back to instant cached phrases during games / heavy
   * renders so it never steals frames. Explicit TALK / remote say forces it. */
  bool affordable = !st.forzaLive && st.hw.gl < 55 && st.hw.cl < 80;
  bool allowLlm = forceLlm || affordable;
  bool canLlm = st.settings.petLlm && llm_ && !llm_->busy() &&
                st.link.wifiConnected && allowLlm && now >= llmSuppressUntil_;
  if (canLlm && llm_->request(buildContext(eventRu, st), /*big=*/false)) {
    strncpy(pendingBucket_, bucket, sizeof(pendingBucket_) - 1);
    pendingBucket_[sizeof(pendingBucket_) - 1] = '\0';
    thinking_ = true;
    speechStart_ = now;
    lastSpeech_ = now;
    st.link.llmBusy = true;
  } else {
    /* A plain bucket gets the hour appended ("hunger.night", "back.morning");
     * the cache walks back to "hunger" when the table has no such flavour. */
    PhraseCtx c = makeCtx(st);
    char b[40];
    const char *tod = c.hour < 0 ? "" : c.hour < 6 ? ".night"
                    : c.hour < 11 ? ".morning" : "";
    snprintf(b, sizeof(b), "%s%s", bucket, strchr(bucket, '.') ? "" : tod);
    show(cache_->pick(b, c), now, toneForBucket(bucket));
  }
}

void PetBrain::onAction(int action) {
  unsigned long now = millis();
  reactionKind_ = action; /* drives the DEN particle burst */
  actionEvent_ = action;  /* and the achievement counter */
  reactionAt_ = now;
  switch (action) {
  case WolfPet::ACT_FEED:
    diary("кормил");
    break;
  case WolfPet::ACT_PLAY:
    diary("играл");
    break;
  case WolfPet::ACT_PET:
    diary("гладил");
    break;
  case WolfPet::ACT_TALK:
    diary("поговорил");
    break;
  }
  /* speech itself fires from tick() via action flags below */
  actionPending_ = action;
  actionPendingAt_ = now;
}

void PetBrain::tick(unsigned long now, AppState &st) {
  st.link.llmBusy = llm_ && llm_->busy();
  st.link.llmOk = llm_ && llm_->lastOk();

  /* 1) pump finished LLM replies. The mailbox now serves two callers, so the
   * tag decides: speech goes on screen, a journal entry goes on the card. */
  if (llm_) {
    String reply;
    bool ok = false;
    int tag = kTagSpeech;
    if (llm_->takeReply(reply, ok, &tag)) {
      if (tag == kTagJournal) {
        journalPending_ = false;
        if (ok && reply.length() && sd_ && sd_->ok()) {
          String path = "/wolf/journal/" + journalDate_ + ".txt";
          sd_->enqueueAppend(path.c_str(), reply);
          sd_->writeBlob("/wolf/journal/last.txt", reply.c_str(),
                         reply.length());
          lastJournal_ = reply;
          journalWritten_ = true;
          Serial.printf("[WOLF] journal %s: %s\n", journalDate_.c_str(),
                        reply.c_str());
        }
      } else if (thinking_) {
        int tone = toneForBucket(pendingBucket_);
        if (ok) {
          llmFailStreak_ = 0; /* LLM is healthy again */
          cache_->remember(pendingBucket_, reply);
          show(reply, now, tone);
        } else {
          if (++llmFailStreak_ >= 2) llmSuppressUntil_ = now + kLlmSuppressMs;
          show(cache_->pick(pendingBucket_, makeCtx(st)), now, tone);
        }
      }
    } else if (thinking_ && now - speechStart_ > NOCT_LLM_TIMEOUT_MS + 4000UL) {
      if (++llmFailStreak_ >= 2) llmSuppressUntil_ = now + kLlmSuppressMs;
      thinking_ = false; /* belt & braces: task wedged — fall back */
      show(cache_->pick(pendingBucket_, makeCtx(st)), now,
           toneForBucket(pendingBucket_));
    }
  }

  /* 1.5) boot greeting: first moment we can speak (also a live e2e check) */
  if (bootGreetPending_ && now > 4000) {
    bool online = st.link.wifiConnected;
    if (online || now > 30000) { /* give WiFi 30 s, then greet offline */
      bootGreetPending_ = false;
      trigger("boot", "тебя только что включили на новом устройстве",
              now, st, true);
    }
  }

  /* 2) user actions (urgent, bypass cooldown) */
  if (actionPending_ >= 0 && now - actionPendingAt_ < 2000) {
    int a = actionPending_;
    actionPending_ = -1;
    if (!pet_->isAlive()) {
      trigger("revive", "хозяин привёл тебя в чувство", now, st, true);
    } else if (a == WolfPet::ACT_FEED) {
      trigger("fed", "хозяин только что покормил тебя", now, st, true);
    } else if (a == WolfPet::ACT_PLAY) {
      trigger("played", "хозяин только что поиграл с тобой", now, st, true);
    } else if (a == WolfPet::ACT_PET) {
      trigger("pet", "хозяин ласково тебя гладит, ответь по-волчьи", now, st,
              true);
    } else {
      /* explicit TALK: the owner asked the AI directly — force the LLM and
       * bypass the chatter-mute (this is the one way to make a muted wolf talk) */
      trigger("talk", "хозяин просит тебя что-нибудь сказать", now, st, true,
              true, /*solicited=*/true);
    }
  } else {
    actionPending_ = -1;
  }

  /* 2.5) something the board noticed on its own, from the SD archive */
  if (pendingNotice_.length() && !thinking_) {
    String ev = pendingNotice_;
    pendingNotice_ = "";
    trigger("notice", ev.c_str(), now, st, true);
  }

  /* 3) pet state edges */
  if (lastAlive_ && !pet_->isAlive())
    trigger("faint", "ты упал в обморок от голода и скуки", now, st, true);
  lastAlive_ = pet_->isAlive();

  if (lastSleeping_ && !pet_->isSleeping() && pet_->isAlive())
    trigger("wake", "ты только что проснулся выспавшимся", now, st, false);
  lastSleeping_ = pet_->isSleeping();

  if (pet_->hunger() < 25 && !lowHungerFired_) {
    lowHungerFired_ = true;
    trigger("hunger", "ты сильно проголодался, попроси еды", now, st, false);
  } else if (pet_->hunger() > 45) {
    lowHungerFired_ = false;
  }
  if (pet_->happy() < 25 && !lowHappyFired_) {
    lowHappyFired_ = true;
    trigger("bored", "тебе очень скучно, попроси поиграть", now, st, false);
  } else if (pet_->happy() > 45) {
    lowHappyFired_ = false;
  }
  if (pet_->energy() < 25 && !pet_->isSleeping() && !lowEnergyFired_) {
    lowEnergyFired_ = true;
    trigger("sleepy", "ты очень устал и хочешь спать", now, st, false);
  } else if (pet_->energy() > 45) {
    lowEnergyFired_ = false;
  }

  /* 3.5) the wolf knows what time it is. It has had an NTP clock since 1.7.7
   * and never once used it to say anything — which is odd for something that
   * sits on your desk watching you work at three in the morning. */
  {
    time_t tnow = time(nullptr);
    struct tm tmv;
    if (tnow >= 1700000000L && localtime_r(&tnow, &tmv)) {
      int h = tmv.tm_hour;
      if (h != lastHour_) {
        lastHour_ = h;
        bool ownerHere = st.pcIdleSec >= 0 && st.pcIdleSec < 600;
        if (h >= 6 && h < 11) nightScolded_ = false;
        if (h >= 12) morningGreeted_ = false;
        if (h >= 1 && h < 5 && ownerHere && !nightScolded_) {
          nightScolded_ = true;
          char ev[160]; /* Cyrillic is 2 B/char - this line is 125 B */
          snprintf(ev, sizeof(ev),
                   "сейчас %d часов ночи, а хозяин всё ещё за компьютером - "
                   "поворчи и отправь спать", h);
          trigger("latenight", ev, now, st, false);
        } else if (h >= 6 && h < 11 && ownerHere && !morningGreeted_) {
          morningGreeted_ = true;
          trigger("morning", "хозяин появился утром - поздоровайся с ним",
                  now, st, false);
        }
      }
    }
  }

  /* 4) telemetry edges */
  if (st.alertActive && !lastAlert_) {
    /* name the hottest component so the wolf is specific, not generic */
    String which = "железо греется";
    if (st.hw.gt >= 80)
      which = "видеокарта раскалилась до " + String(st.hw.gt) + " градусов";
    else if (st.hw.ct >= 85)
      which = "процессор раскалился до " + String(st.hw.ct) + " градусов";
    else if (st.hw.gl >= 95)
      which = "видеокарта пашет на " + String(st.hw.gl) + " процентов";
    else if (st.hw.cl >= 95)
      which = "процессор загружен на " + String(st.hw.cl) + " процентов";
    String ev = "тревога у хозяина: " + which + " — кричи";
    trigger("alert", ev.c_str(), now, st, true);
  }
  lastAlert_ = st.alertActive;

  /* server events: react ONCE per distinct alert (severity+text), never loop
   * on a persistent one (the old 20-char string compare re-fired every
   * cooldown — that was the "циклится на одних и тех же алертах" bug). */
  if (st.events.count > 0 && st.events.top[0]) {
    String sig = String(st.events.severity) + "|" + st.events.top;
    if (sig != lastAlertSig_) {
      if (seenFirstPayload_ && lastAlertSig_.length()) {
        String ev = "новое событие на сервере: " + String(st.events.top) +
                    " (" + st.events.severity + ") — отреагируй";
        trigger("event", ev.c_str(), now, st, false);
        diary(("событие " + String(st.events.top)).c_str());
      }
      lastAlertSig_ = sig;
    }
  } else {
    lastAlertSig_ = ""; /* cleared → a returning alert counts as new */
  }

  if (st.media.track.length() && st.media.isPlaying &&
      st.media.track != lastTrack_) {
    if (seenFirstPayload_ && lastTrack_.length()) {
      String ev = "хозяин включил трек: " + st.media.artist + " — " +
                  st.media.track + " — оцени";
      trigger("media", ev.c_str(), now, st, false);
    }
    lastTrack_ = st.media.track;
  }

  if (st.claude.windowPct >= 80 && !claudeFired_) {
    claudeFired_ = true;
    if (seenFirstPayload_)
      trigger("claude", "у хозяина почти кончился лимит Claude", now, st,
              false);
  } else if (st.claude.windowPct >= 0 && st.claude.windowPct < 70) {
    claudeFired_ = false;
  }

  if (st.weatherReceived && st.weather.wmoCode != lastWmo_) {
    if (lastWmo_ != -999)
      trigger("weather", "погода за окном сменилась", now, st, false);
    lastWmo_ = st.weather.wmoCode;
  }

  /* 4.5) PC activity — the wolf lives next to the machine and notices */
  if (seenFirstPayload_ && st.link.tcpConnected && !st.link.signalLost) {
    /* GPU pegged for a minute = gaming / heavy graphics */
    if (st.hw.gl >= 85) {
      if (!gpuHighSince_) gpuHighSince_ = now;
      if (!gamingFired_ && now - gpuHighSince_ > 60000UL) {
        gamingFired_ = true;
        trigger("gaming",
                "хозяин уже минуту гоняет видеокарту на полную — похоже, "
                "играет; прокомментируй",
                now, st, false);
        diary("долго играл");
      }
    } else if (st.hw.gl < 50) {
      gpuHighSince_ = 0;
      gamingFired_ = false;
    }

    /* CPU pegged while GPU idle = build/render */
    if (st.hw.cl >= 85 && st.hw.gl < 50) {
      if (!cpuHighSince_) cpuHighSince_ = now;
      if (!cpuWorkFired_ && now - cpuHighSince_ > 60000UL) {
        cpuWorkFired_ = true;
        trigger("cpuwork",
                "процессор хозяина уже минуту пашет на полную (сборка или "
                "рендер?), прокомментируй",
                now, st, false);
      }
    } else if (st.hw.cl < 50) {
      cpuHighSince_ = 0;
      cpuWorkFired_ = false;
    }

    /* owner left / came back (server pidle = seconds since last input) */
    if (st.pcIdleSec >= 0) {
      if (st.pcIdleSec >= 600 && !awayFired_) {
        awayFired_ = true;
        trigger("away", "хозяин куда-то ушёл от компьютера", now, st, false);
      }
      if (awayFired_ && lastPidle_ > 300 && st.pcIdleSec <= 10) {
        awayFired_ = false;
        trigger("back", "хозяин вернулся за компьютер, поприветствуй", now,
                st, false);
      }
      lastPidle_ = st.pcIdleSec;
    }
  }

  /* app launch — the top CPU process changed to a new real program. Skip
   * idle/system noise; debounce so a brief spike doesn't chatter. */
  if (seenFirstPayload_ && st.process.cpuNames[0].length()) {
    String app = st.process.cpuNames[0];
    bool sys = sysProcess(app);
    if (!sys && app != lastApp_ && now - lastAppAt_ > 20000) {
      String ev = "хозяин запустил программу " + app + ", прокомментируй";
      /* games get a livelier angle */
      String low = app;
      low.toLowerCase();
      if (low.indexOf("forza") >= 0 || low.indexOf("game") >= 0 ||
          low.indexOf("steam") >= 0)
        ev = "хозяин запустил игру " + app + " — пожелай удачной катки";
      if (lastApp_.length()) {
        trigger("app", ev.c_str(), now, st, false);
        diary(("запуск " + app).c_str()); /* remember the owner's habits */
      }
      lastApp_ = app;
      lastAppAt_ = now;
    } else if (app == lastApp_) {
      lastApp_ = app; /* keep latching */
    }
  }

  /* Forza session start */
  if (st.forzaLive && !lastForzaLive_)
    trigger("race", "хозяин завёл гонки в Forza, поболей за него", now, st,
            false);
  lastForzaLive_ = st.forzaLive;

  if (st.link.tcpConnected && !st.link.signalLost) seenFirstPayload_ = true;

  /* 5) idle observations — frequency follows the "Болтливость" setting
   * (0 off / 1 rare / 2 normal / 3 often) */
  if (st.settings.wolfChatter > 0 && (long)(now - nextIdleChatter_) >= 0) {
    unsigned long base, rnd;
    switch (st.settings.wolfChatter) {
    case 1: base = 40UL * 60000; rnd = 20UL * 60000; break; /* rare */
    case 3: base = 6UL * 60000;  rnd = 6UL * 60000;  break; /* often */
    default: base = 20UL * 60000; rnd = 20UL * 60000; break; /* normal */
    }
    nextIdleChatter_ = now + base + random(rnd);
    if (pet_->isAlive() && !pet_->isSleeping()) {
      /* ground the idle remark in whatever is actually happening right now, so
       * the wolf names a real track / app / metric instead of vague chitchat */
      String angle;
      if (viewSceneName_ && random(2) == 0)
        angle = String("хозяин засмотрелся на экран ") + viewSceneName_ +
                " — скажи что-нибудь по теме этого экрана";
      else if (st.media.isPlaying && st.media.track.length())
        angle = "под музыку " + st.media.track + " от " + st.media.artist +
                " — скажи пару слов";
      else if (st.events.count > 0 && st.events.top[0])
        angle = "глянь на событие сервера: " + String(st.events.top) +
                " — что думаешь";
      else if (st.hw.gl >= 50)
        angle = "видеокарта хозяина занята на " + String(st.hw.gl) +
                " процентов — прокомментируй чем";
      else if (st.process.cpuNames[0].length())
        angle = "хозяин сейчас сидит в " + st.process.cpuNames[0] +
                " — подколи или похвали";
      else if (st.weatherReceived)
        angle = "за окном " + String(st.weather.temp) +
                " градусов — свяжи с настроением";
      else {
        static const char *fb[] = {
            "скажи короткую волчью мудрость про технологии",
            "пошути, что хозяина давно не видно за компьютером",
        };
        angle = fb[random(2)];
      }
      /* The LLM gets the angle as prose; the cache gets it as a bucket path
       * and answers at whatever depth the table has lines for. */
      String b = idleBucket(st, makeCtx(st));
      trigger(b.c_str(), angle.c_str(), now, st, false);
    }
  }
}

bool PetBrain::bubbleVisible(unsigned long now) const {
  if (thinking_) return true;
  return phrase_.length() > 0 && (now - speechStart_) < speechHold_;
}

float PetBrain::speechEnvelope(unsigned long now) const {
  if (thinking_) return 1.0f;
  if (!phrase_.length()) return 0.0f;
  unsigned long e = now - speechStart_;
  if (e >= speechHold_) return 0.0f;
  const unsigned long T = 260; /* slide duration */
  if (e < T) return (float)e / T;
  if (speechHold_ > T && e > speechHold_ - T)
    return (float)(speechHold_ - e) / T;
  return 1.0f;
}

int PetBrain::revealChars(unsigned long now) const {
  if (thinking_) return 0;
  unsigned long dt = now - speechStart_;
  return (int)(dt / 30); /* ~33 chars/s typewriter */
}

bool PetBrain::talkingAnim(unsigned long now) const {
  return thinking_ ||
         (phrase_.length() > 0 && now - speechStart_ < 1500UL);
}
