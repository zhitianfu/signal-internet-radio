#!/usr/bin/env python3
"""Fetch latin subsets of Archivo / IBM Plex Mono / Newsreader from Google
Fonts and write a self-contained fonts.css (base64 woff2 data URIs) so the
prototype renders with its real typography offline."""
import re, base64, os, sys, urllib.request

UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "fonts.css")

FAMS = [
    "family=Archivo:wght@400;500;600;700;800",
    "family=IBM+Plex+Mono:wght@400;500;600",
    "family=Newsreader:ital,wght@1,400;1,600",
]
url = "https://fonts.googleapis.com/css2?" + "&".join(FAMS) + "&display=swap"

def get(u):
    req = urllib.request.Request(u, headers={"User-Agent": UA})
    return urllib.request.urlopen(req, timeout=45).read()

css = get(url).decode("utf-8")
blocks = re.findall(r"(/\*\s*([\w-]+)\s*\*/\s*@font-face\s*\{[^}]+\})", css)
out, seen, total = [], {}, 0
for full, subset in blocks:
    if subset not in ("latin", "latin-ext"):
        continue
    m = re.search(r"url\((https://[^)]+\.woff2)\)", full)
    if not m:
        continue
    u = m.group(1)
    if u in seen:
        data = seen[u]
    else:
        data = get(u)
        seen[u] = data
    b64 = base64.b64encode(data).decode("ascii")
    block = re.sub(r"url\(https://[^)]+\.woff2\)",
                   "url(data:font/woff2;base64,%s)" % b64, full)
    block = re.sub(r"^/\*.*?\*/\s*", "", block)
    fam = re.search(r"font-family:\s*'([^']+)'", block).group(1)
    wght = re.search(r"font-weight:\s*([^;]+);", block)
    print("  %-16s %-9s %s  %6d B" % (fam, subset, wght.group(1).strip() if wght else "?", len(data)))
    out.append(block)
    total += len(data)

header = ("/* Self-contained latin subsets — Archivo, IBM Plex Mono, Newsreader.\n"
          "   Fonts by Google Fonts, SIL Open Font License 1.1.\n"
          "   Regenerate: python3 build_fonts.py */\n")
with open(OUT, "w") as f:
    f.write(header + "\n".join(out) + "\n")
print("wrote %s  (%.0f KB raw font data, %.0f KB file)" %
      (OUT, total / 1024, os.path.getsize(OUT) / 1024))
