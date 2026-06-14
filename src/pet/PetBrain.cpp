#include "pet/PetBrain.h"

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
}

void PetBrain::diary(const char *ev) {
  if (!sd_ || !sd_->ok()) return;
  String line = String("{\"age\":") + pet_->ageDays() + ",\"up\":" +
                (millis() / 60000UL) + ",\"ev\":\"" + ev + "\"}";
  sd_->enqueueAppend("/wolf/memory.jsonl", line);
}

String PetBrain::buildContext(const char *eventRu, AppState &st) {
  /* Natural prose grounds the small model far better than "состояние=N". */
  String c;
  c.reserve(420);
  c += "Сейчас происходит вот что: ";
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
    c += "Связи с компьютером хозяина сейчас нет. ";
  }
  if (st.weatherReceived) {
    c += "За окном ";
    c += st.weather.temp;
    c += " градусов. ";
  }
  /* diary tail → continuity ("ты кормил меня недавно, опять?") */
  if (sd_ && sd_->ok()) {
    String mem;
    if (sd_->readLastLines("/wolf/memory.jsonl", 3, mem) && mem.length()) {
      mem.replace("\n", " ");
      c += "Недавно в твоём дневнике: ";
      c += mem;
      c += ". ";
    }
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

/* PC counts as idle when the owner has clearly stepped away AND the machine
 * is quiet — only then may the wolf spend GPU time thinking. Strict on
 * purpose (user: "LLM не должна юзаться если я пользуюсь пк"): explicit TALK
 * still forces a reply regardless of this gate. */
bool PetBrain::pcIdle(const AppState &st) const {
  if (st.forzaLive) return false;
  if (!st.link.tcpConnected || st.link.signalLost) return false;
  if (st.alertActive) return false;
  bool away = st.pcIdleSec >= 0 && st.pcIdleSec >= 90; /* ≥1.5 min no input */
  bool quiet = st.hw.gl < 18 && st.hw.cl < 30;          /* GPU/CPU near-free */
  return away && quiet;
}

void PetBrain::show(const String &p, unsigned long now) {
  phrase_ = p;
  speechStart_ = now;
  /* hold long enough to read: 8 s + 60 ms/char */
  speechHold_ = 8000UL + (unsigned long)p.length() * 60UL;
  thinking_ = false;
  lastSpeech_ = now;
}

void PetBrain::trigger(const char *bucket, const char *eventRu,
                       unsigned long now, AppState &st, bool urgent,
                       bool forceLlm) {
  if (!urgent && now - lastSpeech_ < NOCT_LLM_COOLDOWN_MS) return;
  if (thinking_) return; /* one request in flight, latest-wins not needed */

  /* The LLM runs on the owner's GPU. The wolf comments on what the owner is
   * doing (browsing, coding, server events…) using the LLM while the load is
   * LIGHT, and falls back to instant cached phrases during games / heavy
   * renders so it never steals frames. Explicit TALK / remote say forces it. */
  bool affordable = !st.forzaLive && st.hw.gl < 55 && st.hw.cl < 80;
  bool allowLlm = forceLlm || affordable;
  bool canLlm = st.settings.petLlm && llm_ && !llm_->busy() &&
                st.link.wifiConnected && allowLlm;
  if (canLlm && llm_->request(buildContext(eventRu, st), /*big=*/false)) {
    strncpy(pendingBucket_, bucket, sizeof(pendingBucket_) - 1);
    pendingBucket_[sizeof(pendingBucket_) - 1] = '\0';
    thinking_ = true;
    speechStart_ = now;
    lastSpeech_ = now;
    st.link.llmBusy = true;
  } else {
    show(cache_->pick(bucket), now);
  }
}

void PetBrain::onAction(int action) {
  unsigned long now = millis();
  reactionKind_ = action; /* drives the DEN particle burst */
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

  /* 1) pump finished LLM replies */
  if (thinking_ && llm_) {
    String reply;
    bool ok = false;
    if (llm_->takeReply(reply, ok)) {
      if (ok) {
        cache_->remember(pendingBucket_, reply);
        show(reply, now);
      } else {
        show(cache_->pick(pendingBucket_), now);
      }
    } else if (now - speechStart_ > NOCT_LLM_TIMEOUT_MS + 4000UL) {
      thinking_ = false; /* belt & braces: task wedged — fall back */
      show(cache_->pick(pendingBucket_), now);
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
      /* explicit TALK: the owner asked the AI directly — force the LLM */
      trigger("talk", "хозяин просит тебя что-нибудь сказать", now, st, true,
              true);
    }
  } else {
    actionPending_ = -1;
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
    bool sys = app == "System Idle Process" || app == "Idle" ||
               app.startsWith("System") || app == "pythonw3.13.exe" ||
               app == "dwm.exe" || app == "Registry";
    if (!sys && app != lastApp_ && now - lastAppAt_ > 20000) {
      String ev = "хозяин запустил программу " + app + ", прокомментируй";
      /* games get a livelier angle */
      String low = app;
      low.toLowerCase();
      if (low.indexOf("forza") >= 0 || low.indexOf("game") >= 0 ||
          low.indexOf("steam") >= 0)
        ev = "хозяин запустил игру " + app + " — пожелай удачной катки";
      if (lastApp_.length()) trigger("app", ev.c_str(), now, st, false);
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
      trigger("idle", angle.c_str(), now, st, false);
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
