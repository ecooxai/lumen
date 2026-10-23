import argparse
import pathlib
import tempfile


def write_atomic(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = pathlib.Path(output.name)
        output.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    sources = sorted((arguments.root / "src/js/prelude").glob("*.js"))
    if not sources:
        parser.error("No JavaScript prelude sources found")
    prelude = b"".join(source.read_bytes() for source in sources)
    # src/js/shared/NAME.js is spliced into the page prelude at its /*@shared NAME.js*/ marker and also emitted as
    # NAME_shared.inc (a raw string literal) for the worker prelude
    shared = {}
    for source in sorted((arguments.root / "src/js/shared").glob("*.js")):
        shared[source.name] = source.read_bytes()
        marker = b"/*@shared " + source.name.encode() + b"*/"
        if prelude.count(marker) != 1:
            parser.error(f"Prelude must contain exactly one {marker.decode()} marker")
        prelude = prelude.replace(marker, shared[source.name])
    if b')JS"' in prelude:
        parser.error("Prelude contains the reserved C++ raw-string delimiter")
    icon = (arguments.root / "assets/icon/lumen-256.png").read_bytes()
    rows = [", ".join(f"0x{value:02x}" for value in icon[offset:offset + 12]) + ",\n"
            for offset in range(0, len(icon), 12)]
    newtab = (arguments.root / "src/app/newtab.html").read_bytes() + b"\0"
    newtab_rows = [", ".join(f"0x{value:02x}" for value in newtab[offset:offset + 16]) + ",\n"
                   for offset in range(0, len(newtab), 16)]
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    write_atomic(arguments.output_dir / "prelude.inc", b'R"JS(' + prelude + b')JS"')
    for name, data in shared.items():
        write_atomic(arguments.output_dir / (name[:-3] + "_shared.inc"), b'R"JS(' + data + b')JS"')
    # src/js/lazy/NAME.js is compiled by a page only when first needed (N.lazySrc): emitted as NAME_lazy.inc, a raw
    # string literal split into pieces, since compilers cap the length of one literal
    for source in sorted((arguments.root / "src/js/lazy").glob("*.js")):
        data = source.read_bytes()
        if b')JS"' in data:
            parser.error(f"{source.name} contains the reserved C++ raw-string delimiter")
        pieces = [data[i:i + 60000] for i in range(0, len(data), 60000)]
        write_atomic(arguments.output_dir / (source.name[:-3] + "_lazy.inc"), b"\n".join(b'R"JS(' + p + b')JS"' for p in pieces))
    write_atomic(arguments.output_dir / "icon.inc", "".join(rows).encode("ascii"))
    write_atomic(arguments.output_dir / "newtab.inc", "".join(newtab_rows).encode("ascii"))


if __name__ == "__main__":
    main()
