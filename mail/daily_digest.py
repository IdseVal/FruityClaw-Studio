"""Daily agent digest -- the workflow's ONE e-mail per day.

Derived from commit and pull-request history plus the live label state, never from
agent-written logs. Agents forget, parallel worktrees conflict on a shared file, and a
report assembled from self-description tells you what each agent believed -- the very
thing being checked.

Contract (see agent-workflow-setup.md section 11):

* Sources: ``git log <branch> --since=<day> --until=<day+1> --no-merges``,
  ``gh pr list --base <branch> --state merged|all --json ...`` and
  ``gh issue list --state open --json ...`` for the attention section.
* Sections, in order: WAITING ON YOU (open items labelled needs-human / escalated /
  proposed), pull requests opened today, pull requests merged today, commits by author.
* Quiet day: nothing landed, nothing opened AND nothing waiting -> prints a line and
  sends no mail. Anything waiting -> the mail is sent even on a silent day.
* No credentials: prints the rendered HTML instead of sending.
* Any missing tool, branch or auth degrades to "nothing from this source"; the script
  never exits non-zero.
* Every string coming from a commit message, title or label is HTML-escaped.
* Field split uses ``\\x1f`` / ``\\x1e`` control characters; a commit subject can
  contain any printable delimiter you might otherwise pick.
* Mail is sent as ``MIMEText(body, "html", "utf-8")``; the default is us-ascii and
  mangles punctuation.
"""

from __future__ import annotations

import argparse
import html
import json
import os
import smtplib
import subprocess
import sys
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from email.mime.text import MIMEText

# Control characters, not printable ones. A commit subject can legitimately contain any
# character a human types, so a printable delimiter is a bug waiting for a colourful
# subject line.
UNIT_SEP = "\x1f"
RECORD_SEP = "\x1e"

# git and gh are cheap; a 60s ceiling keeps the workflow from hanging on a broken remote.
SUBPROCESS_TIMEOUT_SECONDS = 60

# Open items carrying any of these labels appear under WAITING ON YOU.
ATTENTION_LABELS = ("needs-human", "escalated", "proposed")


@dataclass(frozen=True)
class Commit:
    """One commit, as reported by ``git log``."""

    sha: str
    author: str
    subject: str


@dataclass(frozen=True)
class PullRequest:
    """One pull request, as reported by ``gh pr list``."""

    number: int
    title: str
    author: str
    labels: tuple[str, ...]
    url: str
    stamp: str  # mergedAt for merged lists, createdAt for opened lists


@dataclass(frozen=True)
class Attention:
    """An open issue or pull request that is waiting on the human."""

    kind: str  # "issue" or "PR"
    number: int
    title: str
    url: str
    flags: tuple[str, ...]  # the attention labels it carries


def parse_args() -> argparse.Namespace:
    """Parse the CLI. All arguments are optional; defaults match the workflow."""

    parser = argparse.ArgumentParser(
        description="Daily agent digest: git history, pull requests, and open items "
                    "waiting on the human.",
    )
    parser.add_argument(
        "--branch",
        default="dev",
        help="Branch to summarise. Defaults to dev.",
    )
    parser.add_argument(
        "--day",
        default="",
        help="Day to report, YYYY-MM-DD (UTC). Empty means today.",
    )
    parser.add_argument(
        "--print",
        dest="print_only",
        action="store_true",
        help="Render the HTML to stdout and send no mail.",
    )
    return parser.parse_args()


def resolve_day(day_arg: str) -> date:
    """Return the day to report on. Bad input falls back to today rather than raising --
    the workflow must never fail because someone typed a date wrong.
    """

    if not day_arg:
        return datetime.now(timezone.utc).date()
    try:
        return datetime.strptime(day_arg, "%Y-%m-%d").date()
    except ValueError:
        print(
            f"!! Bad --day {day_arg!r}; using today (UTC).",
            file=sys.stderr,
        )
        return datetime.now(timezone.utc).date()


