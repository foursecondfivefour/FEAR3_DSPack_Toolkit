#!/usr/bin/env python3
"""Read-only index parser and extractor for Despair .dsPack v107/v108."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

MAGIC = b"mgf "
SUPPORTED_VERSIONS = {0x5A5A0107, 0x5A5A0108}
FLAG_INTERNAL_COMPRESSION = 0x02
FLAG_ZLIB = 0x08
FLAG_LZX = 0x10
FLAG_LZD = 0x20
FLAG_MINIPACK = 0x80000000
CODEC_ZLIB = 0xC5735A0F
CODEC_LZX = 0xC5735A10
CODEC_LZD = 0xC5735A11
LZX_FRAME_SIZE = 0x8000
LZX_WINDOW_BITS = 20
FILE_RECORD = struct.Struct("<6I")
DIR_RECORD = struct.Struct("<6I")


class PackError(Exception):
    pass


@dataclass(frozen=True)
class Header:
    version: int
    file_count: int
    file_offset: int
    dir_count: int
    dir_offset: int
    string_size: int
    string_offset: int
    flags: int
    block_size: int
    aux_offset: int


@dataclass(frozen=True)
class FileEntry:
    name_offset: int
    parent: int
    logical_size: int
    stored_size: int
    timestamp: int
    data_offset: int


@dataclass(frozen=True)
class DirEntry:
    name_offset: int
    parent: int
    first_child: int
    next_sibling: int
    first_file: int
    end_file: int


def read_exact_at(stream, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0:
        raise PackError(f"negative range: offset={offset}, size={size}")
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise PackError(f"short read at 0x{offset:X}: wanted {size}, got {len(data)}")
    return data


class LzdReader:
    """Read MSB-first 32-bit words as consumed by the engine's LZD bitstream."""

    def __init__(self, data: bytes):
        self.words = [int.from_bytes(data[i:i + 4], "big") for i in range(0, len(data) - 3, 4)]
        self.word_pos = 0
        self.bits = 0
        self.available = 0
        self._refill()
        self._refill()

    def _refill(self):
        if self.word_pos < len(self.words):
            self.bits |= self.words[self.word_pos] << self.available
            self.available += 32
            self.word_pos += 1

    def read(self, count: int) -> int:
        if count < 0 or count > 32:
            raise PackError(f"invalid LZD bit count {count}")
        if self.available <= 32 and self.word_pos < len(self.words):
            self._refill()
        if self.available < count:
            raise PackError("truncated LZD bitstream")
        value = self.bits & ((1 << count) - 1) if count else 0
        self.bits >>= count
        self.available -= count
        return value

    def prepare_lookup(self):
        if self.available <= 32 and self.word_pos < len(self.words):
            self._refill()


def _build_lzd_tables(data: bytes):
    if not data:
        raise PackError("empty LZD payload")
    count = data[0]
    start = 2 * count + 2
    if start > len(data):
        raise PackError("truncated LZD prefix tree")

    decode = [(0, 0)] * 1024
    nodes = [[0, 0] for _ in range(512)]
    active = 0

    def parse(tree_pos: int, symbol_pos: int, node: int, previous: int, depth: int, code: int):
        nonlocal active
        active += 1
        if active > 4096 or depth > 512 or node < 0 or node >= len(nodes):
            raise PackError("invalid LZD prefix tree")
        if tree_pos >= len(data) or symbol_pos >= len(data):
            raise PackError("truncated LZD prefix tree data")

        marker = data[tree_pos]
        if marker:
            nodes[node][0] = node - 1
            next_symbol = parse(tree_pos + 1, symbol_pos, node - 1, marker, depth + 1, code)
        else:
            leaf = data[symbol_pos]
            nodes[node][0] = leaf
            next_symbol = symbol_pos + 1
            if depth < 10 and code < 1024:
                step = 1 << (depth + 1)
                for slot in range(code, 1024, step):
                    decode[slot] = (leaf, depth + 1)

        code = (1 << depth) | code
        if ((previous - marker) & 0xFFFFFFFF) <= 1:
            if next_symbol >= len(data):
                raise PackError("truncated LZD leaf table")
            leaf = data[next_symbol]
            nodes[node][1] = leaf
            next_symbol += 1
            if depth < 10 and code < 1024:
                step = 1 << (depth + 1)
                for slot in range(code, 1024, step):
                    decode[slot] = (leaf, depth + 1)
        else:
            child = node - marker - 1
            if child < 0:
                raise PackError("invalid LZD child node")
            nodes[node][1] = child
            next_symbol = parse(tree_pos + marker + 1, next_symbol, child,
                                previous - marker - 1, depth + 1, code)
        return next_symbol

    root = count + 255
    parse(count + 2, 1, root, count, 0, 0)
    for slot in range(1024):
        node = root
        bit_count = 0
        while node > 0xFF and bit_count < 10:
            if node >= len(nodes):
                raise PackError("invalid LZD fast-decode node")
            node = nodes[node][(slot >> bit_count) & 1]
            bit_count += 1
        decode[slot] = (node, bit_count)
    return decode, nodes, start


