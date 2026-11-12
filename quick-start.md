# Quick Start

Run from the FEAR 3 installation directory, or pass absolute paths.

## Inspect And List

```powershell
py -3 .\dspack.py .\resources\Resource.dsPack
py -3 .\dspack.py .\resources\Resource.dsPack --list --filter luasense
```

The first command prints archive/version/index metadata. `--list` prints paths and logical/stored sizes. `--filter` is a case-insensitive substring match.

## Extract An Archive

Use one folder per archive to avoid collisions between identical internal paths:

```powershell
py -3 .\dspack.py .\resources\Boot.dsPack --extract C:\Games\fear3-out\other\Boot
```

By default, uncompressed entries are copied and LZD entries are decoded. Unsupported compressed codecs are counted as skipped. Existing files at destination paths are overwritten; use a fresh folder if you want to preserve prior output.

## Keep Encoded Payloads

```powershell
py -3 .\dspack.py .\resources\Resource.dsPack --extract C:\Temp\fear3-raw --raw-compressed
```

Compressed entries are written as `<original-name>.packed`; these are stored payloads, not ready-to-use assets. Uncompressed entries are still copied normally.

## Extract A Subset

```powershell
py -3 .\dspack.py .\resources\Resource.dsPack --extract C:\Temp\fear3-audio --filter Audio/Cues
```

The filter applies to both listing and extraction. It does not support wildcards or regular expressions.

## Output Summary

The CLI reports `extracted`, `raw compressed payloads`, and `skipped compressed` counts. If `skipped compressed` is nonzero, the output is partial; currently that applies to compressed ZLib/LZX entries.
