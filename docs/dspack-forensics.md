Forensic report: .dsPack containers

Date of analysis: 2026-11-10
Target: /home/drcerg/Desktop/WORK/SINSRC/resources
Mode: static examination, read-only; files were not launched or unpacked to disk.
Summary of findings

All 13 .dsPack files in resources and its subdirectories were examined. Total size — 3 056 566 272 bytes. SHA-256 was calculated over the full contents of each file. In all containers, the headers are consistent with the recognized dsPack structure, the sizes of the record directories and their ranges; all 52 927 file records and 6 726 folder records were read. Record names and attributes, stored/pre-restoration sizes, and payload addresses were studied.

The format and tables were structurally established with high confidence. The packs use two byte orders: 7 files little-endian and 6 localized audio packs big-endian. The containers contain real game resource names; in particular, the audio packs index WAV files. For most game payloads, the size after decompression differs from the stored size, but the decompression algorithm and full decoding of every payload were not confirmed within this analysis. Encryption was not established: high entropy alone is not proof of encryption.
Scope and methods

    All resources/.dsPack and resources/Localized/Audio/.dsPack files were recursively enumerated; all 13 files were checked by exact paths.

    SHA-256 was computed for the full file by streaming reads. Full-file entropies and block series were calculated by REA; for the index tables, an independent byte-level parse was additionally performed with bounds and reference checks.

    Both header byte orders, directory fields, table lengths, name strings, folder indices, and all fixed-length 24-byte records were checked.

    Payload ranges were checked for EOF overrun and range overlaps were counted. Overlaps were retained as an observation, not automatically declared corruption: they may reflect shared regions or container features.

    file returns data for all 13 files. 7-Zip refused to open each .dsPack as a supported archive. This is expected for a specialized game container and does not refute its valid internal structure.

    Signature scanners reported individual potential gzip/zlib/ZIP/CAB matches inside high-entropy data, but the full streams and checksums of these candidates were not validated. These matches are not used as evidence of embedded archives.

    Extraction, running the game, QuickBMS/unverified third-party extractors, and key-guessing attempts were not performed.

Structural layout

The parsing corresponds to the publicly published Game Extractor reverse-engineering description for F.E.A.R. 3 dsPack. This is a third-party specification, not an official developer document; its fields are accepted only where they are confirmed by consistent file data.
Offset	Field	Size
0x00	Magic 1: little-endian mgf or big-endian fgm	4
0x04	Magic 2: value 08 01 5a 5a in file byte order	4
0x08	Zero/reserved field	4
0x0c	Number of file records	4
0x10, 0x14	File table length and offset	4 + 4
0x18	Number of folder records	4
0x1c, 0x20	Folder table length and offset	4 + 4
0x24, 0x28	Name table length and offset	4 + 4
0x2c onward	Not fully documented/additional fields and padding	—

The file table consists of 24-byte rows: name offset relative to the name table, parent folder ID, restored data size, stored data size, unknown 32-bit field, and payload offset. The folder table also consists of 24-byte rows and contains a name offset, parent ID, and folder/file links. Multi-byte fields were read in the endian order of the corresponding file. The stated unknown record field is not treated as a CRC without independent validation.

In all files, the lengths of the file and folder tables are exactly number_of_rows * 24. The boundaries of both tables and the name table are within the file. All used name offsets are valid, parent folder IDs are in range, and all payloads end no later than EOF. This is a structural check of the index, not proof of correctness or decompressibility of all payloads.

For the first file / last file links in folder records, one boundary record was found in each of the 13 packs where the last file value equals the number of files in the index (for example, 2 813 with 2 813 records). This value is not a valid zero-based index under the usual inclusive-range model. It was retained as a schema/sentinel ambiguity; treating these links as ordinary indices without checking the game parser is not confirmed. The remaining checked parent-folder and child-folder links are within expected bounds.
Container inventory