def lzd_decompress(data: bytes, expected_size: int) -> bytes:
    """Decode one internal LZD block using the routine reconstructed from IDA."""
    if len(data) == expected_size:
        return data
    if expected_size < 0 or expected_size > 512 * 1024 * 1024:
        raise PackError(f"unreasonable LZD output size {expected_size}")

    decode, nodes, bit_start = _build_lzd_tables(data)
    reader = LzdReader(data[bit_start:])
    command_count = reader.read(32)
    output_size = reader.read(32)
    literal_offset = reader.read(32)
    if output_size != expected_size:
        raise PackError(f"LZD header output size {output_size} does not match index size {expected_size}")
    if literal_offset > len(data):
        raise PackError("LZD literal-data offset outside compressed block")

    def symbol() -> int:
        reader.prepare_lookup()
        node, bit_count = decode[reader.bits & 0x3FF]
        if node <= 0xFF:
            if bit_count == 0:
                raise PackError("invalid LZD fast-decode entry")
            reader.read(bit_count)
            return node
        reader.read(10)
        steps = 0
        while node > 0xFF:
            if node < 0 or node >= len(nodes) or steps > 512:
                raise PackError("invalid LZD prefix code")
            node = nodes[node][reader.read(1)]
            steps += 1
        return node

    literals = memoryview(data)[literal_offset:]
    literal_pos = 0
    out = bytearray()

    def append_literals(amount: int):
        nonlocal literal_pos
        if amount < 0 or amount > expected_size - len(out):
            raise PackError("LZD literal run exceeds expected output")
        if literal_pos + amount > len(literals):
            raise PackError("LZD literal run exceeds literal-data section")
        out.extend(literals[literal_pos:literal_pos + amount])
        literal_pos += amount

    def copy_match(distance: int, amount: int):
        if distance <= 0 or distance > len(out):
            raise PackError(f"invalid LZD back-reference distance {distance}")
        if amount < 0 or amount > expected_size - len(out):
            raise PackError("LZD back-reference exceeds expected output")
        for _ in range(amount):
            out.append(out[-distance])

    for _ in range(command_count):
        token = symbol()
        kind = token & 0xC0
        if kind == 0:
            amount = token + 3
            append_literals(amount)
            continue
        if kind == 0xC0:
            distance_high = symbol()
            distance = (distance_high | (token << 8)) & 0x3FFF
            copy_match(distance, symbol() + 6)
        elif kind == 0x80:
            distance = token & 0x3F
            copy_match(distance, symbol() + 6)
        elif kind == 0x40:
            copy_match(symbol(), (token & 0x3F) + 6)

    append_literals(symbol())
    if len(out) != expected_size:
        raise PackError(f"LZD produced {len(out)} bytes, expected {expected_size}")
    return bytes(out)


