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
  String c;
  c.reserve(420);
  c += "событие: ";
  c += eventRu;
  c += "\nтвоё состояние: настроение=";
  c += moodRu(pet_->mood());
  c += pet_->isSleeping() ? " (спал)" : "";
  c += " сытость=";
  c += pet_->hunger();
  c += " радость=";
  c += pet_->happy();
  c += " энергия=";
  c += pet_->energy();
  c += " возраст=";
  c += (int)pet_->ageDays();
  c += "д";
  if (st.link.tcpConnected && !st.link.signalLost) {
    c += "\nпк хозяина: cpu=";
    c += st.hw.ct;
    c += "C/";
    c += st.hw.cl;
    c += "% gpu=";
    c += st.hw.gt;
    c += "C/";
    c += st.hw.gl;
    c += "%";
    if (st.alertActive) c += " ТРЕВОГА-ПЕРЕГРЕВ";
    if (st.media.isPlaying && st.media.track.length()) {
      c += " играет:";
      c += st.media.artist;
      c += "-";
      c += st.media.track;
    }
    if (st.events.count > 0) {
      c += " событие-сервера:";
      c += st.events.top;
    }
  } else {
    c += "\nпк хозяина: не на связи";
  }
  if (st.weatherReceived) {
    c += "\nпогода: ";
    c += st.weather.desc;
    c += " ";
    c += st.weather.temp;
    c += "C";
  }
  /* diary tail → continuity ("ты кормил меня недавно, опять?") */
  if (sd_ && sd_->ok()) {
    String mem;
    if (sd_->readLastLines("/wolf/memory.jsonl", 4, mem) && mem.length()) {
      mem.replace("\n", " ");
      c += "\nтвой дневник (последнее): ";
      c += mem;
    }
  }
  return c;
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
                       unsigned long now, AppState &st, bool urgent) {
  if (!urgent && now - lastSpeech_ < NOCT_LLM_COOLDOWN_MS) return;
  if (thinking_) return; /* one request in flight, latest-wins not needed */

  /* The LLM runs on the owner's GPU: never steal frames from a game or a
   * heavy job — fall back to cached phrases while the PC is busy. */
  bool pcBusy = st.forzaLive || st.hw.gl >= 40 || st.hw.cl >= 85;
  bool canLlm = st.settings.petLlm && llm_ && !llm_->busy() &&
                st.link.wifiConnected && !pcBusy;
  if (canLlm && llm_->request(buildContext(eventRu, st))) {
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
  switch (action) {
  case WolfPet::ACT_FEED:
    diary("кормил");
    break;
  case WolfPet::ACT_PLAY:
    diary("играл");
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
    } else {
      trigger("talk", "хозяин просит тебя что-нибудь сказать", now, st, true);
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
  if (st.alertActive && !lastAlert_)
    trigger("alert", "у хозяина перегрев железа, кричи тревогу", now, st,
            true);
  lastAlert_ = st.alertActive;

  if (st.events.top[0] && strncmp(st.events.top, lastEventTop_, 20) != 0) {
    if (seenFirstPayload_)
      trigger("event", "на серверах хозяина новое предупреждение", now, st,
              false);
    strncpy(lastEventTop_, st.events.top, 20);
    lastEventTop_[20] = '\0';
  }

  if (st.media.track.length() && st.media.isPlaying &&
      st.media.track != lastTrack_) {
    if (seenFirstPayload_ && lastTrack_.length())
      trigger("media", "хозяин включил новый трек, оцени его", now, st,
              false);
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

  /* Forza session start */
  if (st.forzaLive && !lastForzaLive_)
    trigger("race", "хозяин завёл гонки в Forza, поболей за него", now, st,
            false);
  lastForzaLive_ = st.forzaLive;

  if (st.link.tcpConnected && !st.link.signalLost) seenFirstPayload_ = true;

  /* 5) idle observations — random angle each time */
  if ((long)(now - nextIdleChatter_) >= 0) {
    nextIdleChatter_ = now + NOCT_LLM_IDLE_CHATTER_MIN_MS +
                       random(NOCT_LLM_IDLE_CHATTER_RND_MS);
    if (pet_->isAlive() && !pet_->isSleeping()) {
      static const char *angles[] = {
          "просто скажи что-нибудь от скуки",
          "прокомментируй, что сейчас творится на пк хозяина",
          "оцени температуру и нагрузку железа по-своему",
          "расскажи короткую волчью мудрость про технологии",
          "пошути про то, что видишь в телеметрии",
      };
      trigger("idle", angles[random(5)], now, st, false);
    }
  }
}

bool PetBrain::bubbleVisible(unsigned long now) const {
  if (thinking_) return true;
  return phrase_.length() > 0 && (now - speechStart_) < speechHold_;
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
