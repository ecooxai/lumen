import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("verify_apk", ROOT / "tools/android/verify_apk.py")
verify_apk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify_apk)


def elf_report(machine=183, alignment=16384, offset=0, address=0):
    return [{"ElfHeader": {"Type": "SharedObject (0x3)",
                           "Machine": {"Value": machine},
                           "Ident": {"Class": {"Value": 2}, "DataEncoding": {"Value": 1}}},
             "ProgramHeaders": [{"ProgramHeader": {
                 "Type": {"Value": 1}, "Alignment": alignment,
                 "Offset": offset, "VirtualAddress": address}}]}]


class AndroidApkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.apk = self.root / "codex-test.apk"
        self.system = {"libc.so": "21", "libandroid.so": "21", "libamidi.so": "29"}

    def bundle(self, libraries=None):
        libraries = libraries or ["libmain.so", "libSDL3.so", "libwgpu_native.so"]
        with zipfile.ZipFile(self.apk, "w") as archive:
            for library in libraries:
                archive.writestr(f"lib/arm64-v8a/{library}", b"test-only native placeholder")

    def verify(self, dependencies=None, **kwargs):
        with patch.object(verify_apk, "inspect_library", return_value=dependencies or ["libc.so"]):
            return verify_apk.verify_apk(self.apk, Path("readelf"), self.system,
                                         expected_abis={"arm64-v8a"}, **kwargs)

    def test_complete_dependency_closure(self):
        self.bundle(["libmain.so", "libSDL3.so", "libwgpu_native.so", "libc++_shared.so"])
        result = self.verify(["libc.so", "libc++_shared.so"])
        self.assertEqual(len(result["arm64-v8a"]), 4)

    def test_missing_required_library(self):
        self.bundle(["libmain.so", "libSDL3.so"])
        with self.assertRaisesRegex(ValueError, "libwgpu_native"):
            self.verify()

    def test_missing_transitive_runtime(self):
        self.bundle()
        with self.assertRaisesRegex(ValueError, "libc\\+\\+_shared"):
            self.verify(["libc++_shared.so"])

    def test_newer_api_dependency_rejected(self):
        self.bundle()
        with self.assertRaisesRegex(ValueError, "libamidi"):
            self.verify(["libamidi.so"])

    def test_wrong_or_missing_abi_rejected(self):
        self.bundle()
        with patch.object(verify_apk, "inspect_library", return_value=[]):
            with self.assertRaisesRegex(ValueError, "ABI"):
                verify_apk.verify_apk(self.apk, Path("readelf"), self.system,
                                      expected_abis={"arm64-v8a", "x86_64"})

    def test_unsafe_member_and_system_override_rejected(self):
        for name in ("../libescape.so", "libc.so"):
            with self.subTest(name=name):
                self.bundle(["libmain.so", "libSDL3.so", "libwgpu_native.so", name])
                with self.assertRaises(ValueError):
                    self.verify()

    def test_duplicate_member_rejected(self):
        self.bundle()
        with zipfile.ZipFile(self.apk, "a") as archive:
            with self.assertWarns(UserWarning):
                archive.writestr("lib/arm64-v8a/libmain.so", b"duplicate")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.verify()

    def test_size_limit_rejected_before_inspection(self):
        self.bundle()
        with patch.object(verify_apk, "MAX_LIBRARY_BYTES", 1):
            with self.assertRaisesRegex(ValueError, "size"):
                self.verify()

    def inspect(self, report=None, dynamic=None, abi="arm64-v8a"):
        if dynamic is None:
            dynamic = "0x1 NEEDED Shared library: [libc.so]\n0xe SONAME Library soname: [libmain.so]\n"
        with patch.object(verify_apk, "read_tool", side_effect=[json.dumps(report or elf_report()), dynamic]):
            return verify_apk.inspect_library(self.root / "libmain.so", Path("readelf"), abi)

    def test_real_readelf_report_shape(self):
        self.assertEqual(self.inspect(), ["libc.so"])
        self.assertEqual(self.inspect(elf_report(machine=62), abi="x86_64"), ["libc.so"])

    def test_gnu_dynamic_tag_parentheses(self):
        dynamic = ("0x0000000000000001 (NEEDED) Shared library: [libandroid.so]\n"
                   "0x0000000000000001 (NEEDED) Shared library: [libc.so]\n"
                   "0x000000000000000e (SONAME) Library soname: [libmain.so]\n")
        self.assertEqual(self.inspect(dynamic=dynamic), ["libandroid.so", "libc.so"])

    def test_machine_and_load_alignment(self):
        for report in (elf_report(machine=62), elf_report(alignment=4096),
                       elf_report(offset=1), elf_report(alignment=24576)):
            with self.subTest(report=report):
                with self.assertRaises(ValueError):
                    self.inspect(report)

    def test_missing_load_segment_rejected(self):
        report = elf_report()
        report[0]["ProgramHeaders"] = []
        with self.assertRaisesRegex(ValueError, "LOAD"):
            self.inspect(report)

    def test_executable_or_wrong_elf_encoding_rejected(self):
        report = elf_report()
        report[0]["ElfHeader"]["Type"] = "Executable (0x2)"
        with self.assertRaises(ValueError):
            self.inspect(report)
        for field in ("Class", "DataEncoding"):
            report = elf_report()
            report[0]["ElfHeader"]["Ident"][field]["Value"] = 0
            with self.assertRaises(ValueError):
                self.inspect(report)

    def test_missing_soname_rejected(self):
        with self.assertRaisesRegex(ValueError, "SONAME"):
            self.inspect(dynamic="0x1 NEEDED Shared library: [libc.so]\n")

    def test_host_search_path_and_wrong_soname_rejected(self):
        for dynamic in ("0x1d RUNPATH Library runpath: [/home/build]\n",
                        "0xf RPATH Library rpath: [/usr/lib]\n",
                        "0xe SONAME Library soname: [libwrong.so]\n",
                        "0x1 NEEDED Shared library: [/host/libc.so]\n",
                        "0x16 TEXTREL 0\n"):
            with self.subTest(dynamic=dynamic):
                with self.assertRaises(ValueError):
                    self.inspect(dynamic=dynamic)


if __name__ == "__main__":
    unittest.main()