def lzd_decompress_resource(data: bytes, expected_size: int) -> bytes:
    """Decode either a raw LZD block or a StreamCompression-wrapped resource."""
    if len(data) < 32 or struct.unpack_from(">I", data, 4)[0] != 0xC5735A11:
        return lzd_decompress(data, expected_size)

    checksum, codec, block_count, total_size, max_input, block_size, aux, _ = struct.unpack_from(">8I", data)
    if codec != 0xC5735A11 or total_size != expected_size:
        raise PackError("invalid StreamCompression LZD header")
    if block_count == 0 or block_count > 1_000_000 or block_size == 0:
        raise PackError("invalid StreamCompression block dimensions")
    index_end = 32 + block_count * 12
    if index_end > len(data):
        raise PackError("truncated StreamCompression block index")

    records = [struct.unpack_from(">3I", data, 32 + i * 12) for i in range(block_count)]
    state = 0x811C9DC5
    for value in (codec, block_count, total_size, max_input, block_size, aux):
        state = ((state * 16777619) & 0xFFFFFFFF) ^ value
    for record in records:
        for value in record:
            state = ((state * 16777619) & 0xFFFFFFFF) ^ value
    if state != checksum:
        raise PackError(f"StreamCompression checksum mismatch (0x{state:08X} != 0x{checksum:08X})")

    output = bytearray()
    for i, (offset, stored_size, _reserved) in enumerate(records):
        if offset < index_end or offset > len(data) or stored_size > len(data) - offset:
            raise PackError(f"invalid LZD block range {i}: 0x{offset:X}+0x{stored_size:X}")
        wanted = min(block_size, total_size - len(output))
        if wanted <= 0:
            raise PackError("too many LZD blocks for declared output size")
        output.extend(lzd_decompress(data[offset:offset + stored_size], wanted))
    if len(output) != total_size:
        raise PackError(f"StreamCompression produced {len(output)} bytes, expected {total_size}")
    return bytes(output)


def zlib_decompress_block(data: bytes, expected_size: int) -> bytes:
    """Decode one zlib-wrapped block as used by the engine's ZLib path."""
    if expected_size < 0 or expected_size > 512 * 1024 * 1024:
        raise PackError(f"unreasonable zlib output size {expected_size}")
    decoder = zlib.decompressobj(wbits=zlib.MAX_WBITS)
    try:
        output = decoder.decompress(data, expected_size + 1)
        if len(output) > expected_size or decoder.unconsumed_tail:
            raise PackError("zlib output exceeds expected block size")
        output += decoder.flush()
    except zlib.error as exc:
        raise PackError(f"invalid zlib block: {exc}") from exc
    if not decoder.eof:
        raise PackError("truncated zlib stream")
    if decoder.unused_data:
        raise PackError("unexpected trailing bytes after zlib stream")
    if len(output) != expected_size:
        raise PackError(f"zlib produced {len(output)} bytes, expected {expected_size}")
    return output


def zlib_decompress_resource(data: bytes, expected_size: int) -> bytes:
    """Decode a zlib resource directly or through StreamCompression blocks."""
    if len(data) < 32 or struct.unpack_from(">I", data, 4)[0] != CODEC_ZLIB:
        return zlib_decompress_block(data, expected_size)

    checksum, codec, block_count, total_size, max_input, block_size, aux, _ = struct.unpack_from(">8I", data)
    if codec != CODEC_ZLIB or total_size != expected_size:
        raise PackError("invalid StreamCompression ZLib header")
    if block_count == 0 or block_count > 1_000_000 or block_size == 0:
        raise PackError("invalid StreamCompression block dimensions")
    index_end = 32 + block_count * 12
    if index_end > len(data):
        raise PackError("truncated StreamCompression block index")

    records = [struct.unpack_from(">3I", data, 32 + i * 12) for i in range(block_count)]
    state = 0x811C9DC5
    for value in (codec, block_count, total_size, max_input, block_size, aux):
        state = ((state * 16777619) & 0xFFFFFFFF) ^ value
    for record in records:
        for value in record:
            state = ((state * 16777619) & 0xFFFFFFFF) ^ value
    if state != checksum:
        raise PackError(f"StreamCompression checksum mismatch (0x{state:08X} != 0x{checksum:08X})")

    output = bytearray()
    for i, (offset, stored_size, _reserved) in enumerate(records):
        if offset < index_end or offset > len(data) or stored_size > len(data) - offset:
            raise PackError(f"invalid zlib block range {i}: 0x{offset:X}+0x{stored_size:X}")
        wanted = min(block_size, total_size - len(output))
        if wanted <= 0:
            raise PackError("too many zlib blocks for declared output size")
        output.extend(zlib_decompress_block(data[offset:offset + stored_size], wanted))
    if len(output) != total_size:
        raise PackError(f"StreamCompression produced {len(output)} bytes, expected {total_size}")
    return bytes(output)


