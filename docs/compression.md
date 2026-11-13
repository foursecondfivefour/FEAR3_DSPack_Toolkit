# Compression And Codecs

## Engine Codec Flags

IDA analysis of `sub_AA9AC0` identifies these archive-level codec selections:

| Flag | Codec ID | Name | C++ GUI/CLI | Older Python reference |
|---:|---:|---|---|---|
| `0x08` | `0xC5735A0F` | ZLib | Supported using local `zlib1.dll` | Supported using Python `zlib` |
| `0x10` | `0xC5735A10` | LZX | Unsupported; skip or preserve raw | Experimental 7-Zip CAB bridge, unverified on real game data |
| `0x20` | `0xC5735A11` | LZD | Native support | Python support |

The game diagnostic `sub_A767C0` names ZLib, LZX, and LZD. Codec selection is archive-level. `Audio.dsPack` advertises ZLib, but inspected entries have equal stored/logical sizes and are copied as stored data; audio codecs are not decoded here.

## ZLib

The engine path `sub_A50420 -> sub_E5A3A0 -> sub_F023C0` sends each block to zlib 1.2.3 with the expected output size. It uses the regular zlib wrapper rather than raw DEFLATE. C++ dynamically loads the copied `dspack_cpp_runtime/zlib1.dll`; Python uses its standard library. Both validate output size. Python additionally checks EOF and trailing data.

ZLib may be raw per-resource or inside `StreamCompression`. Both implementations validate the wrapper checksum and block ranges and inflate each indexed block to its declared size.

## LZD

The C++ and Python decoders are based on engine functions `sub_B44930`, `sub_714AC0`, `sub_4694F0`, `sub_65C290`, `sub_7D7630`, `sub_AE3D50`, and `sub_78A130`.

The decoder builds a symbol tree and 10-bit lookup table, reads big-endian 32-bit bitstream words (low-order bits consumed first), then executes literal runs and three match forms. Back-reference copies can overlap. Output must match the logical size from the archive index.

## StreamCompression Wrapper

The game parser `sub_839990` reads a 32-byte header and 12-byte block records; `sub_A50420` seeks to blocks and decodes them.

Both tools interpret eight big-endian DWORDs:

| Offset | Meaning |
|---:|---|
| `+0x00` | Checksum |
| `+0x04` | Codec ID |
| `+0x08` | Block count |
| `+0x0C` | Total logical size |
| `+0x10` | Max input/buffer field, included in checksum |
| `+0x14` | Logical block size |
| `+0x18` | Auxiliary DWORD, included in checksum |
| `+0x1C` | Trailing DWORD, ignored |

Each 12-byte big-endian record contains payload offset, stored size, and a reserved/unknown DWORD. Checksum starts at `0x811C9DC5`; header DWORDs 1-6 and all record DWORDs are folded as `hash = (hash * 16777619) XOR word` modulo 32 bits. Recovered from `sub_D7AD60` and `sub_8986D0`, then validated on installed resources.

## LZX: Experimental Python Bridge Only

The game uses LZX with a 1-MiB window and FEAR-specific framing. `sub_F0C650` parses a two-byte compressed size for 32-KiB output or an extended `FF + output size + compressed size` header; the stream reserves a five-byte trailer.

The old Python reference wraps frames as a synthetic CAB (`LZX:20`) and invokes 7-Zip. No installed archive contains an LZX entry, so this has not been validated on actual FEAR 3 data. C++ intentionally does not attempt the bridge: LZX entries are skipped or preserved raw.

## Practical Support

- C++: LZD and ZLib; no LZX.
- Python reference: LZD, ZLib, and an experimental/unverified LZX bridge.
- `--raw-compressed` preserves stored bytes as `.packed`, not decoded assets.
- Neither program creates or recompresses archives.
