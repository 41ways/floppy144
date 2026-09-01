#!/usr/bin/env python3
"""Turn docs/PROPOSALS.md into the artifact, images and all.

Generated rather than hand-written so the page and the file cannot drift:
the file is what gets read at home and the page is what gets read here, and
two copies of fifty proposals maintained by hand would be one copy and one
lie. Handles exactly the markdown PROPOSALS.md uses -- headings, tables,
blockquotes, fences, bold/italic/strike/code, and image links, which are
inlined as data URIs because the artifact sandbox blocks every other host.
"""
import base64
import html
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
REPO = ROOT.parent

md = (REPO / "docs" / "PROPOSALS.md").read_text()
shots = {f.name: base64.b64encode(f.read_bytes()).decode()
         for f in sorted((REPO / "docs" / "shots").glob("*.png"))}


def inline(t):
    t = html.escape(t)
    t = re.sub(r"!\[\]\(shots/([a-z0-9-]+\.png)\)",
               lambda m: '<img class="shot" loading="lazy" alt="%s" src="data:image/png;base64,%s">'
                         % (m.group(1)[:-4], shots.get(m.group(1), "")), t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)
    t = re.sub(r"~~(.+?)~~", r"<s>\1</s>", t)
    # Bold is already gone, so any surviving pair of asterisks is an italic.
    # The obvious guard -- no word character either side -- is wrong for
    # Korean, where the particle attaches straight to the closing asterisk:
    # *내가 작아진 것*으로. Every italic in the file failed that lookahead.
    t = re.sub(r"\*([^*\n]+?)\*", r"<em>\1</em>", t)
    t = re.sub(r"`(.+?)`", r"<code>\1</code>", t)
    return t


lines = md.split("\n")
out, i = [], 0
while i < len(lines):
    ln = lines[i]

    if ln.startswith("# "):
        out.append("<h1>%s</h1>" % inline(ln[2:])); i += 1; continue
    if ln.startswith("## "):
        out.append("<h2>%s</h2>" % inline(ln[3:])); i += 1; continue
    if ln.startswith("### "):
        out.append("<h3>%s</h3>" % inline(ln[4:])); i += 1; continue
    if ln.strip() == "---":
        out.append("<hr>"); i += 1; continue
    if not ln.strip():
        i += 1; continue

    if ln.startswith("```"):
        i += 1
        buf = []
        while i < len(lines) and not lines[i].startswith("```"):
            buf.append(lines[i]); i += 1
        i += 1
        out.append("<pre><code>%s</code></pre>" % html.escape("\n".join(buf)))
        continue

    if ln.startswith("|"):
        rows = []
        while i < len(lines) and lines[i].startswith("|"):
            rows.append([c.strip() for c in lines[i].strip("|").split("|")])
            i += 1
        head = None
        if len(rows) > 1 and set("".join(rows[1])) <= set("-: "):
            head, rows = rows[0], rows[2:]
        t = '<div class="tw"><table>'
        if head:
            t += "<thead><tr>%s</tr></thead>" % "".join("<th>%s</th>" % inline(c) for c in head)
        t += "<tbody>%s</tbody></table></div>" % "".join(
            "<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r) for r in rows)
        out.append(t)
        continue

    if ln.startswith(">"):
        buf = []
        while i < len(lines) and lines[i].startswith(">"):
            buf.append(lines[i].lstrip(">").lstrip()); i += 1
        paras = [p for p in "\n".join(buf).split("\n\n") if p.strip()]
        out.append("<blockquote>%s</blockquote>"
                   % "".join("<p>%s</p>" % inline(p.replace("\n", " ")) for p in paras))
        continue

    # a paragraph: everything up to a blank line or the next block marker.
    # i is always advanced at least once, or this loops for ever -- which is
    # exactly what the first version of this script did.
    buf = [ln]
    i += 1
    while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "|", ">", "`", "---")):
        buf.append(lines[i]); i += 1
    para = " ".join(buf)
    entry = para.startswith("**") or para.startswith("~~")
    body = inline(para)
    body = body.replace("→ ", '<br><span class="arrow">→</span> ')
    out.append('<p%s>%s</p>' % (' class="entry"' if entry else "", body))

tpl = (ROOT / "md2artifact.tpl.html").read_text()
page = tpl.replace("<!--BODY-->", "\n".join(out))
(REPO / "proposals.html").write_text(page)
print("built %d KB, %d images" % (len(page) // 1024, len(shots)), file=sys.stderr)
