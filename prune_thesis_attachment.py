#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import stat
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Set, Tuple


# -------------------------
# ANSI colors
# -------------------------
ANSI_RED = "\033[31m"
ANSI_YELLOW = "\033[33m"
ANSI_GREEN = "\033[32m"
ANSI_CYAN = "\033[36m"
ANSI_RESET = "\033[0m"


def colorize(s: str, c: str, enable: bool) -> str:
    if not enable:
        return s
    return f"{c}{s}{ANSI_RESET}"


def red(s: str, enable: bool) -> str:
    return colorize(s, ANSI_RED, enable)


def yellow(s: str, enable: bool) -> str:
    return colorize(s, ANSI_YELLOW, enable)


def green(s: str, enable: bool) -> str:
    return colorize(s, ANSI_GREEN, enable)


def cyan(s: str, enable: bool) -> str:
    return colorize(s, ANSI_CYAN, enable)


# -------------------------
# Config / expectations
# -------------------------
@dataclass(frozen=True)
class DeleteTarget:
    rel_path: str
    required: bool  # missing => RED if True, else YELLOW
    note: str = ""  # optional additional note


SCRIPT_NAME = Path(__file__).name
ROOT = Path(__file__).resolve().parent

# What must remain in the pruned attachment root (besides this prune script itself).
EXPECTED_ROOT: Set[str] = {
    ".vscode",
    "CMakeLists.txt",
    "CMakePresets.json",
    "backend",
    "default_modules",
    "manifests",
    "vcpkg-triplets",
}

# Root-level items that are explicitly pruned away (and therefore ignored during verification if present pre-apply).
ROOT_PRUNE_TARGETS: Set[str] = {
    ".git",
    ".gitignore",
    "default_modules_retired",
    "demos",
    "parts",
    "project_structure.txt",
    "sources",
}

# Exact helper folders expected inside backend/module_helpers
EXPECTED_MODULE_HELPERS: Set[str] = {
    "activation_wrapper",
    "async_helpers",
    "base_64",
    "base_usecase",
    "calibrated_camera_world_messages",
    "camera_messages",
    "camera_pose_helper",
    "mixed_buffer_allocator",
    "parameter_description",
    "pen_messages",
    "pose_utils",
    "robot_interface",
    "robot_wrapper",
    "scene_detection_helper",
    "serialization_helper",
    "synchronous_request_helper",
    "usecase_tree",
    "usecase_wrapper",
    "visualization_3d_interface",
}

# Exact non-demo modules expected under default_modules (demo_* are ignored)
EXPECTED_DEFAULT_MODULES_NON_DEMO: Set[str] = {
    "camera_pose_injector_module",
    "frontend_module",
    "pen_tracking_multicam_module",
    "robot_module_kassow",
    "robot_stereo_camera_calibration_module",
    "scene_detection_stereocam_module",
    "stereo_camera_module_windows",
    "usecase_move_arc",
    "usecase_move_joint",
    "usecase_move_linear",
    "usecase_move_trajectory",
    "usecase_pick_and_place",
    "usecase_weld",
}

# Exact manifests structure
EXPECTED_MANIFEST_DIRS: Set[str] = {"core", "demos"}
EXPECTED_MANIFEST_FILES: Set[str] = {"core/vcpkg.json", "demos/vcpkg.json"}

# Exact vcpkg-triplets structure
EXPECTED_TRIPLET_FILES: Set[str] = {
    "x64-linux-dynamic.cmake",
    "x64-linux-pic-static.cmake",
    "x64-windows-dynamic.cmake",
    "x64-windows-static.cmake",
}

DELETE_TARGETS = [
    DeleteTarget(".git", required=True),
    DeleteTarget(".gitignore", required=True),
    DeleteTarget("default_modules_retired", required=True),
    DeleteTarget("demos", required=True),
    DeleteTarget("parts", required=True),
    DeleteTarget("project_structure.txt", required=True),
    DeleteTarget("sources", required=True),
    # Special-case: allowed to be missing (warn yellow), but pruned if present.
    DeleteTarget(
        "default_modules/robot_stereo_camera_calibration_module/tests",
        required=False,
        note="Prune \"calibration tests data\" for the attachment; ensure CMakeLists guards add_subdirectory(tests).",
    ),
]


# -------------------------
# Helpers
# -------------------------
def is_within_root(p: Path, root: Path) -> bool:
    try:
        p.resolve().relative_to(root.resolve())
        return True
    except Exception:
        return False


def chmod_writeable(path: str) -> None:
    # Best-effort for Windows read-only files (or chmod-protected files)
    try:
        os.chmod(path, stat.S_IWRITE)
    except Exception:
        pass


def rmtree_onerror(func, path, exc_info) -> None:
    chmod_writeable(path)
    try:
        func(path)
    except Exception:
        pass