def decompress_resource(data: bytes, expected_size: int, codec: str) -> bytes:
    if codec == "lzd":
        return lzd_decompress_resource(data, expected_size)
    if codec == "zlib":
        return zlib_decompress_resource(data, expected_size)
    if codec == "lzx":
        return lzx_decompress_resource(data, expected_size)
    raise PackError(f"compressed codec {codec!r} is not implemented")


def _parse_lzx_chunks(data: bytes, expected_size: int) -> list[tuple[bytes, int]]:
    """Parse the game's 2/5-byte chunk headers and return raw LZX chunks."""
    if len(data) <= 5:
        raise PackError("truncated game LZX stream")
    pos = 0
    input_end = len(data) - 5
    output_total = 0
    chunks = []
    while pos < input_end:
        if data[pos] == 0xFF:
            if pos + 5 > input_end:
                raise PackError("truncated extended LZX chunk header")
            out_size = (data[pos + 1] << 8) | data[pos + 2]
            packed_size = (data[pos + 3] << 8) | data[pos + 4]
            pos += 5
        else:
            if pos + 2 > input_end:
                raise PackError("truncated LZX chunk header")
            packed_size = (data[pos] << 8) | data[pos + 1]
            out_size = LZX_FRAME_SIZE
            pos += 2
        if out_size == 0 or packed_size == 0 or packed_size > input_end - pos:
            raise PackError("invalid game LZX chunk dimensions")
        if out_size > expected_size - output_total:
            raise PackError("game LZX chunks exceed expected output size")
        chunks.append((data[pos:pos + packed_size], out_size))
        output_total += out_size
        pos += packed_size
    if pos != input_end or output_total != expected_size:
        raise PackError(f"game LZX framing describes {output_total} output bytes; expected {expected_size}")
    return chunks


def _find_7z() -> str | None:
    candidates = [
        shutil.which("7z"),
        shutil.which("7zz"),
        r"C:\Program Files\7-Zip\7z.exe",
        r"C:\Program Files (x86)\7-Zip\7z.exe",
    ]
    return next((p for p in candidates if p and os.path.isfile(p)), None)


def _make_lzx_cab(expected_size: int, chunks: list[tuple[bytes, int]]) -> bytes:
    """Wrap game chunks as a single CAB LZX folder for an external decoder."""
    if not chunks or expected_size > 0xFFFFFFFF:
        raise PackError("LZX stream is too large for a CAB bridge")
    if len(chunks) > 0xFFFF:
        raise PackError("too many game LZX chunks for a CAB bridge")

    cab_data = bytearray()
    for payload, uncompressed_size in chunks:
        cb_data = len(payload)
        cb_uncomp = uncompressed_size
        if cb_data > 0xFFFF or cb_uncomp > 0xFFFF:
            raise PackError("LZX frame exceeds CAB CFDATA limits")
        cab_data += struct.pack("<IHH", 0, cb_data, cb_uncomp)
        cab_data += payload

    cffolder_offset = 36
    cffile_offset = cffolder_offset + 8
    cffile = struct.pack("<IIHHHH", expected_size, 0, 0, 0, 0, 0x20)
    cffile += b"data.bin\0"
    data_offset = cffile_offset + len(cffile)
    folder = struct.pack("<IHH", data_offset, len(chunks), (LZX_WINDOW_BITS << 8) | 3)
    cab_size = data_offset + len(cab_data)
    cab_header = struct.pack("<4sIIIIIBBHHHHH", b"MSCF", 0, cab_size, 0,
                             cffile_offset, 0, 3, 1, 1, 1, 0, 0, 0)
    return cab_header + folder + cffile + cab_data


