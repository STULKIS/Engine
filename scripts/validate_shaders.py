#!/usr/bin/env python3
"""Validate Frontier Engine shaders with glslangValidator.

The engine's shader loader supports `#include "path"` relative to the including
file; glslang does not, so this script performs the same include expansion into
a temporary directory and then validates every stage with glslangValidator.

Usage:
    python3 scripts/validate_shaders.py [--glslang path/to/glslangValidator]

Exit code 0 means every shader compiled without errors.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

STAGES = {".vert": "vert", ".frag": "frag", ".geom": "geom", ".comp": "comp"}
SHADER_DIR = pathlib.Path(__file__).resolve().parent.parent / "assets" / "shaders"
MAX_INCLUDE_DEPTH = 16


class IncludeError(RuntimeError):
    pass


def expand(path: pathlib.Path, seen: set[pathlib.Path], depth: int = 0) -> str:
    if depth > MAX_INCLUDE_DEPTH:
        raise IncludeError(f"include depth exceeded while reading {path}")
    if path in seen:
        return ""
    seen.add(path)

    if not path.is_file():
        raise IncludeError(f"missing shader file: {path}")

    lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("#include"):
            start = line.find('"')
            end = line.find('"', start + 1) if start >= 0 else -1
            if start < 0 or end < 0:
                raise IncludeError(f"malformed include in {path}: {line}")
            child = path.parent / line[start + 1 : end]
            lines.append(f"// ---- {child.name} ----")
            lines.append(expand(child, seen, depth + 1))
        else:
            lines.append(line)
    return "\n".join(lines) + "\n"


def find_glslang(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in ("glslangValidator", "glslang"):
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit("glslangValidator not found; pass --glslang /path/to/glslangValidator")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslang", default=None)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    glslang = find_glslang(args.glslang)
    shaders = sorted(p for p in SHADER_DIR.rglob("*") if p.suffix in STAGES)
    if not shaders:
        raise SystemExit(f"no shaders found under {SHADER_DIR}")

    program_pairs = [
        ("sky.vert", "sky.frag"),
        ("terrain.vert", "terrain.frag"),
        ("foliage.vert", "foliage.frag"),
        ("water.vert", "water.frag"),
        ("shadow.vert", "shadow.frag"),
        ("shadow_instanced.vert", "shadow.frag"),
        ("fullscreen.vert", "bloom_threshold.frag"),
        ("fullscreen.vert", "bloom_blur.frag"),
        ("fullscreen.vert", "post.frag"),
    ]

    failures = 0
    with tempfile.TemporaryDirectory(prefix="frontier-shaders-") as tmp:
        tmpdir = pathlib.Path(tmp)
        for shader in shaders:
            stage = STAGES[shader.suffix]
            try:
                source = expand(shader, set())
            except IncludeError as exc:
                print(f"[FAIL] {shader.relative_to(SHADER_DIR)}: {exc}")
                failures += 1
                continue
            output = tmpdir / shader.name
            output.write_text(source, encoding="utf-8")

            result = subprocess.run(
                [glslang, "-S", stage, str(output)],
                capture_output=True,
                text=True,
            )
            rel = shader.relative_to(SHADER_DIR)
            if result.returncode == 0:
                print(f"[ OK ] {rel}")
                if args.verbose and result.stdout.strip():
                    print(result.stdout.rstrip())
            else:
                failures += 1
                print(f"[FAIL] {rel}")
                print(result.stdout.rstrip() or result.stderr.rstrip())

    # Verify stage interfaces and uniforms link into complete programs.
    link_failures = 0
    with tempfile.TemporaryDirectory(prefix="frontier-programs-") as tmp:
        tmpdir = pathlib.Path(tmp)
        for vertex, fragment in program_pairs:
            try:
                stage_sources = {}
                for name in (vertex, fragment):
                    stage_sources[name] = expand(SHADER_DIR / name, set())
            except IncludeError as exc:
                print(f"[FAIL] link {vertex} + {fragment}: {exc}")
                link_failures += 1
                continue
            outputs = []
            for name, source in stage_sources.items():
                output = tmpdir / name
                output.write_text(source, encoding="utf-8")
                outputs.append(str(output))
            result = subprocess.run([glslang, "-l", *outputs], capture_output=True, text=True)
            if result.returncode == 0:
                print(f"[ OK ] link {vertex} + {fragment}")
            else:
                print(f"[FAIL] link {vertex} + {fragment}")
                print((result.stdout + result.stderr).strip())
                link_failures += 1

    print()
    print(f"{len(shaders) - failures}/{len(shaders)} shader stages passed glslang validation.")
    print(f"{len(program_pairs) - link_failures}/{len(program_pairs)} programs passed stage linking.")
    return 1 if (failures or link_failures) else 0


if __name__ == "__main__":
    sys.exit(main())