Entropy is given in bits per byte. The measurement does not distinguish compression, encryption, and arbitrary binary data. Sizes and SHA-256 refer to the exact original bytes.
File (from resources/)	Size, bytes	Byte order	Files	Folders	File entropy	SHA-256
Audio.dsPack	415 760 384	LE	2 813	123	7.8655	16ca81ca8e335aec92ddd474c6b73eacc97b37ccb1d285e1adb9e1fff6ec7304
Boot.dsPack	103 219 200	LE	2 464	571	7.7056	a05a409eba3f0ce342a5d87a98bc46a2d205035f89641f22780ba122396bf54b
DLC_MP1.dsPack	67 469 312	LE	668	132	7.7509	f0ef591b97b02e79cbab8f156daebe70ce70f1a9e7a5528988de3cf869eca2ba
DLC_MP2.dsPack	112 132 096	LE	832	177	7.7222	132c2b331bd30b282102e5adefded4d4322327cf2ca04b37f853c891e7e6abdf
Patch.dsPack	16 875 520	LE	40	44	6.7485	1b93c9f6cd4dab57e4e0e2a024e83db30949928aa65edc6c5f10fd3bc7ef8231
Preloads.dsPack	12 419 072	LE	557	305	7.0727	62330f5f539216a3652a08c99241c87afe89bf32990ca1e95150d617dc5788ac
Resource.dsPack	1 764 655 104	LE	30 853	5 158	7.7034	db189f9ba8c8507122054348045897a289aa2a8673b0f56ded6f98b05b234105
Localized/Audio/English.dsPack	90 734 592	BE	2 450	36	7.7442	c187b19db1b9a9363a71c4034c8ce5940cc8dc4e2ef16a42b08ac3ef5d78decc
Localized/Audio/French.dsPack	89 784 320	BE	2 450	36	7.7141	09716c31676e46b564bfaf80d90ef2a7d787fce62a2e8f62cce9f20781412a1b
Localized/Audio/German.dsPack	107 118 592	BE	2 450	36	7.7438	15d93f0724e346bd8c14869282ac3475726dc807488fb12b90612bdaf06a8b58
Localized/Audio/Italian.dsPack	91 062 272	BE	2 450	36	7.7113	fbd2cda755e87abafba632047fc3193470783bad818d7fab8250c09682b49e81
Localized/Audio/Japanese.dsPack	93 847 552	BE	2 450	36	7.5782	6721d1eecaa2ad3b8af5f7207f51cbebff528f90c689f94044532bec186a8948
Localized/Audio/Spanish.dsPack	91 488 256	BE	2 450	36	7.7068	926c285c3e8e911dc4135ce12cecec4f2fa61505456bfd5f318d1f1073782044

Total: 52 927 file and 6 726 folder records. In the localized audio packs, the index is uniform: 2 450 file records and 36 folders per language; names in the table include crc.map and 2 449 WAV files. The byte order of these files is determined by fgm and confirmed by correct interpretation of all sizes and offsets in big-endian. Reading them incorrectly as little-endian produces impossible counters and addresses.
Compression, payloads, and signatures

The total declared file sizes before restoration and the stored data sizes are consistent with the presence of many uncompressed and compressed records. Comparing the two lengths only gives an indication of possible compression; it does not determine the codec.
File	Equal declared/stored sizes	Differing sizes	Empty records	Payload range overlaps after sorting
Audio.dsPack	2 813	0	0	0
Boot.dsPack	216	2 248	0	71
DLC_MP1.dsPack	53	615	0	0
DLC_MP2.dsPack	65	767	0	5
Patch.dsPack	0	40	0	0
Preloads.dsPack	111	446	64	13
Resource.dsPack	3 134	27 719	0	25
Each localized audio pack	2 450	0	0	0 or 1

The absence of a size difference does not guarantee uncompressed bytes, and a difference in lengths does not identify the algorithm. In Preloads.dsPack, 64 records have zero lengths/offsets, which is permitted by the published format description; they do not exceed the file boundaries. For checking intersections, stored ranges [offset, offset + compressed_size) were used; the observations by themselves prove neither corruption nor sharing.

