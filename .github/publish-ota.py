#!/usr/bin/env python3
"""
Publish OTA binaries to a GitHub Pages worktree, prune old builds, and rebuild manifests.

- Copies *.bin from an artifacts/ directory (recursively) to: site/ota/<build-id>/...
- <build-id> comes from `git describe --tags --always --dirty` (slugified, unless --allow-slashes).
- Writes per-build manifest.json (with sha256), updates latest.json, and rebuilds a global manifest.json.
- When specified, prunes builds older than --retention-days using (in order): manifest date, git commit time for the dir, or mtime.

Requirements: Python 3.8+, `git` on PATH. No third-party modules.
"""

from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from glob import glob
from pathlib import Path
from typing import List, Optional


def run(
    cmd: List[str], cwd: Optional[str] = None, check: bool = False
) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=check)


def git_rev_parse(repo_root: Path, ref: str = "HEAD") -> Optional[str]:
    try:
        cp = run(["git", "-C", str(repo_root), "rev-parse", ref], check=True)
        return cp.stdout.strip()
    except subprocess.CalledProcessError as e:
        msg = e.stderr.strip() if getattr(e, "stderr", None) else "git rev-parse failed"
        print(f"ERROR: {msg}", file=sys.stderr)
        return None


def git_describe(repo_root: Path, commit_sha: str) -> Optional[str]:
    try:
        cp = run(
            [
                "git",
                "-C",
                str(repo_root),
                "describe",
                "--tags",
                "--always",
                "--abbrev=7",
                commit_sha or "--dirty",
            ],
            check=True,
        )
        return cp.stdout.strip()
    except subprocess.CalledProcessError as e:
        msg = e.stderr.strip() if getattr(e, "stderr", None) else "git describe failed"
        print(f"ERROR: {msg}", file=sys.stderr)
        return None


def git_last_commit_ts_for_path(site_dir: Path, target: Path) -> Optional[int]:
    """Return the unix timestamp (int) of the last commit touching `target` within the site git repo."""
    try:
        cp = run(
            [
                "git",
                "-C",
                str(site_dir),
                "log",
                "-1",
                "--format=%ct",
                "--",
                str(target),
            ],
            check=True,
        )
        s = cp.stdout.strip()
        return int(s) if s else None
    except subprocess.CalledProcessError:
        return None
    except ValueError:
        return None


_slug_keep = re.compile(r"[^A-Za-z0-9._/-]+")


def slugify(s: str, allow_slashes: bool = False) -> str:
    """
    Convert to a URL/path-safe slug:
    - keep alnum, dot, underscore, dash (and optionally '/')
    - collapse other runs into '-'
    - trim leading/trailing '-'
    """
    keep = _slug_keep if allow_slashes else re.compile(r"[^A-Za-z0-9._-]+")
    out = keep.sub("-", s).strip("-")
    # Avoid path traversal just in case
    out = out.replace("..", "-")
    # Collapse multiple consecutive slashes if allowed
    if allow_slashes:
        out = re.sub(r"/{2,}", "/", out)
    return out or "untagged"


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def find_bins(artifacts_dir: Path) -> List[Path]:
    return [Path(p) for p in glob(str(artifacts_dir / "**" / "*.bin"), recursive=True)]


def now_utc_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def cutoff_ts(days: int) -> int:
    dt = datetime.now(timezone.utc) - timedelta(days=days)
    return int(dt.timestamp())


def safe_url_join(*parts: str) -> str:
    parts_clean = [p.strip("/") for p in parts if p is not None]
    if not parts_clean:
        return ""
    head = parts_clean[0]
    rest = "/".join(parts_clean[1:])
    return f"{head}/{rest}" if rest else head


def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def parse_manifest_date_iso(s: str) -> Optional[int]:
    try:
        # allow 'Z' suffix
        if s.endswith("Z"):
            s = s[:-1] + "+00:00"
        return int(datetime.fromisoformat(s).timestamp())
    except Exception:
        return None