def lzx_decompress_resource(data: bytes, expected_size: int) -> bytes:
    """Decode game-framed LZX by bridging it to an installed 7-Zip CAB decoder."""
    if len(data) >= 32 and struct.unpack_from(">I", data, 4)[0] == CODEC_LZX:
        checksum, codec, block_count, total_size, max_input, block_size, aux, _ = struct.unpack_from(">8I", data)
        if codec != CODEC_LZX or total_size != expected_size:
            raise PackError("invalid StreamCompression LZX header")
        if block_count == 0 or block_count > 1_000_000 or block_size == 0:
            raise PackError("invalid StreamCompression block dimensions")
        index_end = 32 + block_count * 12
        if index_end > len(data):
            raise PackError("truncated StreamCompression block index")
        records = [struct.unpack_from(">3I", data, 32 + i * 12) for i in range(block_count)]
        state = 0x811C9DC5
        for value in (codec, block_count, total_size, max_input, block_size, aux):
            state = ((state * 16777619) & 0xFFFFFFFF) ^ value
        for record in records:
            for value in record:
                state = ((state * 16777619) & 0xFFFFFFFF) ^ value
        if state != checksum:
            raise PackError(f"StreamCompression checksum mismatch (0x{state:08X} != 0x{checksum:08X})")
        payload = bytearray()
        for i, (offset, stored_size, _reserved) in enumerate(records):
            if offset < index_end or offset > len(data) or stored_size > len(data) - offset:
                raise PackError(f"invalid LZX block range {i}")
            wanted = min(block_size, total_size - len(payload))
            if wanted <= 0:
                raise PackError("too many LZX blocks for declared output size")
            payload.extend(data[offset:offset + stored_size])
        framed_data = bytes(payload)
    else:
        framed_data = data

    chunks = _parse_lzx_chunks(framed_data, expected_size)
    seven_zip = _find_7z()
    if not seven_zip:
        raise PackError("LZX decoding requires 7-Zip (7z.exe) installed")
    cab_data = _make_lzx_cab(expected_size, chunks)

    with tempfile.TemporaryDirectory(prefix="fear3-lzx-") as temp:
        cab_path = os.path.join(temp, "block.cab")
        out_dir = os.path.join(temp, "out")
        os.mkdir(out_dir)
        with open(cab_path, "wb") as cab_file:
            cab_file.write(cab_data)
        try:
            result = subprocess.run(
                [seven_zip, "x", "-y", f"-o{out_dir}", cab_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=120,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise PackError(f"could not run 7-Zip LZX decoder: {exc}") from exc
        output_path = os.path.join(out_dir, "data.bin")
        if result.returncode not in (0, 1) or not os.path.isfile(output_path):
            detail = result.stderr.decode(errors="replace").strip() or result.stdout.decode(errors="replace").strip()
            raise PackError(f"7-Zip LZX decode failed (exit {result.returncode}): {detail}")
        output = Path(output_path).read_bytes()
    if len(output) != expected_size:
        raise PackError(f"LZX produced {len(output)} bytes, expected {expected_size}")
    return output


class Pack:
    def __init__(self, path: Path):
        self.path = path
        self.stream = path.open("rb")
        self.size = os.fstat(self.stream.fileno()).st_size
        self.header = self._read_header()
        self.strings = read_exact_at(self.stream, self.header.string_offset, self.header.string_size)
        self.dirs = self._read_dirs()
        self.files = self._read_files()
        self._validate_index()

    def close(self):
        self.stream.close()

    def _range_ok(self, offset: int, length: int, what: str):
        if offset > self.size or length > self.size - offset:
            raise PackError(f"{what} range outside archive: 0x{offset:X}+0x{length:X}")

    def _read_header(self) -> Header:
        prefix = read_exact_at(self.stream, 0, 8)
        if prefix[:4] != MAGIC:
            raise PackError(f"bad magic {prefix[:4]!r}, expected {MAGIC!r}")
        version = struct.unpack_from("<I", prefix, 4)[0]
        if version not in SUPPORTED_VERSIONS:
            raise PackError(f"unsupported version 0x{version:08X}; supported: v107/v108")

        # The v107/v108 disk header is 14 little-endian DWORDs after the prefix.
        raw = read_exact_at(self.stream, 8, 56)
        fields = struct.unpack("<14I", raw)
        header = Header(
            version=version,
            file_count=fields[1],
            file_offset=fields[3],
            dir_count=fields[4],
            dir_offset=fields[6],
            string_size=fields[7],
            string_offset=fields[8],
            flags=fields[11],
            block_size=fields[12],
            aux_offset=fields[13],
        )

        if header.file_count > 20_000_000 or header.dir_count > 5_000_000:
            raise PackError("implausible index counts")
        self._range_ok(header.file_offset, header.file_count * FILE_RECORD.size, "file index")
        self._range_ok(header.dir_offset, header.dir_count * DIR_RECORD.size, "directory index")
        self._range_ok(header.string_offset, header.string_size, "string table")
        return header

    def _read_files(self) -> list[FileEntry]:
        h = self.header
        data = read_exact_at(self.stream, h.file_offset, h.file_count * FILE_RECORD.size)
        return [FileEntry(*FILE_RECORD.unpack_from(data, i * FILE_RECORD.size)) for i in range(h.file_count)]

    def _read_dirs(self) -> list[DirEntry]:
        h = self.header
        data = read_exact_at(self.stream, h.dir_offset, h.dir_count * DIR_RECORD.size)
        return [DirEntry(*DIR_RECORD.unpack_from(data, i * DIR_RECORD.size)) for i in range(h.dir_count)]

    def _validate_index(self):
        for i, entry in enumerate(self.files):
            if entry.parent >= len(self.dirs):
                raise PackError(f"file record {i} has invalid parent directory {entry.parent}")
            self._range_ok(entry.data_offset, entry.stored_size, f"file payload {i}")
            if (entry.name_offset & 0x7FFFFFFF) >= len(self.strings):
                raise PackError(f"file record {i} has invalid string offset {entry.name_offset}")
        for i, entry in enumerate(self.dirs):
            if (entry.name_offset & 0x7FFFFFFF) >= len(self.strings):
                raise PackError(f"directory record {i} has invalid string offset {entry.name_offset}")
            if entry.parent != 0xFFFFFFFF and entry.parent >= len(self.dirs):
                raise PackError(f"directory record {i} has invalid parent {entry.parent}")
            no_files = entry.first_file == 0xFFFFFFFF and entry.end_file == 0xFFFFFFFF
            if not no_files and (entry.first_file > entry.end_file or entry.end_file > len(self.files)):
                raise PackError(f"directory record {i} has invalid file range")

    def name(self, offset: int) -> str:
        offset &= 0x7FFFFFFF
        end = self.strings.find(b"\0", offset)
        if end < 0:
            raise PackError(f"unterminated string at string-table offset 0x{offset:X}")
        return self.strings[offset:end].decode("utf-8", errors="replace")

    def directory_path(self, index: int) -> str:
        parts = []
        seen = set()
        while index != 0xFFFFFFFF:
            if index >= len(self.dirs) or index in seen:
                raise PackError("invalid/cyclic directory parent chain")
            seen.add(index)
            entry = self.dirs[index]
            name = self.name(entry.name_offset)
            if name:
                parts.append(name)
            index = entry.parent
        return "/".join(reversed(parts))

    def file_path(self, entry: FileEntry) -> str:
        name = self.name(entry.name_offset)
        parent = self.directory_path(entry.parent)
        combined = f"{parent}/{name}" if parent else name
        return "/".join(part for part in combined.replace("\\", "/").split("/") if part not in ("", "."))

    def codec(self) -> str:
        flags = self.header.flags
        if flags & FLAG_ZLIB:
            return "zlib"
        if flags & FLAG_LZX:
            return "lzx"
        if flags & FLAG_LZD:
            return "lzd"
        return "unknown/none"

    def is_compressed(self, entry: FileEntry) -> bool:
        return bool(self.header.flags & FLAG_INTERNAL_COMPRESSION) and entry.logical_size != entry.stored_size

    def list_files(self):
        for entry in self.files:
            yield entry, self.file_path(entry)

    def extract(self, destination: Path, raw_compressed: bool, path_filter: str | None = None):
        destination.mkdir(parents=True, exist_ok=True)
        extracted = 0
        raw = 0
        skipped = 0
        for entry, name in self.list_files():
            if path_filter and path_filter.casefold() not in name.casefold():
                continue
            path = PurePosixPath(name)
            if ".." in path.parts or not path.parts:
                raise PackError(f"unsafe archive path: {name!r}")
            compressed = self.is_compressed(entry)
            can_decode = compressed and self.codec() in ("lzd", "zlib", "lzx")
            if compressed and not raw_compressed and not can_decode:
                skipped += 1
                continue
            suffix = ".packed" if compressed and raw_compressed else ""
            target = destination.joinpath(*path.parts)
            if suffix:
                target = target.with_name(target.name + suffix)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("wb") as output:
                self.stream.seek(entry.data_offset)
                payload = self.stream.read(entry.stored_size)
            if len(payload) != entry.stored_size:
                raise PackError(f"short payload read for {name!r}")
            decoded = False
            if compressed and not raw_compressed and can_decode:
                try:
                    payload = decompress_resource(payload, entry.logical_size, self.codec())
                    decoded = True
                except PackError as exc:
                    raise PackError(f"{self.codec()} decode failed for {name!r}: {exc}") from exc
            with target.open("wb") as output:
                output.write(payload)
            if compressed and not decoded:
                raw += 1
            else:
                extracted += 1
        return extracted, raw, skipped


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--list", action="store_true", help="print indexed paths and sizes")
    parser.add_argument("--extract", type=Path, help="extract uncompressed entries to this directory")
    parser.add_argument("--raw-compressed", action="store_true", help="also save compressed payloads as .packed (not decoded)")
    parser.add_argument("--filter", help="only list/extract paths containing this substring")
    args = parser.parse_args(argv)

    try:
        pack = Pack(args.archive)
        try:
            h = pack.header
            print(f"archive: {args.archive} ({pack.size:,} bytes)")
            print(f"version: 0x{h.version:08X}; files: {h.file_count:,}; dirs: {h.dir_count:,}")
            print(f"flags: 0x{h.flags:08X}; codec: {pack.codec()}; block size: {h.block_size:,}")
            print(f"tables: files@0x{h.file_offset:X}, dirs@0x{h.dir_offset:X}, strings@0x{h.string_offset:X}+0x{h.string_size:X}")
            if args.list:
                for entry, name in pack.list_files():
                    if args.filter and args.filter.casefold() not in name.casefold():
                        continue
                    mark = "compressed" if pack.is_compressed(entry) else "stored"
                    print(f"{entry.logical_size:>10} logical  {entry.stored_size:>10} stored  {mark:10}  {name}")
            if args.extract:
                extracted, raw, skipped = pack.extract(args.extract, args.raw_compressed, args.filter)
                print(f"extracted: {extracted}; raw compressed payloads: {raw}; skipped compressed: {skipped}")
                if skipped:
                    print(f"NOTE: {skipped} compressed entries use a codec not implemented by this prototype.")
        finally:
            pack.close()
    except (OSError, PackError, struct.error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
