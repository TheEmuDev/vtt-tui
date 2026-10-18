#!/usr/bin/env python3
"""Rewrite the what-to-do column of README.md's code table from src/maptools.c's
CODE_HELP, after a help line changes. Suite `checkhelp` fails while the two
disagree. Run from the repository's root:

    python3 -I tools/codehelp.py
"""
import re

src = open("src/maptools.c").read()
start = src.index("static const CodeHelp CODE_HELP[] = {")
end = src.index("};", start)
helps = {}
for code, slug, h in re.findall(r'\{ "([EWN]\d{3})", (NULL|"[^"]*"), "((?:[^"\\]|\\.)*)"', src[start:end]):
    helps.setdefault(code, []).append(h.replace('\\"', '"').replace("\\'", "'"))


def readme_form(h):
    """The README's own sections as links; a bar escaped for the table."""
    return (h.replace("the README's File format", "[File format](#file-format)")
             .replace("the README's Rulesets", "[Rulesets](#rulesets)")
             .replace("|", "\\|"))


readme = open("README.md").read()
a = readme.index("| code | finds | what to do |")
b = readme.index("\n\n", a)
out = []
for line in readme[a:b].split("\n"):
    m = re.match(r"^\| (`[EWN]\d{3}[^|]*) \| ((?:[^|]|\\\|)*?) \| (.*) \|$", line)
    if m:
        todo = []
        for code in re.findall(r"`([EWN]\d{3})", m.group(1)):
            for h in helps.get(code, []):
                if h not in todo:
                    todo.append(h)
        line = "| %s | %s | %s |" % (m.group(1), m.group(2), "; ".join(readme_form(h) for h in todo))
    out.append(line)
new = readme[:a] + "\n".join(out) + readme[b:]
if new != readme:
    open("README.md", "w").write(new)
    print("README.md: the what-to-do column rewritten")
else:
    print("README.md: the what-to-do column already agrees")
