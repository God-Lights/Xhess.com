#!/usr/bin/env python3
"""Writes tuned evaluation weights into src/eval/params.def (values only: comments and layout are kept).

    tools/apply_params.py src/eval/params.def tuned.txt

tuned.txt is the output of albatross_tuner (or `albatross_tuner --dump-defaults`): one line per params.def entry,
    name = v0 v1 ...        (SCORE: mg eg; INT: value; ARRAY: all elements)
Every entry of tuned.txt must exist in params.def with the same number of values; nothing is written otherwise.
"""
import re
import sys

ENTRY = re.compile(r"^(SCORE|INT|ARRAY)\(\s*(\w+)\s*,(.*?)\)", re.MULTILINE | re.DOTALL)


def parse_values(text):
    values = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, sep, rest = line.partition("=")
        if not sep:
            raise ValueError(f"expected 'name = values': {line!r}")
        values[name.strip()] = [int(v) for v in rest.split()]
    return values


def current_values(def_text):
    """The numbers currently in params.def, in the same shape as parse_values()."""
    out = {}
    for m in ENTRY.finditer(def_text):
        kind, name, body = m.group(1), m.group(2), m.group(3)
        nums = [int(x) for x in re.findall(r"-?\d+", body)]
        out[name] = nums[1:] if kind == "ARRAY" else nums   # ARRAY: the first number is the size
    return out


def format_entry(kind, name, size, vals):
    if kind == "SCORE":
        return f"SCORE({name}, {vals[0]}, {vals[1]})"
    if kind == "INT":
        return f"INT({name}, {vals[0]})"
    if size == 64:   # tables: eight rows of eight
        rows = [", ".join(f"{v:4d}" for v in vals[r * 8:(r + 1) * 8]) + "," for r in range(8)]
        return f"ARRAY({name}, 64,\n" + "\n".join("    " + r for r in rows) + "\n)"
    return f"ARRAY({name}, {size}, " + ", ".join(str(v) for v in vals) + ")"


def apply(def_text, values):
    known = {m.group(2) for m in ENTRY.finditer(def_text)}
    unknown = sorted(set(values) - known)
    if unknown:
        raise ValueError("not in params.def: " + ", ".join(unknown))
    used = set()

    def sub(m):
        kind, name, body = m.group(1), m.group(2), m.group(3)
        if name not in values:
            return m.group(0)
        vals = values[name]
        size = int(body.split(",")[0]) if kind == "ARRAY" else 0
        expected = 2 if kind == "SCORE" else 1 if kind == "INT" else size
        if len(vals) != expected:
            raise ValueError(f"{name}: expected {expected} values, got {len(vals)}")
        used.add(name)
        return format_entry(kind, name, size, vals)

    return ENTRY.sub(sub, def_text)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    def_path, tuned_path = argv[1], argv[2]
    text = open(def_path, encoding="utf-8").read()
    try:
        new = apply(text, parse_values(open(tuned_path, encoding="utf-8").read()))
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if new != text:
        open(def_path, "w", encoding="utf-8").write(new)
    changed = sum(1 for k, v in current_values(new).items() if current_values(text).get(k) != v)
    print(f"{def_path}: {changed} entries changed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
