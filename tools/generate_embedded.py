import argparse
import hashlib
import json
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
    suffix_path = arguments.root / "assets/data/public_suffix_list.dat"
    suffix_data = suffix_path.read_bytes()
    suffix_meta = json.loads((arguments.root / "assets/data/public_suffix_list.metadata.json").read_text())
    suffix_license = (arguments.root / "assets/data/public_suffix_list.LICENSE").read_bytes()
    if hashlib.sha256(suffix_data).hexdigest() != suffix_meta["sha256"] or hashlib.sha256(suffix_license).hexdigest() != suffix_meta["license_sha256"]:
        parser.error("Pinned Public Suffix List or license digest does not match its metadata")
    suffix_data.decode("utf-8")
    if b')PSL"' in suffix_data:
        parser.error("Public Suffix List contains the reserved raw-string delimiter")
    suffix_chunks, chunk = [], b""
    for line in suffix_data.splitlines(keepends=True):
        if len(line) > 60000:
            parser.error("Public Suffix List contains an oversized line")
        if len(chunk) + len(line) > 60000:
            suffix_chunks.append(chunk)
            chunk = b""
        chunk += line
    if chunk:
        suffix_chunks.append(chunk)
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    write_atomic(arguments.output_dir / "public_suffix_list.inc", b"\n".join(b'R"PSL(' + part + b')PSL"' for part in suffix_chunks))
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
