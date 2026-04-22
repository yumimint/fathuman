#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo
from typing import Optional, Sequence

fathuman_exe: Optional[str | Path] = None
entry_rex = re.compile(
    r"^([-adhlrsvwx]+) +(\d+) (\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2}):(\d{2}) (.*[^\/])$"
)


def dim_files(ps: subprocess.Popen, rex: Optional[re.Pattern] = None):
    for linebytes in iter(ps.stdout.readline, b"-eol-\n"):
        line = linebytes.decode(encoding="cp932", errors="surrogateescape")
        line = line.rstrip()

        match = entry_rex.search(line)
        if not match:
            print(f"unmatch: '{line}'", file=sys.stderr)
            continue
        if line[0] in "dv":  # ignore directory and volume label
            continue

        params = list(match.groups())
        params[1:8] = list(map(int, params[1:8]))
        # 0   1     2  3  4  5   6   7   8
        attr, size, y, m, d, hh, mm, ss, name = params
        if rex and rex.search(name) is None:
            continue
        yield (name, size, attr, y, m, d, hh, mm, ss)


def get_contents(dim: Path, rex: Optional[re.Pattern] = None):
    exe = fathuman_exe or Path(__file__).with_name("fathuman")
    ps = subprocess.Popen([exe, "interact", dim], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    files = list(dim_files(ps, rex=rex))

    for file in files:
        name, size = file[:2]
        ename = name.encode(encoding="cp932")

        ps.stdin.write(ename + b"\n")
        ps.stdin.flush()

        res = ps.stdout.readline()
        if res == b"ok\n":
            content = ps.stdout.read(size)
            yield file, content
        else:
            print("recived: ", res.rstrip(), file=sys.stderr)

    stdout, stderr = ps.communicate()


def dim2zip(
    dim: Path,
    rex: Optional[re.Pattern] = None,
    dest=Optional[Path],
):
    contents = list(get_contents(dim, rex=rex))
    if not contents:
        return
    zip_path = dest or dim.with_suffix(".zip")
    with ZipFile(str(zip_path), "w", compression=ZIP_DEFLATED) as zf:
        for file, content in contents:
            zi = ZipInfo(file[0], file[3:])
            zi.compress_type = zf.compression
            with zf.open(zi, "w") as f:
                f.write(content)


def dim2onezip(
    zip_path: Path,
    dims: Sequence[Path],
    rex: Optional[re.Pattern] = None,
):
    with ZipFile(str(zip_path), "w", compression=ZIP_DEFLATED) as zf:
        for dim in dims:
            contents = list(get_contents(dim, rex=rex))
            if not contents:
                continue
            dir = f"{dim.stem}/"
            for file, content in contents:
                zi = ZipInfo(dir + file[0], file[3:])
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
            for file, content in get_contents(dim, rex=rex):
                print(file, len(content))
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
