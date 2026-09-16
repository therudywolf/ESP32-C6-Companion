# Documentation

**English** · [Русский](README.ru.md)

| | what it covers |
|---|---|
| [SETTINGS-MAP](SETTINGS-MAP.md) | every setting: where it lives in the menu, its name on the wire and in NVS, its default |

The main documents are one level up: [README](../README.md) — building and how
the thing is put together, [SCHEMA](../SCHEMA.md) — the payload format and
every protocol field.

---

Ten briefs used to live here — tasks written for sessions that would arrive
without any context. They have done their job and are gone, and they are gone
**from the history as well**, not merely deleted in a later commit: they
described the owner's home infrastructure in detail — addresses, host names,
the monitoring layout — and this repository is public. A file that is only
deleted stays one click away in any fork.

One of them survives, and on purpose: [`BRIEF-5-motion-sensors`][b5] is about
the Aqara RTCGQ11LM and nothing else — what the hardware can and cannot say,
and why "no motion" is not among the things it can say. There is no address,
host name or token in it. It was written before the sensors were in hand, so
its "pairing" section is a plan rather than a report.

[b5]: https://github.com/therudywolf/ESP32-C6-Companion/blob/f0ad2f7/docs/BRIEF-5-motion-sensors.md
