"""Guard the wire contract between SCHEMA.md and the firmware parser.

SCHEMA.md says the firmware parser is the source of truth and that a producer
renaming a field "silently desyncs the board". Nothing enforced that: the doc
and the parser could drift apart for months without a single error.

This checks the one direction that actually breaks things:
  * every JSON key TelemetryClient.cpp reads must appear in EVERY SCHEMA file
    -> ERROR (the board depends on a field nobody documented)
  * keys documented but never read are reported as warnings (a producer may
    legitimately emit more than the board consumes)

Every SCHEMA file, plural, since the document exists in two languages. A
translation is a second copy of a contract, and a second copy of a contract is
a copy that drifts — the server repository had one that still said "cfg: 15
fields" and "last synced at firmware v1.8.8" while the firmware was on v1.44.1.
Checking only the English one would have let the Russian rot exactly the same
way, quietly, which is worse than having no Russian at all.

Run:  python tools/check_schema.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARSER = ROOT / "src" / "net" / "TelemetryClient.cpp"
# SCHEMA.md and every translation beside it (SCHEMA.ru.md, …).
SCHEMAS = sorted(ROOT.glob("SCHEMA*.md"))

# doc["ct"], rc["screen"], nodes[i]["cpu"], hdd[i]["n"] ...
KEY_RE = re.compile(r'\b(?:doc|rc|c|e|f|s|nt|nodes\[i\]|list\[i\]|hdd\[i\]'
                    r'|tp\[i\]|tr\[i\]|wf\[i\])\["([A-Za-z_][\w]*)"\]')

# Keys the firmware writes rather than reads, or that are structural.
IGNORE = {"seq"}


def parsed_keys() -> set[str]:
    src = PARSER.read_text(encoding="utf-8")
    return {k for k in KEY_RE.findall(src)} - IGNORE


def documented_tokens(path: Path) -> set[str]:
    md = path.read_text(encoding="utf-8")
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
    if not SCHEMAS:
        print("!! no SCHEMA*.md found next to the repository root")
        return 2

    print(f"parser reads {len(keys)} keys")
    failed = False
    for path in SCHEMAS:
        doc = documented_tokens(path)
        undocumented = sorted(k for k in keys if k not in doc)
        print(f"  {path.name}: {len(doc)} tokens", end="")
        if undocumented:
            failed = True
            print("\n    ERROR - parsed by the firmware but absent from this file:")
            for k in undocumented:
                print(f"      {k}")
            print(f"    Add them to {path.name} (or drop them from the parser).")
        else:
            print(" - every key the firmware reads is documented")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
