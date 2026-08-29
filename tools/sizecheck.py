#!/usr/bin/env python3
"""Enforce the contest budget.

The rule is 1,474,560 bytes -- the real capacity of a 3.5" HD floppy --
measured AFTER decompression, across every file needed to run the game.
So we sum the whole dist tree, not just the .exe.

Exits non-zero when over budget, which fails the CI job.
"""

import os
import sys

LIMIT = 1_474_560  # bytes, per the official rules


def collect(root):
    files = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            path = os.path.join(dirpath, name)
            files.append((os.path.relpath(path, root), os.path.getsize(path)))
    return sorted(files, key=lambda f: -f[1])


def bar(fraction, width=44):
    filled = min(width, int(round(fraction * width)))
    return "#" * filled + "." * (width - filled)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "dist"

    if not os.path.isdir(root):
        print(f"sizecheck: no such directory: {root}", file=sys.stderr)
        return 2

    files = collect(root)
    if not files:
        print(f"sizecheck: {root} is empty -- did the build run?", file=sys.stderr)
        return 2

    total = sum(size for _, size in files)
    free = LIMIT - total
    pct = total / LIMIT

    lines = [
        "A:\\> CHKDSK",
        "",
        f"  {'file':<28}{'bytes':>12}{'% of disk':>12}",
        f"  {'-' * 52}",
    ]
    for name, size in files:
        lines.append(f"  {name:<28}{size:>12,}{size / LIMIT:>11.1%}")
    lines += [
        f"  {'-' * 52}",
        f"  {'total':<28}{total:>12,}{pct:>11.1%}",
        f"  {'free':<28}{free:>12,}",
        "",
        f"  [{bar(pct)}]",
        "",
    ]

    if total > LIMIT:
        lines.append(f"  OVER BUDGET by {total - LIMIT:,} bytes. Limit is {LIMIT:,}.")
        status = 1
    else:
        lines.append(f"  OK -- {free:,} bytes still free on the disk.")
        status = 0

    report = "\n".join(lines)
    print(report)

    # Mirror the report into the GitHub Actions run summary so the result is
    # readable from a phone without opening the log.
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        verdict = "OVER BUDGET" if status else "within budget"
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(f"## Disk usage: {total:,} / {LIMIT:,} bytes ({pct:.1%}) -- {verdict}\n\n")
            fh.write("```\n" + report + "\n```\n")

    return status


if __name__ == "__main__":
    sys.exit(main())
