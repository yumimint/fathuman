#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo
from typing import Optional, Sequence

fathuman_exe: Optional[str | Path] = None
entry_rex = re.compile(
    r"^[-dv].* +(\d+) (\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}) (.*[^\/])$"
)


def fathuman(*args):
    exe = fathuman_exe or Path(__file__).with_name("fathuman")
    args = list(map(str, args))
    cp = subprocess.run(
        [exe] + list(args),
        capture_output=True,
        text=True,
        encoding="cp932",
        errors="surrogateescape",
    )
    if cp.stderr:
        print(cp.stderr, file=sys.stderr)
    if cp.returncode != 0:
        print(cp.stdout, file=sys.stderr)
        print([exe] + list(args), file=sys.stderr)
        raise RuntimeError(f"fathuman exitcode={cp.returncode}")
    return cp


def dim_files(dim: Path, rex: Optional[re.Pattern] = None):
    ps = fathuman("list", str(dim))
    if ps.stdout is None:
        return

    for line in ps.stdout.splitlines():
        line = line.rstrip()
        match = entry_rex.search(line)
        if not match:
            print(f"unmatch: '{line}'", file=sys.stderr)
            continue
        if line[0] in "dv":  # ignore directory and volume label
            continue
        size, y, m, d, hh, mm, ss = tuple(map(int, match.groups()[:7]))
        name = match.group(8)
        if rex and rex.search(name) is None:
            continue
        yield (name, size, y, m, d, hh, mm, ss)


def get_contents(dim: Path, max_workers=8, rex: Optional[re.Pattern] = None):
    with tempfile.TemporaryDirectory(dir=".") as tmpdir:

        def copyout(file: tuple):
            tmp = Path(tmpdir) / str(id(file))
            name = file[0]
            try:
                fathuman("copyout", str(dim), name, tmp)
            except Exception as e:
                print(f"{type(e).__name__}: {e}", file=sys.stderr)
                return

            with open(tmp, "rb") as f:
                content = f.read()

            return file, content

        files = dim_files(dim, rex=rex)

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            for file in executor.map(copyout, files):
                if file is None:
                    continue
                yield file


def dim2zip(
    dim: Path,
    rex: Optional[re.Pattern] = None,
    dest=Optional[Path],
):
    zip_path = dest or dim.with_suffix(".zip")
    contents = list(get_contents(dim, rex=rex))
    if contents:
        with ZipFile(str(zip_path), "w", compression=ZIP_DEFLATED) as zf:
            for file, content in contents:
                zi = ZipInfo(file[0], file[2:])
                zi.compress_type = zf.compression
                with zf.open(zi, "w") as f:
                    f.write(content)


def dim2onezip(
    dest: Path,
    dims: Sequence[Path],
    rex: Optional[re.Pattern] = None,
):
    with ZipFile(str(dest), "w", compression=ZIP_DEFLATED) as zf:
        for dim in dims:
            dir = f"{dim.stem}/"
            contents = list(get_contents(dim, rex=rex))
            for file, content in contents:
                zi = ZipInfo(dir + file[0], file[2:])
                zi.compress_type = zf.compression
                with zf.open(zi, "w") as f:
                    f.write(content)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dim", type=Path, nargs="*", help="specify disk image file")
    parser.add_argument("--exe", type=str, help="specify path to fathuman executable")
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--rex", type=str)
    parser.add_argument(
        "--onezip",
        type=Path,
        metavar="DEST",
        help="The files will be combined into a single zip file.",
    )
    args = parser.parse_args()

    global fathuman_exe
    fathuman_exe = args.exe

    rex = None
    if args.rex:
        rex = re.compile(args.rex, re.IGNORECASE)

    if args.test:
        for dim in args.dim:
            files = dim_files(dim, rex=rex)
            for file in files:
                print(file)
        return

    if args.onezip:
        dim2onezip(args.onezip, args.dim, rex=rex)
    else:
        for i, dim in enumerate(args.dim):
            # print(f"{i+1}/{len(args.dim)}: {dim}")
            dest = Path(dim.name).with_suffix(".zip")
            dim2zip(dim, rex=rex, dest=dest)


if __name__ == "__main__":
    main()
