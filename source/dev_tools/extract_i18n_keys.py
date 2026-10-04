#!/usr/bin/env python3
"""Extract translatable UI strings from the CubeScript menus and scripts.

The i18n lookup keys on the exact text that reaches draw_text/conoutf, so keys
must be reproduced byte-for-byte, including CubeScript escapes such as \\i, \\t
and \\f (the translation pack is itself CubeScript, so the same escapes resolve
the same way there).

Usage:
    python3 source/dev_tools/extract_i18n_keys.py [-o keys.txt]

Prints one key per line (source spelling preserved), sorted and de-duplicated.
"""

import argparse
import glob
import os
import re
import sys

# menu commands whose first argument is shown to the user
LABEL_COMMANDS = (
    "menuitem", "menutitle", "menuheader",
    "menuitemslider", "menuitemcheckbox", "menuitemtextinput",
    "menuitemkeyinput", "menuitemradio",
)


def literal_label(raw):
    """Text a label argument resolves to, or None if it is computed.

    A quoted label goes through filterrichtext (escapes resolved), a bracketed
    one is evaluated as an expression and yields its raw contents (brackets
    stripped, escapes NOT resolved). We return the label in quoted spelling, so
    a bracketed label with a backslash in it is not representable and we skip
    it rather than emit a key that can never match.
    """
    if raw.startswith("["):
        if not raw.endswith("]"):
            return None
        body = raw[1:-1]
        if any(ch in body for ch in "$@\\"):  # computed, or escapes unresolved
            return None
        return body
    return raw


def split_args(s):
    """Split a CubeScript argument list, honouring quotes and brackets."""
    args, buf, i, n = [], [], 0, len(s)
    quote = None
    depth = 0
    while i < n:
        ch = s[i]
        if quote:
            if ch == "\\" and i + 1 < n:
                buf.append(ch); buf.append(s[i + 1]); i += 2; continue
            if ch == quote:
                quote = None
                args.append("".join(buf)); buf = []
                i += 1
                continue
            buf.append(ch)
        elif ch == '"':
            quote = ch  # CubeScript only quotes with ", so don't treat ' as one
        elif ch == "[":
            depth += 1
            buf.append(ch)
        elif ch == "]":
            depth -= 1
            buf.append(ch)
        elif ch.isspace() and depth == 0:
            if buf:
                args.append("".join(buf)); buf = []
        elif ch == ";" and depth == 0:
            break
        else:
            buf.append(ch)
        i += 1
    if buf:
        args.append("".join(buf))
    return args


def scan_file(path):
    keys = []
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$", line)
        if not m:
            continue
        cmd, rest = m.group(1), m.group(2)

        if cmd in LABEL_COMMANDS:
            args = split_args(rest)
            if not args:
                continue
            label = literal_label(args[0])
            if not label or label in ("-1", "[]") or label[0] in "($" or label.startswith("$"):
                continue
            keys.append(label)
        elif cmd in ("gamemodedesc",):
            args = split_args(rest)
            if len(args) >= 2:
                desc = literal_label(args[1])
                if desc:
                    keys.append(desc)
        elif cmd == "echo":
            args = split_args(rest)
            if args and args[0] and args[0][0] not in "([$":
                keys.append(args[0])
    return keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", help="write keys here instead of stdout")
    ap.add_argument("--root", default=".", help="AssaultCube root directory")
    args = ap.parse_args()

    patterns = [
        "config/menus*.cfg",
        "config/scripts.cfg",
        "config/opt/faq.cfg",
        "config/opt/survival.cfg",
        "config/opt/*.cfg",
    ]
    files = []
    for p in patterns:
        files.extend(sorted(glob.glob(os.path.join(args.root, p))))
    files = sorted(set(files))

    keys = []
    for f in files:
        found = scan_file(f)
        if found:
            keys.extend(found)

    seen, ordered = set(), []
    for k in keys:
        if k not in seen:
            seen.add(k)
            ordered.append(k)

    out = "\n".join(ordered) + "\n"
    if args.output:
        open(args.output, "w", encoding="utf-8").write(out)
        sys.stderr.write("%d unique keys from %d files -> %s\n" % (len(ordered), len(files), args.output))
    else:
        sys.stdout.write(out)
        sys.stderr.write("%d unique keys from %d files\n" % (len(ordered), len(files)))


if __name__ == "__main__":
    main()