def _run(argv: list[str]) -> tuple[bool, str]:
    """Run a command and return ``(ok, stdout)``. Never raises.

    A missing tool, an auth failure or a non-zero exit degrades to ``(False, "")`` so the
    caller can treat the source as empty for the day.
    """

    try:
        result = subprocess.run(
            argv,
            check=False,
            capture_output=True,
            text=True,
            timeout=SUBPROCESS_TIMEOUT_SECONDS,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as exc:
        print(f"!! {argv[0]} unavailable: {exc}", file=sys.stderr)
        return False, ""
    if result.returncode != 0:
        stderr = (result.stderr or "").strip()
        if stderr:
            print(
                f"!! {' '.join(argv[:3])} failed: {stderr}",
                file=sys.stderr,
            )
        return False, ""
    return True, result.stdout


def _gh_list(argv: list[str]) -> list[dict]:
    """Run a gh listing command and parse its JSON. Empty list on any failure."""

    ok, out = _run(argv)
    if not ok or not out.strip():
        return []
    try:
        parsed = json.loads(out)
    except json.JSONDecodeError as exc:
        print(f"!! gh returned unparseable JSON: {exc}", file=sys.stderr)
        return []
    return parsed if isinstance(parsed, list) else []


def _parse_stamp(raw: str) -> datetime | None:
    """Parse gh's ISO-8601 timestamps. Returns None if unparseable."""

    if not raw:
        return None
    try:
        return datetime.fromisoformat(raw.replace("Z", "+00:00"))
    except ValueError:
        return None


def _window(day: date) -> tuple[datetime, datetime]:
    start = datetime.combine(day, datetime.min.time(), tzinfo=timezone.utc)
    return start, start + timedelta(days=1)


def read_commits(branch: str, day: date) -> list[Commit]:
    """Return commits on ``branch`` for the UTC day given.

    ``--since`` and ``--until`` bracket a 24-hour window; ``--no-merges`` drops merge
    commits so a pull request does not double-count with its own contents.
    """

    since = day.isoformat()
    until = (day + timedelta(days=1)).isoformat()
    pretty = f"%H{UNIT_SEP}%an{UNIT_SEP}%s{RECORD_SEP}"
    ok, out = _run(
        [
            "git",
            "log",
            branch,
            f"--since={since}",
            f"--until={until}",
            "--no-merges",
            f"--pretty=format:{pretty}",
        ]
    )
    if not ok or not out.strip():
        return []

    commits: list[Commit] = []
    for record in out.split(RECORD_SEP):
        record = record.strip("\n")
        if not record:
            continue
        parts = record.split(UNIT_SEP)
        if len(parts) < 3:
            continue
        # The subject can itself contain UNIT_SEP in the pathological case; rejoin the
        # tail rather than dropping content.
        sha, author = parts[0], parts[1]
        subject = UNIT_SEP.join(parts[2:])
        commits.append(Commit(sha=sha, author=author, subject=subject))
    return commits


def _pull_from_entry(entry: dict, stamp_field: str) -> PullRequest:
    author_field = entry.get("author") or {}
    author = author_field.get("login") or "unknown"
    labels = tuple(
        item.get("name", "")
        for item in (entry.get("labels") or [])
        if item.get("name")
    )
    return PullRequest(
        number=int(entry.get("number", 0)),
        title=entry.get("title", ""),
        author=author,
        labels=labels,
        url=entry.get("url", "") or "",
        stamp=entry.get(stamp_field) or "",
    )


def read_merged_prs(branch: str, day: date) -> list[PullRequest]:
    """Pull requests merged into ``branch`` during the UTC day given.

    gh has no ``--merged-on`` filter, so we list merged PRs and window-filter locally.
    """

    entries = _gh_list(
        [
            "gh", "pr", "list",
            "--base", branch,
            "--state", "merged",
            "--limit", "200",
            "--json", "number,title,author,labels,mergedAt,url",
        ]
    )
    start, end = _window(day)
    prs: list[PullRequest] = []
    for entry in entries:
        merged = _parse_stamp(entry.get("mergedAt") or "")
        if merged is None or not (start <= merged < end):
            continue
        prs.append(_pull_from_entry(entry, "mergedAt"))
    prs.sort(key=lambda pull: pull.stamp)
    return prs


def read_opened_prs(branch: str, day: date) -> list[PullRequest]:
    """Pull requests OPENED against ``branch`` during the UTC day given, whatever their
    state now -- the reader wants to know work was published, not only that it landed."""

    entries = _gh_list(
        [
            "gh", "pr", "list",
            "--base", branch,
            "--state", "all",
            "--limit", "200",
            "--json", "number,title,author,labels,createdAt,url",
        ]
    )
    start, end = _window(day)
    prs: list[PullRequest] = []
    for entry in entries:
        created = _parse_stamp(entry.get("createdAt") or "")
        if created is None or not (start <= created < end):
            continue
        prs.append(_pull_from_entry(entry, "createdAt"))
    prs.sort(key=lambda pull: pull.stamp)
    return prs


def read_attention(branch: str) -> list[Attention]:
    """Every OPEN issue and pull request carrying an attention label, regardless of age.

    This is the section that lets one mail a day be enough: a `needs-human` question,
    an `escalated` issue or a `proposed` suggestion reappears every day until acted on.
    """

    items: list[Attention] = []

    for entry in _gh_list(
        [
            "gh", "issue", "list",
            "--state", "open",
            "--limit", "200",
            "--json", "number,title,labels,url",
        ]
    ):
        names = {item.get("name", "") for item in (entry.get("labels") or [])}
        flags = tuple(label for label in ATTENTION_LABELS if label in names)
        if flags:
            items.append(Attention(
                kind="issue",
                number=int(entry.get("number", 0)),
                title=entry.get("title", ""),
                url=entry.get("url", "") or "",
                flags=flags,
            ))

    for entry in _gh_list(
        [
            "gh", "pr", "list",
            "--base", branch,
            "--state", "open",
            "--limit", "200",
            "--json", "number,title,labels,url",
        ]
    ):
        names = {item.get("name", "") for item in (entry.get("labels") or [])}
        flags = tuple(label for label in ATTENTION_LABELS if label in names)
        if flags:
            items.append(Attention(
                kind="PR",
                number=int(entry.get("number", 0)),
                title=entry.get("title", ""),
                url=entry.get("url", "") or "",
                flags=flags,
            ))

    # Questions before escalations before suggestions; then oldest number first.
    order = {label: rank for rank, label in enumerate(ATTENTION_LABELS)}
    items.sort(key=lambda a: (min(order[f] for f in a.flags), a.number))
    return items


def _pr_list_html(prs: list[PullRequest]) -> list[str]:
    escape = html.escape
    parts: list[str] = ["<ul>"]
    for pull in prs:
        label_html = ""
        if pull.labels:
            label_html = " " + " ".join(
                f"<code>{escape(name)}</code>" for name in pull.labels
            )
        title_html = escape(pull.title)
        link_open = f'<a href="{escape(pull.url)}">' if pull.url else ""
        link_close = "</a>" if pull.url else ""
        parts.append(
            f"<li>#{pull.number} {link_open}{title_html}{link_close}"
            f" &mdash; {escape(pull.author)}{label_html}</li>"
        )
    parts.append("</ul>")
    return parts


def render_html(
    branch: str,
    day: date,
    attention: list[Attention],
    opened: list[PullRequest],
    merged: list[PullRequest],
    commits: list[Commit],
) -> str:
    """Render the digest as HTML. Every value from git or gh is escaped."""

    escape = html.escape
    parts: list[str] = [
        f"<h1>Daily digest &mdash; {escape(branch)} &mdash; {day.isoformat()}</h1>",
    ]

    parts.append("<h2>Waiting on you</h2>")
    if not attention:
        parts.append("<p><em>nothing -- the machine needs no answers today</em></p>")
    else:
        parts.append("<ul>")
        for item in attention:
            flags = " ".join(f"<code>{escape(flag)}</code>" for flag in item.flags)
            link_open = f'<a href="{escape(item.url)}">' if item.url else ""
            link_close = "</a>" if item.url else ""
            parts.append(
                f"<li>{escape(item.kind)} #{item.number} {link_open}"
                f"{escape(item.title)}{link_close} &mdash; {flags}</li>"
            )
        parts.append("</ul>")
        parts.append(
            "<p><em>Answer in the item's comments; remove the label when done. "
            "A <code>proposed</code> item wants the <code>ready</code> label (or to be "
            "closed).</em></p>"
        )

    parts.append("<h2>Pull requests opened</h2>")
    if not opened:
        parts.append("<p><em>none</em></p>")
    else:
        parts.extend(_pr_list_html(opened))

    parts.append("<h2>Pull requests merged</h2>")
    if not merged:
        parts.append("<p><em>none</em></p>")
    else:
        parts.extend(_pr_list_html(merged))

    parts.append("<h2>Commits</h2>")
    if not commits:
        parts.append("<p><em>none</em></p>")
    else:
        grouped: dict[str, list[Commit]] = {}
        for commit in commits:
            grouped.setdefault(commit.author, []).append(commit)
        for author in sorted(grouped):
            parts.append(f"<h3>{escape(author)}</h3><ul>")
            for commit in grouped[author]:
                parts.append(
                    f"<li><code>{escape(commit.sha[:7])}</code> "
                    f"{escape(commit.subject)}</li>"
                )
            parts.append("</ul>")

    return "\n".join(parts)


def _smtp_config() -> dict[str, str] | None:
    """Return the SMTP config if every variable is set, else None."""

    keys = (
        "SMTP_SENDER_EMAIL",
        "SMTP_RECEIVER_EMAIL",
        "SMTP_PASSWORD",
        "SMTP_SERVER",
        "SMTP_PORT",
    )
    values = {key: os.environ.get(key, "").strip() for key in keys}
    if not all(values.values()):
        return None
    return values


def send_or_print(html_body: str, subject: str, print_only: bool) -> None:
    """Print the body when asked, when credentials are missing, or when SMTP fails.

    The script always leaves the reader with the digest somewhere they can see it; a mail
    outage must not silently swallow the day.
    """

    if print_only:
        print(html_body)
        return

    config = _smtp_config()
    if config is None:
        # Missing credentials are the deliberate first-run state; do not warn.
        print(html_body)
        return

    try:
        port = int(config["SMTP_PORT"])
    except ValueError:
        print(
            f"!! Bad SMTP_PORT {config['SMTP_PORT']!r}; printing instead.",
            file=sys.stderr,
        )
        print(html_body)
        return

    message = MIMEText(html_body, "html", "utf-8")
    message["Subject"] = subject
    message["From"] = config["SMTP_SENDER_EMAIL"]
    message["To"] = config["SMTP_RECEIVER_EMAIL"]

    try:
        with smtplib.SMTP_SSL(config["SMTP_SERVER"], port, timeout=30) as smtp:
            smtp.login(config["SMTP_SENDER_EMAIL"], config["SMTP_PASSWORD"])
            smtp.sendmail(
                config["SMTP_SENDER_EMAIL"],
                [config["SMTP_RECEIVER_EMAIL"]],
                message.as_string(),
            )
    except (smtplib.SMTPException, OSError) as exc:
        print(f"!! SMTP send failed: {exc}", file=sys.stderr)
        print(html_body)


def main() -> int:
    """Entry point. Always returns 0."""

    args = parse_args()
    day = resolve_day(args.day)
    attention = read_attention(args.branch)
    opened = read_opened_prs(args.branch, day)
    merged = read_merged_prs(args.branch, day)
    commits = read_commits(args.branch, day)

    if not attention and not opened and not merged and not commits:
        print(f"nothing landed on {args.branch} on {day.isoformat()}, nothing waiting")
        return 0

    subject = f"Daily agent digest - {day.isoformat()}"
    if attention:
        subject += f" -- {len(attention)} item(s) waiting on you"

    body = render_html(args.branch, day, attention, opened, merged, commits)
    send_or_print(body, subject, args.print_only)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        # Ctrl+C on a scheduled job should not page anyone.
        sys.exit(0)
