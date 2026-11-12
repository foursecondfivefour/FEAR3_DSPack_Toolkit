# Archive Format

## Prefix And Versions

The current parser accepts the eight-byte prefix:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 4 | ASCII `mgf ` (`6D 67 66 20`) |
| `0x04` | 4 | Version, little-endian; supported values are `0x5A5A0107` and `0x5A5A0108` |

IDA analysis found the game validating this prefix in `sub_93A0A0` and dispatching v107/v108 parsing in `sub_AA9AC0`. Older versions are not accepted by this prototype.

## Common Index Header

After the 8-byte prefix, the prototype reads the common 56-byte index header as fourteen little-endian DWORDs. It uses these fields:

| Header-relative offset | Absolute file offset | Field used by parser | Meaning |
|---:|---:|---|---|
| `0x04` | `0x0C` | `file_count` | Number of file records |
| `0x0C` | `0x14` | `file_offset` | File-record table offset |
| `0x10` | `0x18` | `dir_count` | Number of directory records |
| `0x18` | `0x20` | `dir_offset` | Directory-record table offset |
| `0x1C` | `0x24` | `string_size` | String-table size in bytes |
| `0x20` | `0x28` | `string_offset` | String-table offset |
| `0x2C` | `0x34` | `flags` | Archive flags |
| `0x30` | `0x38` | `block_size` | Internal compression block size |
| `0x34` | `0x3C` | `aux_offset` | Auxiliary/version-specific value; not interpreted by the parser |

The game reads a larger version-dependent header for v107/v108 and processes additional lookup data. `dspack.py` currently parses the common fields it needs for file listing and extraction; it does not reconstruct every auxiliary index. Treat the table above as the parser's consumed layout, not a complete specification of every header field.

## File And Directory Records

The parser reads each file and directory record as six little-endian unsigned 32-bit integers (`<6I`), for 24 bytes per record.

File records are interpreted as:

| Record offset | Parsed field | Use |
|---:|---|---|
| `+0x00` | name offset | NUL-terminated string-table offset; high bit is masked as an engine marker |
| `+0x04` | parent directory | Index into the directory table |
| `+0x08` | logical size | Size after decompression |
| `+0x0C` | stored size | Bytes stored in the archive |
| `+0x10` | timestamp | Preserved metadata; not currently used to set filesystem timestamps |
| `+0x14` | data offset | Payload offset in the archive |

The payload-offset and size roles are supported by the engine's member-open path `sub_826C70`; remaining labels follow the consumers of those fields.

Directory records are interpreted as:

| Record offset | Parsed field | Use |
|---:|---|---|
| `+0x00` | name offset | String-table offset |
| `+0x04` | parent | Parent directory index; `0xFFFFFFFF` denotes the root boundary |
| `+0x08` | first child | Directory enumeration link |
| `+0x0C` | next sibling | Directory enumeration link |
| `+0x10` | first file | Start of the directory's file range |
| `+0x14` | end file | End-exclusive file range; both range fields can be `0xFFFFFFFF` for no files |

The engine's directory enumerator `sub_D72620` follows child/sibling links; `sub_A9C9D0` iterates file ranges. The extractor constructs output paths from file parent indices and the directory parent chain.

## Strings And Paths

Names are NUL-terminated byte strings in the shared string table, not fixed-size inline fields. The parser decodes them as UTF-8 with replacement for invalid sequences. Path separators are normalized to `/`, and empty/dot components are removed. File names retain their archive casing and spaces.

The tool rejects `..` components before writing. It does not currently implement a complete Windows-specific path sanitizer for reserved device names or all drive/UNC forms; use a fresh destination and review `--list` output when working with untrusted archives.

## Compression Classification

The extractor classifies an entry as internally compressed when archive flag `0x02` (`InternalCompression`) is set and `logical_size != stored_size`. This rule is observed in the game's file-open path and used by `Pack.is_compressed()`.

The high bit `0x80000000` is reported by the game as `MiniPack`; it is a filesystem/mount concept, not another codec supported by this parser.
