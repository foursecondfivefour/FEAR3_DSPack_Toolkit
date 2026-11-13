# Archive Format

## Prefix And Versions

Both readers accept an eight-byte prefix:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 4 | ASCII `mgf ` (`6D 67 66 20`) |
| `0x04` | 4 | Little-endian version `0x5A5A0107` or `0x5A5A0108` |

IDA analysis found validation in `sub_93A0A0` and v107/v108 parser dispatch in `sub_AA9AC0`. Other versions are rejected.

## Common Index Header

After the 8-byte prefix, the tools read the common 56-byte index header as fourteen little-endian DWORDs:

| Header-relative offset | Absolute file offset | Field | Meaning |
|---:|---:|---|---|
| `0x04` | `0x0C` | `file_count` | Number of file records |
| `0x0C` | `0x14` | `file_offset` | File-table offset |
| `0x10` | `0x18` | `dir_count` | Directory-record count |
| `0x18` | `0x20` | `dir_offset` | Directory-table offset |
| `0x1C` | `0x24` | `string_size` | String-table size |
| `0x20` | `0x28` | `string_offset` | String-table offset |
| `0x2C` | `0x34` | `flags` | Archive flags |
| `0x30` | `0x38` | `block_size` | Internal compression block size |
| `0x34` | `0x3C` | `aux_offset` | Auxiliary value, not interpreted |

The game reads a larger version-specific header and additional lookup data. This table documents fields used by the extractors, not the complete container specification.

## Records

File and directory records are 24 bytes, each six little-endian unsigned 32-bit integers (`<6I`).

File records:

| Record offset | Field | Meaning/use |
|---:|---|---|
| `+0x00` | name offset | NUL-terminated string-table offset; high bit masked |
| `+0x04` | parent | Directory index |
| `+0x08` | logical size | Expanded size |
| `+0x0C` | stored size | Payload size in archive |
| `+0x10` | timestamp | Parsed, not restored to output timestamps |
| `+0x14` | data offset | Payload offset |

The engine member-open path `sub_826C70` supports the payload offset/size interpretation.

Directory records:

| Record offset | Field | Meaning/use |
|---:|---|---|
| `+0x00` | name offset | String-table offset |
| `+0x04` | parent | Parent index; `0xFFFFFFFF` root boundary |
| `+0x08` | first child | Enumeration link |
| `+0x0C` | next sibling | Enumeration link |
| `+0x10` | first file | Beginning of file range |
| `+0x14` | end file | End-exclusive; both may be `0xFFFFFFFF` if empty |

The engine `sub_D72620` follows child/sibling links and `sub_A9C9D0` iterates file ranges.

## Paths And Safety

Names are NUL-terminated bytes in a shared string table. Python decodes with UTF-8 replacement; C++ converts UTF-8 to Windows filesystem paths. Separators normalize to `/`; empty and `.` components are removed.

Both reject `..`. C++ also rejects rooted/absolute and drive-colon names and duplicate normalized destinations within one archive. Neither tool is a hardened sandbox for hostile archives. Inspect paths and extract to a fresh output directory.

## Compression Classification

An entry is internally compressed when flag `0x02` (`InternalCompression`) is set and logical size differs from stored size. The high bit `0x80000000` is the engine's `MiniPack` filesystem/mount behavior, not a codec.
