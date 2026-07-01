#!/usr/bin/env python3
"""
Build E-Trike Wiring Reference PDF from hand-written LaTeX.

No pandoc needed — etrike-wiring.tex is standalone LaTeX.
Just runs pdflatex twice (for TOC).

Usage:
  python build-wiring.py                  # compile wiring PDF
  python build-wiring.py --date 2026-07-01  # override date
"""

import argparse
import os
import subprocess
import sys
from datetime import date
from pathlib import Path

# ── Paths ────────────────────────────────────────────────────────────────
REPO_ROOT = Path(__file__).resolve().parent.parent
TEX_DIR = REPO_ROOT / "tex"
WIRING_TEX = TEX_DIR / "etrike-wiring.tex"
GENERATED_DIR = TEX_DIR / "generated"
OUTPUT_DIR = TEX_DIR / "output"


# ── Helpers ──────────────────────────────────────────────────────────────

def run(cmd: list[str], cwd: Path | None = None, timeout: int = 300,
        allow_nonzero: bool = False) -> subprocess.CompletedProcess:
    """Run a command, print it, and return the result."""
    print(f"  >> {' '.join(cmd)}")
    result = subprocess.run(
        cmd, cwd=cwd or REPO_ROOT, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout,
    )
    if result.returncode != 0 and not allow_nonzero:
        print(f"  !! Command failed (exit {result.returncode})")
        stderr = result.stderr[:2000].strip()
        if stderr:
            print(f"  stderr:\n{stderr}")
        stdout = result.stdout[-3000:].strip()
        if stdout:
            print(f"  last stdout:\n{stdout}")
        # Non-fatal for pdflatex — it often exits non-zero for warnings
        # but still produces a valid PDF
    return result


def find_pdflatex() -> str:
    """Find pdflatex executable. Tries PATH first, then common MiKTeX locations."""
    # Check PATH first
    result = subprocess.run(
        ["where", "pdflatex"], capture_output=True, text=True, shell=True,
        cwd=REPO_ROOT, timeout=5,
    )
    if result.returncode == 0:
        path = result.stdout.strip().splitlines()[0]
        print(f"  Found pdflatex on PATH: {path}")
        return path

    # Search common MiKTeX install paths
    candidates = [
        Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "MiKTeX" / "miktex" / "bin" / "x64" / "pdflatex.exe",
        Path("C:/Program Files/MiKTeX/miktex/bin/x64/pdflatex.exe"),
        Path("C:/Program Files (x86)/MiKTeX/miktex/bin/x64/pdflatex.exe"),
        Path(os.environ.get("APPDATA", "")) / "MiKTeX" / "miktex" / "bin" / "x64" / "pdflatex.exe",
    ]
    for p in candidates:
        if p.exists():
            print(f"  Found pdflatex: {p}")
            return str(p)

    print("  !! pdflatex not found. Install MiKTeX: https://miktex.org/download")
    sys.exit(1)


def stamp_date(tex_path: Path, build_date: str) -> None:
    """Replace the date line in the .tex file with the build date."""
    content = tex_path.read_text(encoding="utf-8")
    # Replace date on title page and colophon
    content = content.replace("2026-07-01", build_date)
    tex_path.write_text(content, encoding="utf-8")
    print(f"  Date stamped: {build_date}")


def compile_pdf(tex_path: Path, output_dir: Path, pdflatex: str) -> Path:
    """Run pdflatex twice (for TOC) on a .tex file.

    MiKTeX returns non-zero when it hasn't checked for updates recently,
    so we verify success by checking that the PDF file exists."""
    pdf_path = output_dir / f"{tex_path.stem}.pdf"
    print(f"\n  -- Compiling {tex_path.name} --")

    for pass_num in (1, 2):
        print(f"  Pass {pass_num}/2...")
        run([pdflatex, "-interaction=nonstopmode", "-output-directory",
             str(output_dir), str(tex_path)], cwd=TEX_DIR, allow_nonzero=True)

    if not pdf_path.exists():
        print(f"  !! PDF not produced: {pdf_path.name}")
        # Check log for clues
        log_path = output_dir / f"{tex_path.stem}.log"
        if log_path.exists():
            errors = []
            for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("!"):
                    errors.append(line[:120])
            if errors:
                print(f"  LaTeX errors found ({len(errors)}):")
                for e in errors[-5:]:
                    print(f"    {e}")
        sys.exit(1)

    # Clean up aux/log files
    for pat in ["*.aux", "*.log", "*.out", "*.toc", "*.lof", "*.lot"]:
        for f in output_dir.glob(pat):
            f.unlink()

    return pdf_path


# ── Main ─────────────────────────────────────────────────────────────────

def build(args: argparse.Namespace) -> None:
    build_date = args.date or date.today().isoformat()

    print("=" * 72)
    print(f"  E-Trike Wiring Reference Builder")
    print(f"  Date: {build_date}")
    print("=" * 72)

    # Locate pdflatex
    pdflatex = find_pdflatex()

    # Ensure output directories
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Copy the source .tex to generated/ and stamp the date
    dest_tex = GENERATED_DIR / WIRING_TEX.name
    if dest_tex.exists():
        dest_tex.unlink()
    # Read, stamp, write
    content = WIRING_TEX.read_text(encoding="utf-8")
    content = content.replace("2026-07-01", build_date)
    dest_tex.write_text(content, encoding="utf-8")
    print(f"  OK Copied to {dest_tex.relative_to(REPO_ROOT)}")

    # Compile
    pdf_path = compile_pdf(dest_tex, OUTPUT_DIR, pdflatex)

    size_kb = pdf_path.stat().st_size / 1024
    print(f"\n{'=' * 72}")
    print(f"  Build complete — {build_date}")
    print(f"  PDF: {pdf_path.relative_to(REPO_ROOT)} ({size_kb:.0f} KB)")
    print(f"{'=' * 72}")


# ── CLI ──────────────────────────────────────────────────────────────────

def main() -> None:
    today = date.today().isoformat()

    parser = argparse.ArgumentParser(
        description="Build E-Trike Wiring Reference PDF from hand-written LaTeX."
    )
    parser.add_argument(
        "--date", default=today,
        help=f"Build date in YYYY-MM-DD format (default: {today})"
    )
    args = parser.parse_args()
    build(args)


if __name__ == "__main__":
    main()
