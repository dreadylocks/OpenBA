#!/usr/bin/env python3
"""Print the CHANGELOG.md section for one version (the release notes body).

usage: tools/release/changelog_section.py <version> [CHANGELOG.md]
A section starts at a line `## vX.Y.Z ...` and ends before the next `## `. Exit 1 when the
version has no section: the release workflow refuses to publish a release without notes.
"""
import re
import sys

ver = sys.argv[1].lstrip("v")
path = sys.argv[2] if len(sys.argv) > 2 else "CHANGELOG.md"
lines = open(path, encoding="utf-8").read().splitlines()
start = None
for i, line in enumerate(lines):
    if re.match(r"^## v?%s(\s|$)" % re.escape(ver), line):
        start = i + 1
        break
if start is None:
    sys.exit("CHANGELOG.md has no '## v%s' section" % ver)
body = []
for line in lines[start:]:
    if line.startswith("## "):
        break
    body.append(line)
text = "\n".join(body).strip()
if not text:
    sys.exit("CHANGELOG.md section for v%s is empty" % ver)
print(text)
