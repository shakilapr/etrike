#!/usr/bin/env python3
"""
Build E-Trike LaTeX documentation from markdown sources.

Converts architecture.md, can-dictionary.md, docs/*.md, notes/*.md, and
standards/*.md to LaTeX via pandoc, fills the template placeholders, and
compiles PDFs with pdflatex.

Usage:
  python build-docs.py                          # build current version
  python build-docs.py --version 0.0.4-alpha    # specify version
  python build-docs.py --no-compile             # generate .tex only, skip PDF
  python build-docs.py --arch-only              # architecture PDF only (faster)
  python build-docs.py --date 2026-06-24        # override date
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import date
from pathlib import Path

# ── Paths ────────────────────────────────────────────────────────────────
REPO_ROOT = Path(__file__).resolve().parent.parent
DOC_DIR = REPO_ROOT / "tex"
TEMPLATE_DIR = DOC_DIR
GENERATED_DIR = DOC_DIR / "generated"
OUTPUT_DIR = DOC_DIR / "output"

# Source files grouped by template placeholder
ARCH_SOURCES = [
    REPO_ROOT / "architecture.md",
    REPO_ROOT / "can-dictionary.md",
]
DOC_SOURCES = sorted((REPO_ROOT / "docs").glob("*.md"))
NOTE_SOURCES = sorted((REPO_ROOT / "notes").glob("*.md"))
STD_SOURCES = sorted((REPO_ROOT / "standards").glob("*.md"))

# Template files
FULL_TEMPLATE = TEMPLATE_DIR / "etrike-template.tex"
ARCH_TEMPLATE = TEMPLATE_DIR / "etrike-architecture-template.tex"


# ── Helpers ──────────────────────────────────────────────────────────────

def run(cmd: list[str], cwd: Path | None = None, timeout: int = 120,
        allow_nonzero: bool = False) -> subprocess.CompletedProcess:
    """Run a command, print it, and return the result."""
    print(f"  >> {' '.join(cmd)}")
    result = subprocess.run(
        cmd, cwd=cwd or REPO_ROOT, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout,
    )
    if result.returncode != 0 and not allow_nonzero:
        print(f"  !! Command failed (exit {result.returncode})")
        print(f"  stderr:\n{result.stderr[:2000]}")
        sys.exit(1)
    return result


def md_to_latex(md_path: Path) -> str:
    """Convert a single markdown file to a LaTeX fragment via pandoc."""
    try:
        result = subprocess.run(
            [
                "pandoc",
                str(md_path),
                "--from", "markdown+smart+fenced_code_blocks+pipe_tables+multiline_tables+grid_tables",
                "--to", "latex",
                "--top-level-division=chapter",
                "--columns=80",
                "--no-highlight",  # we use listings package
            ],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
        )
        if result.returncode != 0:
            print(f"  !! pandoc failed for {md_path.name}: {result.stderr[:500]}")
            return f"% ERROR converting {md_path.name}\n"
        return result.stdout
    except Exception as e:
        print(f"  !! pandoc crashed for {md_path.name}: {e}")
        return f"% ERROR converting {md_path.name}: {e}\n"


def build_section(sources: list[Path], label: str) -> str:
    """Convert a list of markdown files to LaTeX and join them."""
    print(f"\n  -- Converting {label} ({len(sources)} files) --")
    parts: list[str] = []
    for src in sources:
        print(f"    {src.name}")
        tex = md_to_latex(src)
        parts.append(tex)
    return "\n\n".join(parts)


def replace_template_vars(template: str, version: str, build_date: str) -> str:
    """Replace version/date tokens in the template."""
    t = template
    t = t.replace("v0.0.2-alpha", version)
    t = t.replace("2026-06-15", build_date)
    return t


def compile_pdf(tex_path: Path, output_dir: Path) -> None:
    """Run pdflatex twice (for TOC) on a .tex file.

    MiKTeX returns non-zero when it hasn't checked for updates recently,
    so we verify success by checking that the PDF file exists."""
    pdf_path = output_dir / f"{tex_path.stem}.pdf"
    print(f"\n  -- Compiling {tex_path.name} --")

    for pass_num in (1, 2):
        print(f"  Pass {pass_num}/2...")
        run(["pdflatex", "-interaction=nonstopmode", "-output-directory",
             str(output_dir), str(tex_path)], cwd=GENERATED_DIR, allow_nonzero=True)

    if not pdf_path.exists():
        print(f"  !! PDF not produced: {pdf_path.name}")
        sys.exit(1)

    # Clean up aux/log files
    for pat in ["*.aux", "*.log", "*.out", "*.toc", "*.lof", "*.lot"]:
        for f in output_dir.glob(pat):
            f.unlink()


# ── Main build logic ─────────────────────────────────────────────────────

def build(args: argparse.Namespace) -> None:
    version = args.version
    build_date = args.date or date.today().isoformat()
    short_version = version.replace("v", "").replace("-alpha", "a").replace("-beta", "b")

    print("=" * 72)
    print(f"  E-Trike Documentation Builder")
    print(f"  Version: {version}  |  Date: {build_date}")
    print("=" * 72)

    # Clean and recreate generated directory (retry on Windows file locks)
    if GENERATED_DIR.exists():
        for attempt in range(5):
            try:
                shutil.rmtree(GENERATED_DIR)
                break
            except PermissionError:
                if attempt < 4:
                    print(f"  (retry {attempt + 1}: directory locked, waiting...)")
                    time.sleep(1)
                else:
                    print(f"  !! Cannot clean {GENERATED_DIR}, overwriting files instead")
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # ── Full system documentation ───────────────────────────────────────
    if not args.arch_only:
        print("\n=== Building Full System Documentation ===")

        # Convert all sections
        arch_tex = build_section(ARCH_SOURCES, "Architecture & CAN Dictionary")
        docs_tex = build_section(DOC_SOURCES, "Component Documentation")
        notes_tex = build_section(NOTE_SOURCES, "Theory & Concepts")
        stds_tex = build_section(STD_SOURCES, "Standards")

        # Read template and replace version/date
        template = FULL_TEMPLATE.read_text(encoding="utf-8")
        template = replace_template_vars(template, version, build_date)

        # Fill placeholders
        print("\n  -- Filling template --")
        filled = template
        filled = filled.replace("__ARCH_INPUT__", arch_tex)
        filled = filled.replace("__DOC_INPUTS__", docs_tex)
        filled = filled.replace("__NOTE_INPUTS__", notes_tex)
        filled = filled.replace("__STD_INPUTS__", stds_tex)

        full_tex_path = GENERATED_DIR / f"etrike-{short_version}.tex"
        full_tex_path.write_text(filled, encoding="utf-8")
        print(f"  OK Wrote {full_tex_path.relative_to(REPO_ROOT)}")

        if not args.no_compile:
            compile_pdf(full_tex_path, OUTPUT_DIR)
            pdf_path = OUTPUT_DIR / f"etrike-{short_version}.pdf"
            print(f"  OK PDF: {pdf_path.relative_to(REPO_ROOT)}")

    # ── Architecture-only documentation ──────────────────────────────────
    print("\n=== Building Architecture & CAN Dictionary ===")

    arch_tex = build_section(ARCH_SOURCES, "Architecture & CAN Dictionary")

    # Save the architecture LaTeX as a generated fragment for the template
    arch_fragment_path = GENERATED_DIR / "architecture.tex"
    arch_fragment_path.write_text(arch_tex, encoding="utf-8")
    print(f"  OK Wrote {arch_fragment_path.relative_to(REPO_ROOT)}")

    # Read architecture template
    arch_template = ARCH_TEMPLATE.read_text(encoding="utf-8")
    arch_template = replace_template_vars(arch_template, version, build_date)
    # Fix input path: both files live in generated/, so drop the directory prefix
    arch_template = arch_template.replace("{generated/architecture.tex}", "{architecture.tex}")

    arch_tex_path = GENERATED_DIR / f"etrike-architecture-{short_version}.tex"
    arch_tex_path.write_text(arch_template, encoding="utf-8")
    print(f"  OK Wrote {arch_tex_path.relative_to(REPO_ROOT)}")

    if not args.no_compile:
        compile_pdf(arch_tex_path, OUTPUT_DIR)
        arch_pdf_path = OUTPUT_DIR / f"etrike-architecture-{short_version}.pdf"
        print(f"  OK PDF: {arch_pdf_path.relative_to(REPO_ROOT)}")

    # ── Summary ──────────────────────────────────────────────────────────
    print(f"\n{'=' * 72}")
    print(f"  Build complete -- {version} ({build_date})")
    if not args.no_compile:
        print(f"  PDFs in: {OUTPUT_DIR.relative_to(REPO_ROOT)}/")
    print(f"{'=' * 72}")


# ── CLI ──────────────────────────────────────────────────────────────────

def main() -> None:
    today = date.today().isoformat()

    parser = argparse.ArgumentParser(
        description="Build E-Trike LaTeX documentation from markdown sources."
    )
    parser.add_argument(
        "--version", default="v0.0.6-alpha",
        help="Version string for the documentation (default: v0.0.6-alpha)"
    )
    parser.add_argument(
        "--date", default=today,
        help=f"Build date in YYYY-MM-DD format (default: {today})"
    )
    parser.add_argument(
        "--no-compile", action="store_true",
        help="Generate .tex files only, skip pdflatex compilation"
    )
    parser.add_argument(
        "--arch-only", action="store_true",
        help="Build only the Architecture & CAN Dictionary PDF"
    )
    args = parser.parse_args()
    build(args)


if __name__ == "__main__":
    main()