def delete_path(abs_path: Path, apply: bool) -> Tuple[bool, str]:
    """
    Returns: (did_delete_or_would_delete, human_action_string)
    """
    if not abs_path.exists() and not abs_path.is_symlink():
        return (False, "missing")

    if not apply:
        return (True, "would-delete")

    # Safety: never delete outside ROOT
    if not is_within_root(abs_path, ROOT):
        raise RuntimeError(f"Refusing to delete outside root: {abs_path}")

    # If it's a symlink, unlink only (never follow).
    if abs_path.is_symlink() or abs_path.is_file():
        abs_path.unlink()
        return (True, "deleted")

    if abs_path.is_dir():
        shutil.rmtree(abs_path, onerror=rmtree_onerror)
        return (True, "deleted")

    # Fallback: try unlink
    abs_path.unlink(missing_ok=True)
    return (True, "deleted")


def list_dir_names(path: Path) -> Tuple[Set[str], Set[str]]:
    """
    Returns: (dirs, files) as name sets (direct children only).
    """
    dirs: Set[str] = set()
    files: Set[str] = set()
    for child in path.iterdir():
        name = child.name
        if child.is_dir():
            dirs.add(name)
        else:
            files.add(name)
    return dirs, files


def snapshot_tree_relative(root_dir: Path) -> Tuple[Set[str], Set[str]]:
    """
    Recursive snapshot: returns (dirs, files) as POSIX relative paths,
    excluding the root itself.
    """
    dirs: Set[str] = set()
    files: Set[str] = set()
    for p in root_dir.rglob("*"):
        rel = p.relative_to(root_dir).as_posix()
        if p.is_dir():
            dirs.add(rel)
        else:
            files.add(rel)
    return dirs, files


