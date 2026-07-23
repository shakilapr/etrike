#!/usr/bin/env python3
"""
Build E-Trike Vero Board Simple GPIO Reference PDF.

Usage:
  python build-vero-board-simple.py
"""

import argparse
import os
import subprocess
import sys
from datetime import date
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TEX_DIR = REPO_ROOT / "tex"
TEX_FILE = TEX_DIR / "vero-board-simple.tex"
GENERATED_DIR = TEX_DIR / "generated"
OUTPUT_DIR = TEX_DIR / "output"


def run(cmd, cwd=None, timeout=300, allow_nonzero=False):
    print(f"  >> {' '.join(cmd)}")
    result = subprocess.run(
        cmd, cwd=cwd or REPO_ROOT, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout,
    )
    if result.returncode != 0 and not allow_nonzero:
        print(f"  !! Command failed (exit {result.returncode})")
    return result


def find_pdflatex():
    result = subprocess.run(
        ["where", "pdflatex"], capture_output=True, text=True, shell=True,
        cwd=REPO_ROOT, timeout=5,
    )
    if result.returncode == 0:
        return result.stdout.strip().splitlines()[0]
    candidates = [
        Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "MiKTeX" / "miktex" / "bin" / "x64" / "pdflatex.exe",
        Path("C:/Program Files/MiKTeX/miktex/bin/x64/pdflatex.exe"),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    print("  !! pdflatex not found")
    sys.exit(1)


def compile_pdf(tex_path, output_dir, pdflatex):
    pdf_path = output_dir / f"{tex_path.stem}.pdf"
    print(f"\n  -- Compiling {tex_path.name} --")
    run([pdflatex, "-interaction=nonstopmode", "-output-directory",
         str(output_dir), str(tex_path)], cwd=TEX_DIR, allow_nonzero=True)
    if not pdf_path.exists():
        print(f"  !! PDF not produced: {pdf_path.name}")
        sys.exit(1)
    for pat in ["*.aux", "*.log", "*.out"]:
        for f in output_dir.glob(pat):
            f.unlink()
    return pdf_path


def build(args):
    build_date = args.date or date.today().isoformat()
    print("=" * 60)
    print(f"  Vero Board Simple Reference Builder")
    print("=" * 60)
    pdflatex = find_pdflatex()
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    dest_tex = GENERATED_DIR / TEX_FILE.name
    if dest_tex.exists():
        dest_tex.unlink()
    content = TEX_FILE.read_text(encoding="utf-8")
    content = content.replace("2026-07-01", build_date)
    dest_tex.write_text(content, encoding="utf-8")
    pdf_path = compile_pdf(dest_tex, OUTPUT_DIR, pdflatex)
    size_kb = pdf_path.stat().st_size / 1024
    print(f"\n  PDF: {pdf_path.relative_to(REPO_ROOT)} ({size_kb:.0f} KB)")
    print("=" * 60)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--date", default=date.today().isoformat())
    args = parser.parse_args()
    build(args)


if __name__ == "__main__":
    main()
