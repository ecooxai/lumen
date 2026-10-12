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
    if b')JS"' in prelude:
        parser.error("Prelude contains the reserved C++ raw-string delimiter")
    icon = (arguments.root / "assets/icon/lumen-256.png").read_bytes()
    rows = [", ".join(f"0x{value:02x}" for value in icon[offset:offset + 12]) + ",\n"
            for offset in range(0, len(icon), 12)]
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    write_atomic(arguments.output_dir / "prelude.inc", b'R"JS(' + prelude + b')JS"')
    write_atomic(arguments.output_dir / "icon.inc", "".join(rows).encode("ascii"))


if __name__ == "__main__":
    main()
