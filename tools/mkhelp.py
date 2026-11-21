#!/usr/bin/env python3
"""mkhelp.py OUT.DAT PAGE.MD...: the user guide's pages (help/*.MD) into the one file Help reads (HELP.DAT, beside
the programs), after holding them to the guide's rules. A page that breaks one stops the build:

  - a page's name is DOS's (eight and three, capitals) and ends .MD; INDEX.MD is there;
  - a page begins with its title, a line "# Title";
  - a link goes to a page that is there ("FILES.MD", "FILES.MD#part"), or out ("http://", "https://");
  - every page is in INDEX.MD's contents (its list of links), so that every page can be reached;
  - no line begins "=== " (the mark between pages in the file), and the text is UTF-8.

The file: a line "GLOSHELP 1", then each page as a line "=== NAME.MD" and its text (INDEX.MD first, the rest by
name)."""
import os
import re
import sys

LINK = re.compile(r"(?<!\!)\[[^\]\n]*\]\(([^)\s]+)\)")


def main():
    out, paths = sys.argv[1], sys.argv[2:]
    pages, bad = {}, []
    for p in paths:
        name = os.path.basename(p)
        if not re.fullmatch(r"[A-Z0-9_-]{1,8}\.MD", name):
            bad.append("%s: not a DOS name of capitals ending .MD" % p)
            continue
        raw = open(p, "rb").read()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as e:
            bad.append("%s: not UTF-8 (%s)" % (p, e))
            continue
        text = text.replace("\r\n", "\n")
        if not text.startswith("# ") or not text.split("\n", 1)[0][2:].strip():
            bad.append("%s: does not begin with its title (# Title)" % p)
        if any(line.startswith("=== ") for line in text.split("\n")):
            bad.append("%s: a line begins '=== '" % p)
        pages[name] = text if text.endswith("\n") else text + "\n"
    if "INDEX.MD" not in pages:
        bad.append("no INDEX.MD")
    listed = set()
    for name, text in sorted(pages.items()):
        fenced = False
        for n, line in enumerate(text.split("\n"), 1):
            if line.lstrip().startswith("```"):
                fenced = not fenced
            if fenced:
                continue
            for to in LINK.findall(re.sub(r"`[^`]*`", "", line)):
                if re.match(r"https?://", to):
                    continue
                page = to.split("#", 1)[0]
                if page.upper() not in pages:
                    bad.append("help/%s:%d: a link to %s, which is not a page" % (name, n, to))
                elif name == "INDEX.MD" and re.match(r"\s*[-*] \[", line):
                    listed.add(page.upper())
    for name in sorted(pages):
        if name != "INDEX.MD" and name not in listed:
            bad.append("help/%s: not in INDEX.MD's contents" % name)
    if bad:
        sys.stderr.write("mkhelp: the guide's pages:\n" + "".join("  " + b + "\n" for b in bad))
        return 1
    order = ["INDEX.MD"] + sorted(n for n in pages if n != "INDEX.MD")
    with open(out, "wb") as f:
        f.write(b"GLOSHELP 1\n")
        for name in order:
            f.write(("=== %s\n" % name).encode() + pages[name].encode("utf-8"))
    print("mkhelp: %d pages, %d bytes" % (len(order), os.path.getsize(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
