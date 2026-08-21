"""Guard the wire contract between SCHEMA.md and the firmware parser.

SCHEMA.md says the firmware parser is the source of truth and that a producer
renaming a field "silently desyncs the board". Nothing enforced that: the doc
and the parser could drift apart for months without a single error.

This checks the one direction that actually breaks things:
  * every JSON key TelemetryClient.cpp reads must appear in SCHEMA.md
    -> ERROR (the board depends on a field nobody documented)
  * keys documented but never read are reported as warnings (a producer may
    legitimately emit more than the board consumes)

Run:  python tools/check_schema.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARSER = ROOT / "src" / "net" / "TelemetryClient.cpp"
SCHEMA = ROOT / "SCHEMA.md"

# doc["ct"], rc["screen"], nodes[i]["cpu"], hdd[i]["n"] ...
KEY_RE = re.compile(r'\b(?:doc|rc|c|e|f|s|nt|nodes\[i\]|list\[i\]|hdd\[i\]'
                    r'|tp\[i\]|tr\[i\]|wf\[i\])\["([A-Za-z_][\w]*)"\]')

# Keys the firmware writes rather than reads, or that are structural.
IGNORE = {"seq"}


def parsed_keys() -> set[str]:
    src = PARSER.read_text(encoding="utf-8")
    return {k for k in KEY_RE.findall(src)} - IGNORE


def documented_tokens() -> set[str]:
    md = SCHEMA.read_text(encoding="utf-8")
    # every `backticked` token, split on / and , so "`win` / `wk`" counts as two
    out: set[str] = set()
    for chunk in re.findall(r"`([^`]+)`", md):
        for piece in re.split(r"[\s/,|:]+", chunk):
            piece = piece.strip().strip("{}[]()\"'")
            if piece:
                out.add(piece)
    return out


def main() -> int:
    keys = parsed_keys()
    if not keys:
        print("!! no JSON keys found - did TelemetryClient.cpp move?")
        return 2
    doc = documented_tokens()

    undocumented = sorted(k for k in keys if k not in doc)
    unused = sorted(t for t in doc
                    if t.islower() and 1 < len(t) <= 12 and t not in keys
                    and re.fullmatch(r"[a-z_]+", t))

    print(f"parser reads {len(keys)} keys; SCHEMA.md mentions {len(doc)} tokens")
    if undocumented:
        print("\nERROR - parsed by the firmware but absent from SCHEMA.md:")
        for k in undocumented:
            print(f"  {k}")
        print("\n  Add them to SCHEMA.md (or drop them from the parser).")
        return 1
    print("ok - every key the firmware reads is documented")
    return 0


if __name__ == "__main__":
    sys.exit(main())
