#!/usr/bin/env python3
"""
Generic PSO PC binary patcher.

Common usage:

  python3 pso_pc_patcher.py pso.exe --patch patches/dragon_bml_selector_bypass.toml --verify
  python3 pso_pc_patcher.py pso.exe --patch patches/dragon_bml_selector_bypass.toml --apply
  python3 pso_pc_patcher.py pso.exe --patch patches/dragon_bml_selector_bypass.toml --revert

  python3 pso_pc_patcher.py pso.exe --all --verify
  python3 pso_pc_patcher.py pso.exe --all --apply
  python3 pso_pc_patcher.py pso.exe --all --revert

  python3 pso_pc_patcher.py pso.exe --patch patches/pso_pc_server_address_2002.toml \
    --set address=108.175.11.140 --verify
  python3 pso_pc_patcher.py pso.exe --patch patches/pso_pc_server_address_2002.toml \
    --set address=108.175.11.140 --apply

Patch definitions are stored in TOML files.

This patcher:
  - reads PE section headers
  - converts VA addresses to real file offsets
  - verifies expected bytes before patching
  - skips bytes that are already patched
  - refuses unknown/partial states by default
  - refuses overlapping patch ranges
  - creates a backup before the first write
  - supports patch-aware revert
  - supports explicit full backup restore with --restore-backup
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import os
import shutil
import struct
import sys
from dataclasses import dataclass

try:
    import tomllib  # Python 3.11+
except ModuleNotFoundError:
    print("ERROR: Python 3.11+ is required for built-in TOML support.", file=sys.stderr)
    sys.exit(1)


@dataclass
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_pointer: int
    raw_size: int


@dataclass
class Patch:
    patch_file: str
    patch_set_name: str
    label: str
    file_offset: int
    expected: bytes | None
    wanted: bytes
    va: int | None = None
    expected_any: bool = False
    mode: str = "bytes"

    @property
    def size(self) -> int:
        return len(self.wanted)


class PEImage:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.image_base: int
        self.sections: list[Section]
        self._parse()

    def _u16(self, off: int) -> int:
        return struct.unpack_from("<H", self.data, off)[0]

    def _u32(self, off: int) -> int:
        return struct.unpack_from("<I", self.data, off)[0]

    def _parse(self) -> None:
        if len(self.data) < 0x40 or self.data[0:2] != b"MZ":
            raise ValueError("Not a PE file: missing MZ header")

        pe_off = self._u32(0x3C)

        if pe_off + 0x18 >= len(self.data):
            raise ValueError("Invalid PE header offset")

        if self.data[pe_off:pe_off + 4] != b"PE\x00\x00":
            raise ValueError("Not a PE file: missing PE signature")

        coff_off = pe_off + 4
        number_of_sections = self._u16(coff_off + 2)
        optional_header_size = self._u16(coff_off + 16)

        optional_off = coff_off + 20
        magic = self._u16(optional_off)

        if magic != 0x10B:
            raise ValueError(
                f"Unsupported PE optional header magic: 0x{magic:04X}; expected PE32 0x10B"
            )

        self.image_base = self._u32(optional_off + 28)

        section_off = optional_off + optional_header_size
        self.sections = []

        for i in range(number_of_sections):
            off = section_off + (i * 40)

            if off + 40 > len(self.data):
                raise ValueError("Section table extends past end of file")

            raw_name = self.data[off:off + 8].split(b"\x00", 1)[0]
            name = raw_name.decode("ascii", errors="replace")

            virtual_size = self._u32(off + 8)
            virtual_address = self._u32(off + 12)
            raw_size = self._u32(off + 16)
            raw_pointer = self._u32(off + 20)

            self.sections.append(
                Section(
                    name=name,
                    virtual_address=virtual_address,
                    virtual_size=virtual_size,
                    raw_pointer=raw_pointer,
                    raw_size=raw_size,
                )
            )

    def va_to_file_offset(self, va: int) -> int:
        if va < self.image_base:
            raise ValueError(f"VA 0x{va:08X} is below image base 0x{self.image_base:08X}")

        rva = va - self.image_base

        for sec in self.sections:
            section_start = sec.virtual_address
            section_end = sec.virtual_address + max(sec.virtual_size, sec.raw_size)

            if section_start <= rva < section_end:
                delta = rva - sec.virtual_address

                if delta >= sec.raw_size:
                    raise ValueError(
                        f"VA 0x{va:08X} maps into section {sec.name}, "
                        f"but outside raw file data"
                    )

                return sec.raw_pointer + delta

        raise ValueError(f"VA 0x{va:08X} does not map to any PE section")


def parse_int(value: int | str) -> int:
    if isinstance(value, int):
        return value

    text = str(value).strip().lower()

    if text.startswith("0x"):
        return int(text, 16)

    return int(text, 10)


def parse_hex_bytes(text: str) -> bytes:
    cleaned = (
        str(text)
        .replace("0x", "")
        .replace("0X", "")
        .replace(",", " ")
        .replace(":", " ")
        .replace("-", " ")
        .replace("\n", " ")
        .replace("\t", " ")
    )

    parts = [p for p in cleaned.split(" ") if p]

    if not parts:
        raise ValueError("empty byte string")

    if len(parts) == 1 and len(parts[0]) > 2:
        compact = parts[0]

        if len(compact) % 2 != 0:
            raise ValueError(f"odd-length compact hex string: '{compact}'")

        parts = [compact[i:i + 2] for i in range(0, len(compact), 2)]

    out = bytearray()

    for part in parts:
        if len(part) != 2:
            raise ValueError(f"invalid byte token '{part}' in '{text}'")

        out.append(int(part, 16))

    return bytes(out)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_set_values(values: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}

    for item in values:
        if "=" not in item:
            raise ValueError(f"--set value must be KEY=VALUE, got: {item!r}")

        key, value = item.split("=", 1)
        key = key.strip()

        if not key:
            raise ValueError(f"--set key is empty in: {item!r}")

        out[key] = value

    return out


def build_fixed_ascii_z(
    *,
    path: str,
    label: str,
    input_name: str,
    input_value: str,
    input_cfg: dict,
    field_size: int,
) -> bytes:
    if field_size <= 0:
        raise ValueError(f"{path}: {label}: field_size must be positive")

    input_type = str(input_cfg.get("type", "ascii"))
    encoding = str(input_cfg.get("encoding", "ascii-nul-padded"))

    if input_type != "ascii":
        raise ValueError(f"{path}: {label}: unsupported input type for {input_name}: {input_type}")

    if encoding != "ascii-nul-padded":
        raise ValueError(f"{path}: {label}: unsupported encoding for {input_name}: {encoding}")

    try:
        raw = input_value.encode("ascii")
    except UnicodeEncodeError as e:
        raise ValueError(f"{path}: {label}: {input_name} must be ASCII") from e

    max_length = parse_int(input_cfg.get("max_length", field_size - 1))

    if len(raw) > max_length:
        raise ValueError(
            f"{path}: {label}: {input_name} is {len(raw)} bytes; "
            f"max_length is {max_length}"
        )

    if len(raw) >= field_size:
        raise ValueError(
            f"{path}: {label}: {input_name} is {len(raw)} bytes; "
            f"field_size is {field_size}, so it must be at most {field_size - 1} bytes"
        )

    return raw + (b"\x00" * (field_size - len(raw)))


def load_patch_file(path: str, pe: PEImage, input_values: dict[str, str]) -> list[Patch]:
    with open(path, "rb") as f:
        cfg = tomllib.load(f)

    patch_set_name = str(cfg.get("name", os.path.basename(path)))

    if "image_base" in cfg:
        cfg_image_base = parse_int(cfg["image_base"])

        if cfg_image_base != pe.image_base:
            raise ValueError(
                f"{path}: config image_base 0x{cfg_image_base:08X} does not match "
                f"PE image base 0x{pe.image_base:08X}"
            )

    input_cfgs_raw = cfg.get("input", {})

    if input_cfgs_raw is None:
        input_cfgs_raw = {}

    if not isinstance(input_cfgs_raw, dict):
        raise ValueError(f"{path}: [input.*] config must be a table")

    input_cfgs: dict[str, dict] = {}

    for name, raw in input_cfgs_raw.items():
        if not isinstance(raw, dict):
            raise ValueError(f"{path}: [input.{name}] must be a table")

        input_cfgs[str(name)] = raw

        if bool(raw.get("required", False)) and str(name) not in input_values:
            raise ValueError(f"{path}: missing required input: --set {name}=VALUE")

    patches_raw = cfg.get("patches")

    if not isinstance(patches_raw, list) or not patches_raw:
        raise ValueError(f"{path}: TOML must contain at least one [[patches]] entry")

    patches: list[Patch] = []

    for index, p in enumerate(patches_raw, start=1):
        label = str(p.get("label", f"patch {index}"))
        mode = str(p.get("mode", "bytes"))
        expected_any = str(p.get("expected", "")).lower() == "any"

        if mode == "fixed_ascii_z":
            if "value_from_input" not in p:
                raise ValueError(f"{path}: {label}: missing value_from_input")

            if "field_size" not in p:
                raise ValueError(f"{path}: {label}: missing field_size")

            input_name = str(p["value_from_input"])

            if input_name not in input_values:
                raise ValueError(f"{path}: {label}: missing input: --set {input_name}=VALUE")

            input_cfg = input_cfgs.get(input_name, {})
            field_size = parse_int(p["field_size"])

            wanted = build_fixed_ascii_z(
                path=path,
                label=label,
                input_name=input_name,
                input_value=input_values[input_name],
                input_cfg=input_cfg,
                field_size=field_size,
            )

            if expected_any:
                expected = None
            elif "expected" in p:
                expected = parse_hex_bytes(str(p["expected"]))

                if len(expected) != len(wanted):
                    raise ValueError(
                        f"{path}: {label}: expected length {len(expected)} != wanted length {len(wanted)}. "
                        "Same-size patches are required."
                    )
            else:
                raise ValueError(f"{path}: {label}: fixed_ascii_z requires expected='any' or expected bytes")

        elif mode == "bytes":
            if "expected" not in p or "wanted" not in p:
                raise ValueError(f"{path}: {label}: missing expected or wanted bytes")

            expected = parse_hex_bytes(str(p["expected"]))
            wanted = parse_hex_bytes(str(p["wanted"]))

            if len(expected) != len(wanted):
                raise ValueError(
                    f"{path}: {label}: expected length {len(expected)} != wanted length {len(wanted)}. "
                    "Same-size patches are required."
                )

        else:
            raise ValueError(f"{path}: {label}: unsupported patch mode: {mode}")

        va = None

        if "va" in p:
            va = parse_int(p["va"])
            file_offset = pe.va_to_file_offset(va)
        elif "file_offset" in p:
            file_offset = parse_int(p["file_offset"])
        else:
            raise ValueError(f"{path}: {label}: missing va or file_offset")

        size = len(wanted)

        if file_offset < 0 or file_offset + size > len(pe.data):
            raise ValueError(f"{path}: {label}: file offset is outside file bounds")

        patches.append(
            Patch(
                patch_file=path,
                patch_set_name=patch_set_name,
                label=label,
                file_offset=file_offset,
                expected=expected,
                wanted=wanted,
                va=va,
                expected_any=expected_any,
                mode=mode,
            )
        )

    return patches


def select_patch_files(args: argparse.Namespace) -> list[str]:
    if args.all:
        pattern = os.path.join(args.patch_dir, "*.toml")
        paths = sorted(glob.glob(pattern))

        if not paths:
            raise ValueError(f"--all selected, but no TOML files found at: {pattern}")

        return paths

    if args.patch:
        return [args.patch]

    raise ValueError("choose either --patch patches/name.toml or --all")


def check_overlaps(patches: list[Patch]) -> None:
    ranges = []

    for p in patches:
        start = p.file_offset
        end = p.file_offset + p.size
        ranges.append((start, end, p))

    ranges.sort(key=lambda x: (x[0], x[1]))

    for i in range(1, len(ranges)):
        prev_start, prev_end, prev_patch = ranges[i - 1]
        cur_start, cur_end, cur_patch = ranges[i]

        if cur_start < prev_end:
            raise ValueError(
                "Overlapping patch ranges refused:\n"
                f"  {prev_patch.patch_file}: {prev_patch.label} "
                f"[0x{prev_start:X}, 0x{prev_end:X})\n"
                f"  {cur_patch.patch_file}: {cur_patch.label} "
                f"[0x{cur_start:X}, 0x{cur_end:X})"
            )


def patch_status(data: bytes, p: Patch) -> tuple[str, bytes]:
    cur = data[p.file_offset:p.file_offset + p.size]

    if cur == p.wanted:
        return "PATCHED", cur

    if p.expected_any:
        return "DIFFERENT", cur

    assert p.expected is not None

    if cur == p.expected:
        return "ORIGINAL", cur

    return "UNKNOWN", cur


def printable_current(cur: bytes) -> str:
    stripped = cur.split(b"\x00", 1)[0]

    if stripped and all(0x20 <= b <= 0x7E for b in stripped):
        return stripped.decode("ascii", errors="replace")

    return cur.hex(" ").upper()


def verify(data: bytes, patches: list[Patch]) -> tuple[bool, bool, bool]:
    all_expected = True
    all_wanted = True
    any_unknown = False

    print(f"{'Status':<12} {'VA':<12} {'File off':<12} Patch file / label")
    print("-" * 96)

    for p in patches:
        status, cur = patch_status(data, p)

        if status == "ORIGINAL":
            all_wanted = False
        elif status == "DIFFERENT":
            all_expected = False
            all_wanted = False
        elif status == "PATCHED":
            all_expected = False
        else:
            all_expected = False
            all_wanted = False
            any_unknown = True

        va_text = f"0x{p.va:08X}" if p.va is not None else "-"
        off_text = f"0x{p.file_offset:X}"

        print(f"{status:<12} {va_text:<12} {off_text:<12} {p.patch_file} / {p.label}")

        if status == "UNKNOWN":
            expected_text = p.expected.hex(" ").upper() if p.expected is not None else "any"
            print(f"    found:    {cur.hex(' ').upper()}")
            print(f"    expected: {expected_text}")
            print(f"    wanted:   {p.wanted.hex(' ').upper()}")
        elif status == "DIFFERENT":
            print(f"    current:  {printable_current(cur)}")
            print(f"    wanted:   {printable_current(p.wanted)}")

    print()

    if all_wanted:
        print("STATE: fully patched")
    elif any_unknown:
        print("STATE: partial/unknown")
    elif all_expected:
        print("STATE: clean original")
    else:
        print("STATE: needs patch / different")

    return all_expected, all_wanted, any_unknown


def apply_patches(data: bytes, patches: list[Patch]) -> bytes:
    out = bytearray(data)

    print("Applying:")
    print("-" * 96)

    for p in patches:
        status, cur = patch_status(bytes(out), p)

        if status == "PATCHED":
            print(f"SKIP     {p.patch_file} / {p.label}")
        elif status in {"ORIGINAL", "DIFFERENT"}:
            out[p.file_offset:p.file_offset + p.size] = p.wanted
            print(f"OK       {p.patch_file} / {p.label}")
        else:
            raise ValueError(
                f"{p.patch_file}: {p.label}: unexpected bytes at 0x{p.file_offset:X}; refusing to patch"
            )

    return bytes(out)


def revert_patches(data: bytes, patches: list[Patch]) -> bytes:
    variable_patches = [p for p in patches if p.expected_any]

    if variable_patches:
        labels = ", ".join(f"{p.patch_file} / {p.label}" for p in variable_patches)
        raise ValueError(
            "Selected patch uses expected='any', so patch-aware --revert is not possible. "
            f"Use --restore-backup instead. Variable patches: {labels}"
        )

    out = bytearray(data)

    print("Reverting selected patches:")
    print("-" * 96)

    for p in patches:
        status, cur = patch_status(bytes(out), p)

        if status == "ORIGINAL":
            print(f"SKIP     {p.patch_file} / {p.label}")
        elif status == "PATCHED":
            assert p.expected is not None
            out[p.file_offset:p.file_offset + p.size] = p.expected
            print(f"OK       {p.patch_file} / {p.label}")
        else:
            raise ValueError(
                f"{p.patch_file}: {p.label}: unexpected bytes at 0x{p.file_offset:X}; refusing to revert"
            )

    return bytes(out)


def create_backup_once(exe_path: str, backup_path: str) -> None:
    if not os.path.exists(backup_path):
        shutil.copy2(exe_path, backup_path)
        print(f"Backup created: {backup_path}")
    else:
        print(f"Backup already exists: {backup_path}")

    print()


def main() -> int:
    ap = argparse.ArgumentParser(description="Generic PSO PC binary patcher")
    ap.add_argument("exe", help="Target EXE, usually pso.exe")

    selector = ap.add_mutually_exclusive_group()
    selector.add_argument("--patch", help="Patch TOML file, for example patches/name.toml")
    selector.add_argument("--all", action="store_true", help="Use every *.toml file under --patch-dir")

    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--verify", action="store_true", help="Verify selected patch state only")
    mode.add_argument("--apply", action="store_true", help="Apply selected patch or patches")
    mode.add_argument("--revert", action="store_true", help="Revert selected patch or patches by writing expected bytes")
    mode.add_argument("--restore-backup", action="store_true", help="Restore the whole EXE from backup")

    ap.add_argument("--patch-dir", default="patches", help="Patch directory used by --all; default: patches")
    ap.add_argument("--backup-suffix", default=".original", help="Backup suffix; default: .original")
    ap.add_argument(
        "--set",
        dest="set_values",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Set a patch input value for dynamic TOML patches; may be repeated",
    )

    args = ap.parse_args()

    try:
        input_values = parse_set_values(args.set_values)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    exe_path = args.exe
    backup_path = exe_path + args.backup_suffix

    try:
        if args.restore_backup:
            if args.patch or args.all:
                print("ERROR: --restore-backup does not use --patch or --all.", file=sys.stderr)
                return 2

            if not os.path.exists(backup_path):
                print(f"ERROR: backup not found: {backup_path}", file=sys.stderr)
                return 1

            shutil.copy2(backup_path, exe_path)
            print(f"Restored {exe_path} from {backup_path}")
            return 0

        if not args.patch and not args.all:
            print("ERROR: choose either --patch patches/name.toml or --all", file=sys.stderr)
            return 2

        with open(exe_path, "rb") as f:
            data = f.read()

        pe = PEImage(data)

        patch_files = select_patch_files(args)

        patches: list[Patch] = []
        for path in patch_files:
            patches.extend(load_patch_file(path, pe, input_values))

        check_overlaps(patches)

        print(f"Target:    {exe_path}")
        print(f"Size:      {len(data):,} bytes")
        print(f"SHA256:    {sha256(data)}")
        print(f"ImageBase: 0x{pe.image_base:08X}")
        print()

        if args.all:
            print(f"Patch mode: --all from {args.patch_dir}/")
        else:
            print(f"Patch mode: --patch {args.patch}")

        print(f"Patch files selected: {len(patch_files)}")
        for path in patch_files:
            print(f"  - {path}")

        print()

        all_expected, all_wanted, any_unknown = verify(data, patches)

        if args.verify:
            return 1 if any_unknown else 0

        if any_unknown:
            print("ERROR: refusing to write because at least one selected patch is UNKNOWN.", file=sys.stderr)
            return 1

        if args.apply:
            if all_wanted:
                print("Nothing to do; selected patches are already fully applied.")
                return 0

            create_backup_once(exe_path, backup_path)

            new_data = apply_patches(data, patches)

            with open(exe_path, "wb") as f:
                f.write(new_data)

            print()
            print("Done.")
            print(f"New SHA256: {sha256(new_data)}")
            return 0

        if args.revert:
            if all_expected:
                print("Nothing to do; selected patches are already reverted/original.")
                return 0

            create_backup_once(exe_path, backup_path)

            new_data = revert_patches(data, patches)

            with open(exe_path, "wb") as f:
                f.write(new_data)

            print()
            print("Done.")
            print(f"New SHA256: {sha256(new_data)}")
            return 0

        return 0

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
