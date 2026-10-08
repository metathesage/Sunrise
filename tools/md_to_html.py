#!/usr/bin/env python3
"""Minimal, dependency-free Markdown -> standalone styled HTML converter.

Purpose-built for docs/animation-character-systems.md. Supports the subset of
Markdown used there: ATX headings, fenced code blocks, GitHub-style tables,
unordered/ordered lists (incl. task checkboxes), blockquotes, horizontal rules,
bold/italic/inline code, and links.

Usage:
    python3 tools/md_to_html.py <input.md> <output.html> [--title "Title"]
"""
from __future__ import annotations

import argparse
import html
import re
import sys

INLINE_CODE = re.compile(r"`([^`]+)`")
BOLD = re.compile(r"\*\*([^*]+)\*\*")
ITALIC = re.compile(r"(?<![\*\w])\*([^*\n]+)\*(?!\*)")
LINK = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
SLUG_STRIP = re.compile(r"[^a-z0-9]+")


def slugify(text: str) -> str:
    text = re.sub(r"<[^>]+>", "", text)
    return SLUG_STRIP.sub("-", text.lower()).strip("-") or "section"


def inline(text: str) -> str:
    # Escape first, then re-introduce markup so we don't double-escape tags.
    out = html.escape(text, quote=False)
    out = INLINE_CODE.sub(lambda m: f"<code>{m.group(1)}</code>", out)
    out = BOLD.sub(lambda m: f"<strong>{m.group(1)}</strong>", out)
    out = ITALIC.sub(lambda m: f"<em>{m.group(1)}</em>", out)
    out = LINK.sub(
        lambda m: f'<a href="{html.escape(m.group(2), quote=True)}">{m.group(1)}</a>',
        out,
    )
    return out


def is_table_sep(line: str) -> bool:
    s = line.strip()
    return bool(s) and set(s) <= set("|-: ") and "|" in s and "-" in s


def split_row(line: str) -> list[str]:
    return [c.strip() for c in line.strip().strip("|").split("|")]


def convert(md: str) -> tuple[str, list[tuple[int, str, str]]]:
    lines = md.split("\n")
    html_lines: list[str] = []
    toc: list[tuple[int, str, str]] = []
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i]
        stripped = line.strip()

        # Fenced code block
        if stripped.startswith("```"):
            lang = stripped[3:].strip()
            i += 1
            buf = []
            while i < n and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1  # skip closing fence
            code = html.escape("\n".join(buf))
            cls = f' class="language-{html.escape(lang)}"' if lang else ""
            html_lines.append(f"<pre><code{cls}>{code}</code></pre>")
            continue

        # Horizontal rule
        if stripped in ("---", "***", "___"):
            html_lines.append("<hr />")
            i += 1
            continue

        # Heading
        m = re.match(r"^(#{1,6})\s+(.*)$", stripped)
        if m:
            level = len(m.group(1))
            text = m.group(2).strip()
            sid = slugify(text)
            toc.append((level, text, sid))
            html_lines.append(f'<h{level} id="{sid}">{inline(text)}</h{level}>')
            i += 1
            continue

        # Blockquote (may be multi-line)
        if stripped.startswith(">"):
            buf = []
            while i < n and lines[i].strip().startswith(">"):
                buf.append(lines[i].strip()[1:].strip())
                i += 1
            html_lines.append(f"<blockquote>{inline(' '.join(buf))}</blockquote>")
            continue

        # Table
        if "|" in line and i + 1 < n and is_table_sep(lines[i + 1]):
            header = split_row(line)
            i += 2
            rows = []
            while i < n and "|" in lines[i] and lines[i].strip():
                rows.append(split_row(lines[i]))
                i += 1
            thead = "".join(f"<th>{inline(c)}</th>" for c in header)
            body = "".join(
                "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
                for r in rows
            )
            html_lines.append(
                f"<table><thead><tr>{thead}</tr></thead><tbody>{body}</tbody></table>"
            )
            continue

        # Lists (unordered / ordered), one nesting level
        if re.match(r"^\s*([-*]|\d+\.)\s+", line):
            ordered = bool(re.match(r"^\s*\d+\.\s+", line))
            tag = "ol" if ordered else "ul"
            items = []
            while i < n and re.match(r"^\s*([-*]|\d+\.)\s+", lines[i]):
                indent = len(lines[i]) - len(lines[i].lstrip())
                content = re.sub(r"^\s*([-*]|\d+\.)\s+", "", lines[i])
                checkbox = ""
                cm = re.match(r"^\[([ xX])\]\s+(.*)$", content)
                if cm:
                    checked = " checked" if cm.group(1).lower() == "x" else ""
                    checkbox = f'<input type="checkbox" disabled{checked} /> '
                    content = cm.group(2)
                if indent >= 2 and items:
                    items[-1] += f"<br />{checkbox}{inline(content)}"
                else:
                    items.append(f"{checkbox}{inline(content)}")
                i += 1
            body = "".join(f"<li>{it}</li>" for it in items)
            html_lines.append(f"<{tag}>{body}</{tag}>")
            continue

        # Blank line
        if not stripped:
            i += 1
            continue

        # Paragraph (gather until blank / special)
        buf = [line.strip()]
        i += 1
        while i < n and lines[i].strip() and not re.match(
            r"^\s*(#{1,6}\s|[-*]\s|\d+\.\s|>|\||```)", lines[i]
        ):
            buf.append(lines[i].strip())
            i += 1
        html_lines.append(f"<p>{inline(' '.join(buf))}</p>")

    return "\n".join(html_lines), toc



TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>{title}</title>
<style>
  :root {{
    --bg: #0e1116; --panel: #151a22; --panel2: #1b2230; --fg: #d7dee8;
    --muted: #8b97a8; --accent: #6ea8fe; --accent2: #f0b866; --border: #263041;
    --code-bg: #0b0f14;
  }}
  * {{ box-sizing: border-box; }}
  html {{ scroll-behavior: smooth; }}
  body {{
    margin: 0; background: var(--bg); color: var(--fg);
    font: 16px/1.65 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  }}
  .layout {{ display: grid; grid-template-columns: 320px minmax(0, 1fr); }}
  nav.toc {{
    position: sticky; top: 0; align-self: start; height: 100vh; overflow-y: auto;
    background: var(--panel); border-right: 1px solid var(--border); padding: 24px 18px;
  }}
  nav.toc h2 {{ font-size: 13px; text-transform: uppercase; letter-spacing: .12em; color: var(--muted); margin: 0 0 12px; }}
  nav.toc a {{ display: block; color: var(--fg); text-decoration: none; font-size: 13.5px; padding: 3px 8px; border-radius: 6px; }}
  nav.toc a:hover {{ background: var(--panel2); color: var(--accent); }}
  nav.toc a.lvl3 {{ padding-left: 22px; color: var(--muted); font-size: 12.5px; }}
  main {{ max-width: 940px; padding: 48px 56px 96px; }}
  h1 {{ font-size: 34px; line-height: 1.2; margin: 0 0 8px; }}
  h2 {{ font-size: 25px; margin: 46px 0 14px; padding-top: 18px; border-top: 1px solid var(--border); }}
  h3 {{ font-size: 19px; margin: 30px 0 10px; color: var(--accent2); }}
  h4 {{ font-size: 16px; margin: 22px 0 8px; }}
  p {{ margin: 12px 0; }}
  a {{ color: var(--accent); }}
  code {{ background: var(--code-bg); border: 1px solid var(--border); border-radius: 5px; padding: 1px 6px; font-family: "SFMono-Regular", Consolas, "Liberation Mono", monospace; font-size: 13.5px; color: #e6c07b; }}
  pre {{ background: var(--code-bg); border: 1px solid var(--border); border-radius: 10px; padding: 16px 18px; overflow-x: auto; }}
  pre code {{ background: none; border: 0; padding: 0; color: var(--fg); font-size: 13px; line-height: 1.5; }}
  blockquote {{ margin: 16px 0; padding: 10px 18px; border-left: 3px solid var(--accent); background: var(--panel); color: var(--muted); border-radius: 0 8px 8px 0; }}
  table {{ border-collapse: collapse; width: 100%; margin: 18px 0; font-size: 14px; }}
  th, td {{ border: 1px solid var(--border); padding: 8px 12px; text-align: left; vertical-align: top; }}
  th {{ background: var(--panel2); color: var(--accent2); }}
  tr:nth-child(even) td {{ background: rgba(255,255,255,.02); }}
  ul, ol {{ margin: 12px 0; padding-left: 26px; }}
  li {{ margin: 5px 0; }}
  input[type=checkbox] {{ margin-right: 6px; }}
  hr {{ border: 0; border-top: 1px solid var(--border); margin: 34px 0; }}
  @media (max-width: 980px) {{ .layout {{ grid-template-columns: 1fr; }} nav.toc {{ position: relative; height: auto; }} main {{ padding: 28px 20px 64px; }} }}
</style>
</head>
<body>
<div class="layout">
<nav class="toc">
  <h2>Contents</h2>
  {toc}
</nav>
<main>
{body}
</main>
</div>
</body>
</html>
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--title", default="Document")
    args = ap.parse_args()

    with open(args.input, "r", encoding="utf-8") as f:
        md = f.read()

    body, toc = convert(md)
    toc_html = "\n".join(
        f'<a class="lvl{lvl}" href="#{sid}">{html.escape(text)}</a>'
        for lvl, text, sid in toc
        if lvl in (2, 3)
    )
    out = TEMPLATE.format(title=html.escape(args.title), toc=toc_html, body=body)

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(out)

    print(f"wrote {args.output} ({len(out)} bytes, {len(toc)} headings)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

