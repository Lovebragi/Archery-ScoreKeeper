#!/usr/bin/env python3
"""Release helper for the archery scorekeeper.

Subcommands:
    status   Show version, HEAD, tags and firmware hash.
    check    Verify version header, changelog entry and firmware checksum.
    bump     Update the version header and prepend a changelog section.
    tag      Create an annotated v<version> tag on HEAD (optionally push).
    github   Create or update the GitHub release and upload the firmware.

The script is deliberately explicit: nothing is committed or pushed unless the
matching option is used. The GitHub token is read from the GH_TOKEN environment
variable and is never written to disk.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_HEADER = ROOT / "main" / "archery_version.h"
CHANGELOG = ROOT / "docs" / "applications" / "archery-scorekeeper-changelog.md"
FIRMWARE = ROOT / "build" / "FoloToy-AI-Passport-full.bin"
CHECKSUMS = ROOT / "firmware" / "SHA256SUMS"

VERSION_RE = re.compile(r'#define\s+ARCHERY_FIRMWARE_VERSION\s+"([^"]+)"')
CHANNEL_RE = re.compile(r'#define\s+ARCHERY_FIRMWARE_CHANNEL\s+"([^"]+)"')


def run(args, check=True):
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(args)}\n{result.stderr.strip()}")
    return result.stdout.strip()


def read_version():
    text = VERSION_HEADER.read_text(encoding="utf-8")
    version = VERSION_RE.search(text)
    channel = CHANNEL_RE.search(text)
    if not version or not channel:
        raise SystemExit(f"cannot parse version macros in {VERSION_HEADER}")
    return version.group(1), channel.group(1)


def write_version(version, channel):
    text = VERSION_HEADER.read_text(encoding="utf-8")
    text = VERSION_RE.sub(f'#define ARCHERY_FIRMWARE_VERSION "{version}"', text)
    text = CHANNEL_RE.sub(f'#define ARCHERY_FIRMWARE_CHANNEL "{channel}"', text)
    VERSION_HEADER.write_text(text, encoding="utf-8", newline="\n")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checksum_entry():
    if not CHECKSUMS.exists():
        return None
    for line in CHECKSUMS.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts:
            return parts[0].lower()
    return None


def changelog_has(version):
    text = CHANGELOG.read_text(encoding="utf-8")
    return re.search(rf"^##\s+{re.escape(version)}\b", text, re.MULTILINE) is not None


def changelog_section(version):
    text = CHANGELOG.read_text(encoding="utf-8")
    match = re.search(rf"^##\s+{re.escape(version)}\b.*$", text, re.MULTILINE)
    if not match:
        return None
    start = match.start()
    following = re.search(r"^##\s+", text[match.end():], re.MULTILINE)
    end = match.end() + following.start() if following else len(text)
    return text[start:end].strip()


def insert_changelog(version, notes):
    text = CHANGELOG.read_text(encoding="utf-8")
    if re.search(rf"^##\s+{re.escape(version)}\b", text, re.MULTILINE):
        raise SystemExit(f"changelog already has a section for {version}")
    match = re.search(r"^##\s+", text, re.MULTILINE)
    if not match:
        raise SystemExit("changelog has no existing version sections")
    today = datetime.date.today().isoformat()
    section = f"## {version} — {today}\n\n{notes.strip()}\n\n"
    CHANGELOG.write_text(text[:match.start()] + section + text[match.start():],
                         encoding="utf-8", newline="\n")


def git(*args, check=True):
    return run(["git", *args], check=check)


def remote_repo():
    """Find a GitHub remote; origin may point at a mirror such as Gitee."""
    explicit = os.environ.get("GITHUB_REPOSITORY")
    if explicit:
        return explicit
    names = git("remote").splitlines()
    ordered = ["lovebragi", "github", "origin"] + names
    seen = set()
    for name in ordered:
        if name in seen:
            continue
        seen.add(name)
        url = git("remote", "get-url", name, check=False)
        match = re.search(r"github\.com[:/](?P<repo>[^/]+/[^/.]+?)(?:\.git)?$", url)
        if match:
            return match.group("repo")
    raise SystemExit("no GitHub remote found; pass --repo owner/name")


def api(method, url, token, data=None, raw=None, content_type="application/json"):
    body = raw if raw is not None else (json.dumps(data).encode("utf-8") if data is not None else None)
    request = urllib.request.Request(url, data=body, method=method)
    request.add_header("Authorization", f"Bearer {token}")
    request.add_header("Accept", "application/vnd.github+json")
    request.add_header("User-Agent", "archery-release")
    request.add_header("Content-Type", content_type)
    proxy = os.environ.get("HTTPS_PROXY") or os.environ.get("https_proxy")
    opener = urllib.request.build_opener()
    if proxy:
        opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({"https": proxy, "http": proxy}))
    try:
        with opener.open(request, timeout=300) as response:
            payload = response.read()
            return response.status, (json.loads(payload.decode("utf-8")) if payload else {})
    except urllib.error.HTTPError as error:
        payload = error.read().decode("utf-8", "replace")
        return error.code, payload


def read_token():
    """Return the GitHub token from GH_TOKEN or the git credential store."""
    token = os.environ.get("GH_TOKEN")
    if token:
        return token.strip()
    store = Path.home() / ".config" / "git" / "github-credentials"
    if store.exists():
        match = re.search(r"x-access-token:([^@\s]+)@", store.read_text(encoding="utf-8"))
        if match:
            return match.group(1)
    return None


def release_body(version, sha):
    section = changelog_section(version) or f"## {version}"
    return (
        f"{section}\n\n"
        "### Install\n\n"
        "Write the merged image at offset 0x0:\n\n"
        "```bash\n"
        "python -m esptool --chip esp32c3 -p <PORT> -b 460800 \\\n"
        "  write_flash 0x0 FoloToy-AI-Passport-full.bin\n"
        "```\n\n"
        f"SHA-256: `{sha}`\n\n"
        "Source: https://github.com/Lovebragi/Archery-ScoreKeeper\n"
    )


def cmd_status(args):
    version, channel = read_version()
    print(f"version:  {version} ({channel})")
    print(f"branch:   {git('branch', '--show-current')}")
    print(f"head:     {git('rev-parse', '--short', 'HEAD')}")
    tag_name = f"v{version}"
    if git("tag", "--list", tag_name, check=False):
        target = git("rev-parse", "--short", tag_name)
        print(f"tag:      {tag_name} -> {target}")
    else:
        print(f"tag:      {tag_name} missing")
    if FIRMWARE.exists():
        print(f"firmware: {FIRMWARE.relative_to(ROOT)} {FIRMWARE.stat().st_size} bytes")
        print(f"sha256:   {sha256(FIRMWARE)}")
    else:
        print("firmware: missing (run ./tools/validate.sh --firmware)")
    print(f"changelog: {'present' if changelog_has(version) else 'MISSING'} for {version}")


def cmd_check(args):
    version, channel = read_version()
    problems = []
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        problems.append(f"version '{version}' is not MAJOR.MINOR.PATCH")
    if not changelog_has(version):
        problems.append(f"changelog has no '## {version}' section")
    if not FIRMWARE.exists():
        problems.append(f"missing firmware {FIRMWARE.relative_to(ROOT)}")
    else:
        actual = sha256(FIRMWARE)
        recorded = checksum_entry()
        if recorded and recorded != actual:
            problems.append(f"firmware/SHA256SUMS is {recorded}, file is {actual}")
        print(f"firmware sha256: {actual}")
    if git("status", "--porcelain", check=False):
        print("working tree: dirty (commit before tagging)")
    else:
        print("working tree: clean")
    print(f"version: {version} ({channel})")
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}")
        return 1
    print("check: OK")
    return 0


def cmd_bump(args):
    version = args.version
    notes = args.notes
    if args.notes_file:
        notes = Path(args.notes_file).read_text(encoding="utf-8")
    if not notes or not notes.strip():
        raise SystemExit("provide --notes or --notes-file")
    write_version(version, args.channel)
    insert_changelog(version, notes)
    print(f"version header -> {version} ({args.channel})")
    print(f"changelog     -> docs/applications/archery-scorekeeper-changelog.md")
    print("next: review the diff, run the validation gate, then tag.")


def cmd_tag(args):
    version, _ = read_version()
    tag = f"v{version}"
    existing = git("tag", "--list", tag, check=False)
    if existing:
        raise SystemExit(f"tag {tag} already exists")
    git("tag", "-a", tag, "-m", f"Archery scorekeeper {version}")
    print(f"created annotated tag {tag} on {git('rev-parse', '--short', 'HEAD')}")
    if args.push:
        git("push", args.remote, tag)
        print(f"pushed {tag} to {args.remote}")
    else:
        print(f"next: git push {args.remote} {tag}")


def cmd_github(args):
    token = read_token()
    if not token:
        raise SystemExit("no GitHub token: set GH_TOKEN or store one with git credential")
    version, _ = read_version()
    tag = args.tag
    repo = args.repo or remote_repo()
    if not FIRMWARE.exists():
        raise SystemExit(f"missing {FIRMWARE}; build the firmware first")
    sha = sha256(FIRMWARE)
    body = release_body(version, sha)
    if args.dry_run:
        print(f"would publish {repo} release {tag} with {FIRMWARE.name} ({sha})")
        return 0

    status, payload = api("GET", f"https://api.github.com/repos/{repo}/releases/tags/{tag}", token)
    if status == 200:
        release = payload
        status, payload = api(
            "PATCH", f"https://api.github.com/repos/{repo}/releases/{release['id']}", token,
            data={"body": body, "name": f"射箭计分器 {version}"})
        if status != 200:
            raise SystemExit(f"cannot update release: {status} {payload}")
        print(f"updated release {tag} ({release['html_url']})")
    elif status == 404:
        status, payload = api(
            "POST", f"https://api.github.com/repos/{repo}/releases", token,
            data={"tag_name": tag, "target_commitish": args.target,
                  "name": f"射箭计分器 {version}", "body": body,
                  "draft": args.draft, "prerelease": False})
        if status != 201:
            raise SystemExit(f"cannot create release: {status} {payload}")
        release = payload
        print(f"created release {tag} ({release['html_url']})")
    else:
        raise SystemExit(f"cannot query release: {status} {payload}")

    status, assets = api(
        "GET", f"https://api.github.com/repos/{repo}/releases/{release['id']}/assets", token)
    if status == 200:
        for asset in assets:
            if asset["name"] == FIRMWARE.name:
                api("DELETE",
                    f"https://api.github.com/repos/{repo}/releases/assets/{asset['id']}", token)
                print(f"removed old asset {asset['name']}")

    with open(FIRMWARE, "rb") as handle:
        firmware = handle.read()
    status, asset = api(
        "POST",
        f"https://uploads.github.com/repos/{repo}/releases/{release['id']}/assets?name={FIRMWARE.name}",
        token, raw=firmware, content_type="application/octet-stream")
    if status not in (201, 200):
        raise SystemExit(f"upload failed: {status} {asset}")
    print(f"uploaded {asset['name']} ({asset['size']} bytes)")
    if asset["size"] != len(firmware):
        raise SystemExit("uploaded size does not match the local firmware")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="show version, HEAD and firmware hash")
    sub.add_parser("check", help="verify version, changelog and firmware checksum")

    bump = sub.add_parser("bump", help="update version and changelog")
    bump.add_argument("version")
    bump.add_argument("--channel", default="正式版")
    bump.add_argument("--notes")
    bump.add_argument("--notes-file")

    tag = sub.add_parser("tag", help="create an annotated tag")
    tag.add_argument("--push", action="store_true")
    tag.add_argument("--remote", default="origin")

    github = sub.add_parser("github", help="create/update release and upload firmware")
    github.add_argument("--repo")
    github.add_argument("--tag", help="tag name; defaults to v<version>")
    github.add_argument("--target", default="main")
    github.add_argument("--draft", action="store_true")
    github.add_argument("--dry-run", action="store_true")

    args = parser.parse_args()
    if args.command == "github" and not args.tag:
        args.tag = f"v{read_version()[0]}"

    handlers = {
        "status": cmd_status,
        "check": cmd_check,
        "bump": cmd_bump,
        "tag": cmd_tag,
        "github": cmd_github,
    }
    return handlers[args.command](args) or 0


if __name__ == "__main__":
    sys.exit(main())