# -------------------------
# Main logic
# -------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description="Prune a diploma-thesis code tree for attachment + verify expected structure."
    )
    ap.add_argument(
        "--apply",
        action="store_true",
        help="Actually delete files/folders. Without this, runs as a dry-run (reports what it would delete).",
    )
    ap.add_argument(
        "--no-color",
        action="store_true",
        help="Disable ANSI colors.",
    )
    args = ap.parse_args()

    use_color = not args.no_color and sys.stdout.isatty()
    apply = bool(args.apply)

    errors = 0
    warnings = 0

    def err(msg: str) -> None:
        nonlocal errors
        errors += 1
        print(red(f"[ERROR] {msg}", use_color))

    def warn(msg: str) -> None:
        nonlocal warnings
        warnings += 1
        print(yellow(f"[WARN ] {msg}", use_color))

    def ok(msg: str) -> None:
        print(green(f"[ OK  ] {msg}", use_color))

    def info(msg: str) -> None:
        print(cyan(f"[INFO ] {msg}", use_color))

    # Guard: look like the expected repo root
    must_exist = ["CMakeLists.txt", "CMakePresets.json", "backend", "default_modules", "manifests", "vcpkg-triplets"]
    missing_guard = [x for x in must_exist if not (ROOT / x).exists()]
    if missing_guard:
        err(
            "This does not look like the expected repo root. Missing: "
            + ", ".join(missing_guard)
            + f" (script dir: {ROOT})"
        )
        return 2

    # Always note the script existence (user should delete afterwards for final attachment).
    warn(f"This prune script exists in the tree: {SCRIPT_NAME} (delete it manually before final packaging).")

    # Deletions
    info(("APPLY MODE: deleting targets" if apply else "DRY-RUN MODE: nothing will be deleted (use --apply to delete)"))
    for t in DELETE_TARGETS:
        p = (ROOT / t.rel_path).resolve() if (ROOT / t.rel_path).exists() else (ROOT / t.rel_path)
        try:
            did, action = delete_path(p, apply=apply)
        except Exception as e:
            err(f"Failed to delete {t.rel_path}: {e}")
            continue

        if action == "missing":
            if t.required:
                err(f"Delete-target missing: {t.rel_path}")
            else:
                warn(f"Optional delete-target missing (OK): {t.rel_path}")
        else:
            if apply:
                ok(f"Deleted: {t.rel_path}")
            else:
                info(f"Would delete: {t.rel_path}")

        if t.note:
            warn(t.note)

    # Always remind about CMakeLists updates
    warn(
        "Reminder: update corresponding CMakeLists.txt to not reference pruned content "
        "(e.g., add_subdirectory(demos), default_modules_retired, parts, sources, and the pruned tests folder)."
    )

    # -------------------------
    # Verification (post-prune expectations)
    # -------------------------
    info("Verifying expected structure (at the explicitly requested levels)...")

    # Root verification
    root_dirs, root_files = list_dir_names(ROOT)
    root_all = root_dirs | root_files

    root_ignored = set(ROOT_PRUNE_TARGETS) | {SCRIPT_NAME}
    # In dry-run, deletion targets may still exist: ignore them as well during "unexpected extras"
    root_ignored |= set(ROOT_PRUNE_TARGETS)

    # Missing expected
    for name in sorted(EXPECTED_ROOT):
        if name not in root_all:
            err(f"Root missing expected entry: {name}")

    # Unexpected extras (excluding ignored + expected)
    for name in sorted(root_all - EXPECTED_ROOT - root_ignored):
        err(f"Root has unexpected extra entry: {name}")

    # backend/module_common + backend/module_helpers existence
    if not (ROOT / "backend" / "module_common").is_dir():
        err("Missing required folder: backend/module_common")
    if not (ROOT / "backend" / "module_helpers").is_dir():
        err("Missing required folder: backend/module_helpers")

    # backend/module_helpers exact helper folder set
    mh_root = ROOT / "backend" / "module_helpers"
    mh_dirs, mh_files = list_dir_names(mh_root)

    # Allow exactly CMakeLists.txt as a file at this level; everything else is unexpected
    allowed_mh_files = {"CMakeLists.txt"}
    for f in sorted(mh_files - allowed_mh_files):
        err(f"backend/module_helpers has unexpected file: {f}")
    for f in sorted(allowed_mh_files):
        if f not in mh_files:
            err(f"backend/module_helpers missing expected file: {f}")

    # Compare helper dirs
    missing_helpers = EXPECTED_MODULE_HELPERS - mh_dirs
    extra_helpers = mh_dirs - EXPECTED_MODULE_HELPERS
    for d in sorted(missing_helpers):
        err(f"backend/module_helpers missing expected helper folder: {d}")
    for d in sorted(extra_helpers):
        err(f"backend/module_helpers has unexpected extra helper folder: {d}")

    # default_modules expected set (ignoring demo_*)
    dm_root = ROOT / "default_modules"
    dm_dirs, dm_files = list_dir_names(dm_root)

    allowed_dm_files = {"CMakeLists.txt"}
    for f in sorted(dm_files - allowed_dm_files):
        err(f"default_modules has unexpected file: {f}")
    for f in sorted(allowed_dm_files):
        if f not in dm_files:
            err(f"default_modules missing expected file: {f}")

    missing_dm = EXPECTED_DEFAULT_MODULES_NON_DEMO - dm_dirs
    extra_dm = dm_dirs - EXPECTED_DEFAULT_MODULES_NON_DEMO

    for d in sorted(missing_dm):
        err(f"default_modules missing expected module folder: {d}")
    for d in sorted(extra_dm):
        err(f"default_modules has unexpected extra module folder: {d}")

    # manifests: remember entire structure (exact)
    man_root = ROOT / "manifests"
    man_dirs, man_files = snapshot_tree_relative(man_root)

    # We only expect these dirs exactly (plus their parents implicitly via path):
    # dirs snapshot contains 'core' and 'demos' (no nested dirs expected)
    actual_top_dirs, actual_top_files = list_dir_names(man_root)
    # top-level must contain core + demos only (no other dirs/files)
    for d in sorted(EXPECTED_MANIFEST_DIRS - actual_top_dirs):
        err(f"manifests missing expected folder: {d}")
    for d in sorted(actual_top_dirs - EXPECTED_MANIFEST_DIRS):
        err(f"manifests has unexpected extra folder: {d}")
    for f in sorted(actual_top_files):
        err(f"manifests has unexpected top-level file: {f}")

    # check exact file set anywhere under manifests
    # (also ensures vcpkg.json exist and no extras)
    expected_manifest_files = set(EXPECTED_MANIFEST_FILES)
    for f in sorted(expected_manifest_files - man_files):
        err(f"manifests missing expected file: {f}")
    for f in sorted(man_files - expected_manifest_files):
        err(f"manifests has unexpected extra file: {f}")
    # check there are no nested directories beyond core/ and demos/
    # (man_dirs includes nested dirs if any)
    expected_manifest_dirs_all = set(EXPECTED_MANIFEST_DIRS)
    for d in sorted(man_dirs - expected_manifest_dirs_all):
        err(f"manifests has unexpected extra directory: {d}")

    # vcpkg-triplets: remember entire structure (exact)
    vt_root = ROOT / "vcpkg-triplets"
    vt_dirs, vt_files = snapshot_tree_relative(vt_root)

    # no directories expected under vcpkg-triplets
    for d in sorted(vt_dirs):
        err(f"vcpkg-triplets has unexpected directory: {d}")

    for f in sorted(EXPECTED_TRIPLET_FILES - vt_files):
        err(f"vcpkg-triplets missing expected file: {f}")
    for f in sorted(vt_files - EXPECTED_TRIPLET_FILES):
        err(f"vcpkg-triplets has unexpected extra file: {f}")

    # Special yellow note requested (always)
    warn(
        "Note: default_modules/robot_stereo_camera_calibration_module/tests is pruned for the attachment. "
        "Ensure its CMakeLists.txt handles missing tests/ gracefully."
    )

    # Summary
    print()
    if errors == 0:
        ok(f"Done. Errors: {errors}, Warnings: {warnings}")
        return 0
    else:
        err(f"Done with problems. Errors: {errors}, Warnings: {warnings}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())