Signature sampling of payloads confirms the WAV representation of audio files: typical record starts have RIFF and a 32-bit little-endian RIFF size. Large containers contain game extensions (.dsworld, .dsobject, .tif, .hkx, .dsmodel, .lua, .dssoundcue, and others); the name table shows that this is an organized game resource index, not an ordinary ZIP/CAB. Readable names were also observed in Resource.dsPack, for example Neighborhood_sign_DET.tif, _NOR.tif, and _DIF.tif.

Simple detection of gzip/zlib/ZIP/CAB byte markers at an arbitrary offset is not equivalent to confirming a nested archive. Strict attribution of a stream requires parsing a valid header and validating the full stream/checksum tied to a specific index record's range. This was not proven for all records; the contents of all files were not dumped. In particular, 7-Zip does not open the .dsPack files themselves as a supported archive, which does not prevent parsing dsPack tables with a specialized parser.
Entropy and zero tails

For all files, entropy was calculated over the full file. The summary numbers are rounded: REA used 4 KiB blocks for files that fit within its limit of 32 768 blocks; for Audio.dsPack and Boot.dsPack — 64 KiB blocks. For the large Resource.dsPack, the shown entropy of 7.7034 was obtained by an independent full pass with 64-KiB blocks. Unequal block size affects the block series but not the identity of the file. The last 64 bytes of all 13 files are entirely zero; the contiguous tail length is given below.
File	Zero tail, bytes
Audio.dsPack	10 629
Boot.dsPack	13 538
DLC_MP1.dsPack	21 367
DLC_MP2.dsPack	21 714
Patch.dsPack	730
Preloads.dsPack	7 992
Resource.dsPack	22 694
Localized/Audio/English.dsPack	17 882
Localized/Audio/French.dsPack	4 762
Localized/Audio/German.dsPack	10 762
Localized/Audio/Italian.dsPack	3 998
Localized/Audio/Japanese.dsPack	30 242
Localized/Audio/Spanish.dsPack	26 782

High entropy of large regions is expected for compressed resources but is also compatible with encryption. Low-entropy areas, zero padding, valid tables, and readable names show a mixed container structure. The available observations are insufficient to declare the payload encrypted or fully unencrypted.
Limitations and further verification

    Hashing and structural checking cover the full bytes of the files, but this does not mean that every payload was decoded and verified against internal checksums.

    The semantics of the unknown 32-bit field in the file record, the exact padding/alignment, and the compression algorithms have not been confirmed by an official specification.

    The interpretation of the boundary last file == file_count in one folder of each container remains unknown; likely the field uses a convention different from an ordinary index or a sentinel.

    The numerous range overlaps found require separate case-by-case verification if a repacker/extractor is needed; they cannot be automatically “fixed” or treated as an error.

    To confirm decodability and content integrity, the next step is an isolated verified dsPack parser that extracts records to a temporary directory outside the original tree, checks each length, and compares the result with the declared uncompressed size. Do not run extracted code.

    A cryptographic conclusion requires examination of the code/documentation for the corresponding format, a known key/test vector, or other reproducible verification; entropy is insufficient.

Sources for methodology and format

    Context7, Python documentation (/python/cpython): hashlib for streaming SHA-256; struct.unpack_from for fields by offset; zlib.decompressobj.eof, unused_data, and unconsumed_tail as mandatory signs for correct validation of a complete deflate stream. hashlib, struct, zlib.

    Microsoft Learn, Cabinet Files: CAB is a separate container format. The comparison is needed to test hypotheses about random MSCF matches, but dsPack is not identified as CAB by one found signature.

    Watto/Game Extractor, Archive_DSPACK: third-party reverse-engineered description of tables and two endian variants. Used as a testable hypothesis, not an official specification.

    Public search for the format also revealed experimental third-party tools and community reports about incompletely researched F.E.A.R. 3 compression. They were not run and are not considered proof of properties of these specific files.
