"""Run the sibling CNFontNest generator before the DeskNest firmware build."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821 - PlatformIO injects the SCons environment.


def _project_dir() -> Path:
    return Path(env.subst("$PROJECT_DIR")).resolve()


def _tool_root(project_dir: Path) -> Path:
    override = os.environ.get("CNFONTNEST_ROOT")
    value = override or "../CNFontNest"
    root = Path(value)
    return (root if root.is_absolute() else project_dir / root).resolve()


def _run(project_dir: Path, tool_root: Path) -> None:
    module_root = tool_root / "src"
    config_path = project_dir / "tools" / "cnfontnest.json"
    if not module_root.is_dir():
        raise RuntimeError(
            f"CNFontNest source directory is missing: {module_root}. "
            "Set CNFONTNEST_ROOT to the CNFontNest repository root."
        )
    if not config_path.is_file():
        raise RuntimeError(f"DeskNest CNFontNest config is missing: {config_path}")

    child_env = os.environ.copy()
    existing_pythonpath = child_env.get("PYTHONPATH")
    pythonpath = [str(module_root)]
    if existing_pythonpath:
        pythonpath.append(existing_pythonpath)
    child_env["PYTHONPATH"] = os.pathsep.join(pythonpath)

    command = [
        sys.executable,
        "-m",
        "cnfontnest",
        "sync",
        "--config",
        str(config_path),
    ]
    completed = subprocess.run(
        command,
        cwd=project_dir,
        env=child_env,
        shell=False,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.stdout:
        print(completed.stdout.rstrip())
    if completed.stderr:
        sys.stderr.write(completed.stderr)
    if completed.returncode != 0:
        raise RuntimeError(
            f"CNFontNest sync failed with status {completed.returncode}; "
            f"tool_root={tool_root}"
        )
    print(f"[cnfontnest_pio] sync complete: {tool_root}")


if env.get("PIOENV") == "DeskNest":
    _run(_project_dir(), _tool_root(_project_dir()))
else:
    print(f"[cnfontnest_pio] skipped for PIOENV={env.get('PIOENV')}")