def rebuild_global_manifest(site_dir: Path) -> None:
    manifests = [
        Path(p)
        for p in glob(str(site_dir / "ota" / "*" / "manifest.json"), recursive=True)
    ]
    entries = []
    for mf in manifests:
        try:
            with mf.open("r", encoding="utf-8") as f:
                entries.append(json.load(f))
        except Exception:
            # skip unreadable
            pass
    entries.sort(
        key=lambda e: e.get("date", ""), reverse=True
    )  # newest first by ISO date
    write_json(site_dir / "manifest.json", entries)


def prune_old(site_dir: Path, days: int) -> None:
    cutoff = cutoff_ts(days)
    ota_dir = site_dir / "ota"
    if not ota_dir.exists():
        return

    # Consider any directory that directly contains a manifest.json as a build dir
    manifest_paths = [
        Path(p) for p in glob(str(ota_dir / "**" / "manifest.json"), recursive=True)
    ]
    for mf in manifest_paths:
        build_dir = mf.parent
        ts: Optional[int] = None

        # Prefer the date inside the manifest
        try:
            with mf.open("r", encoding="utf-8") as f:
                m = json.load(f)
            ts = parse_manifest_date_iso(m.get("date", ""))
        except Exception:
            ts = None

        # Fallbacks: git timestamp of dir, then mtime
        if ts is None:
            ts = git_last_commit_ts_for_path(site_dir, build_dir)
        if ts is None:
            ts = int(build_dir.stat().st_mtime)

        if ts < cutoff:
            print(f"Pruning expired: {build_dir}")
            shutil.rmtree(build_dir, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Publish OTA binaries to GitHub Pages.")
    ap.add_argument(
        "--artifacts",
        required=True,
        type=Path,
        help="Path to downloaded artifacts (root dir).",
    )
    ap.add_argument(
        "--site", required=True, type=Path, help="Path to gh-pages working tree."
    )
    ap.add_argument(
        "--base-url",
        required=True,
        type=str,
        help="Public Pages base URL, e.g. https://user.github.io/repo",
    )
    ap.add_argument(
        "--retention-days",
        type=int,
        default=0,
        help="Keep builds modified within last N days (default 30).",
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path("."),
        help="Source repo root (to read git metadata).",
    )
    ap.add_argument(
        "--commit-sha",
        type=str,
        default=None,
        help="Commit SHA to describe (default: HEAD).",
    )
    ap.add_argument(
        "--build-id",
        type=str,
        default=None,
        help="Override build folder id (defaults to git describe).",
    )
    ap.add_argument(
        "--allow-slashes",
        action="store_true",
        help="Allow '/' in build id (creates nested dirs).",
    )

    args = ap.parse_args()

    # Resolve commit SHA
    commit_sha = args.commit_sha or git_rev_parse(args.repo_root, "HEAD")
    if not commit_sha:
        print(
            "ERROR: Could not resolve commit SHA (git rev-parse failed).",
            file=sys.stderr,
        )
        return 2

    # Resolve describe & build id
    raw_describe = args.build_id or git_describe(args.repo_root, args.commit_sha)
    build_id = slugify(raw_describe, allow_slashes=args.allow_slashes)

    now_iso = now_utc_iso()

    # Prepare destination
    dest = args.site / "ota" / build_id
    dest.mkdir(parents=True, exist_ok=True)

    # Collect .bin files
    bins = find_bins(args.artifacts)
    if not bins:
        print(f"ERROR: No .bin files found under {args.artifacts}", file=sys.stderr)
        return 1

    for b in bins:
        tgt = dest / b.name
        shutil.copy2(b, tgt)
        print(f"Copied {b} -> {tgt}")

    # Per-build manifest (with sha256)
    files = []
    for b in sorted(dest.glob("*.bin")):
        files.append(
            {
                "name": b.name,
                "url": safe_url_join(args.base_url, "ota", build_id, b.name),
                "sha256": sha256_file(b),
            }
        )

    per_build_manifest = {
        "commit": commit_sha,
        "describe": raw_describe,
        "id": build_id,
        "date": now_iso,
        "files": files,
    }
    write_json(dest / "manifest.json", per_build_manifest)

    # latest.json pointer
    write_json(args.site / "latest.json", per_build_manifest)

    # Prune old entries if needed
    if args.retention_days > 0:
        prune_old(args.site, args.retention_days)

    # Rebuild global manifest
    rebuild_global_manifest(args.site)

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
