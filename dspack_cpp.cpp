#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>

namespace fs = std::filesystem;

static constexpr uint32_t kVersion107 = 0x5A5A0107;
static constexpr uint32_t kVersion108 = 0x5A5A0108;
static constexpr uint32_t kInternalCompression = 0x02;
static constexpr uint32_t kZlibFlag = 0x08;
static constexpr uint32_t kLzxFlag = 0x10;
static constexpr uint32_t kLzdFlag = 0x20;
static constexpr uint32_t kCodecZlib = 0xC5735A0F;
static constexpr uint32_t kCodecLzx = 0xC5735A10;
static constexpr uint32_t kCodecLzd = 0xC5735A11;
static constexpr int kZlibFinish = 4;

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
static std::atomic<bool> gConsoleCancel{false};
static std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    if(n<=0)throw Error("invalid UTF-8 output text");
    std::wstring out(size_t(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),n);
    return out;
}
static std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)throw Error("cannot convert command-line filter to UTF-8");
    std::string out(size_t(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);
    return out;
}

static uint32_t ReadLE32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint32_t ReadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

struct MappedFile {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const uint8_t* data = nullptr;
    size_t size = 0;
    explicit MappedFile(const fs::path& path) {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw Error("cannot open archive: " + path.string());
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || uint64_t(length.QuadPart) > SIZE_MAX)
            throw Error("cannot determine archive size");
        size = static_cast<size_t>(length.QuadPart);
        if (size) {
            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (!mapping) throw Error("CreateFileMapping failed");
            data = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
            if (!data) throw Error("MapViewOfFile failed");
        }
    }
    ~MappedFile() {
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    void CheckRange(uint64_t offset, uint64_t length, const char* what) const {
        if (offset > size || length > uint64_t(size) - offset)
            throw Error(std::string(what) + " range is outside archive");
    }
};

struct Header {
    uint32_t version, fileCount, fileOffset, dirCount, dirOffset;
    uint32_t stringSize, stringOffset, flags, blockSize;
};
struct FileEntry { uint32_t name, parent, logicalSize, storedSize, timestamp, offset; };
struct DirEntry { uint32_t name, parent, firstChild, nextSibling, firstFile, endFile; };

struct Pack {
    const MappedFile& mapped;
    Header header{};
    std::vector<FileEntry> files;
    std::vector<DirEntry> dirs;
    std::string strings;
    std::string codec;

    explicit Pack(const MappedFile& m) : mapped(m) { Parse(); }

    void Parse() {
        if (mapped.size < 64 || memcmp(mapped.data, "mgf ", 4) != 0) throw Error("bad dsPack magic/header");
        header.version = ReadLE32(mapped.data + 4);
        if (header.version != kVersion107 && header.version != kVersion108) throw Error("unsupported dsPack version");
        const uint8_t* h = mapped.data + 8;
        uint32_t f[14];
        for (unsigned i = 0; i < 14; ++i) f[i] = ReadLE32(h + i * 4);
        header.fileCount = f[1]; header.fileOffset = f[3];
        header.dirCount = f[4]; header.dirOffset = f[6];
        header.stringSize = f[7]; header.stringOffset = f[8];
        header.flags = f[11]; header.blockSize = f[12];
        if (header.fileCount > 20'000'000 || header.dirCount > 5'000'000) throw Error("implausible index counts");
        mapped.CheckRange(header.fileOffset, uint64_t(header.fileCount) * 24, "file index");
        mapped.CheckRange(header.dirOffset, uint64_t(header.dirCount) * 24, "directory index");
        mapped.CheckRange(header.stringOffset, header.stringSize, "string table");
        strings.assign(reinterpret_cast<const char*>(mapped.data + header.stringOffset), header.stringSize);
        files.reserve(header.fileCount); dirs.reserve(header.dirCount);
        for (uint32_t i = 0; i < header.fileCount; ++i) {
            const auto* p = mapped.data + header.fileOffset + size_t(i) * 24;
            FileEntry e{ReadLE32(p), ReadLE32(p+4), ReadLE32(p+8), ReadLE32(p+12), ReadLE32(p+16), ReadLE32(p+20)};
            if (e.parent >= header.dirCount || (e.name & 0x7FFFFFFF) >= strings.size()) throw Error("invalid file index entry");
            mapped.CheckRange(e.offset, e.storedSize, "payload");
            files.push_back(e);
        }
        for (uint32_t i = 0; i < header.dirCount; ++i) {
            const auto* p = mapped.data + header.dirOffset + size_t(i) * 24;
            DirEntry d{ReadLE32(p), ReadLE32(p+4), ReadLE32(p+8), ReadLE32(p+12), ReadLE32(p+16), ReadLE32(p+20)};
            if ((d.name & 0x7FFFFFFF) >= strings.size()) throw Error("invalid directory name offset");
            if (d.parent != 0xFFFFFFFF && d.parent >= header.dirCount) throw Error("invalid directory parent");
            const bool noFiles = d.firstFile == 0xFFFFFFFF && d.endFile == 0xFFFFFFFF;
            if (!noFiles && (d.firstFile > d.endFile || d.endFile > header.fileCount)) throw Error("invalid directory file range");
            dirs.push_back(d);
        }
        if (header.flags & kZlibFlag) codec = "zlib";
        else if (header.flags & kLzxFlag) codec = "lzx";
        else if (header.flags & kLzdFlag) codec = "lzd";
        else codec = "unknown/none";
    }

    std::string Name(uint32_t offset) const {
        offset &= 0x7FFFFFFF;
        if (offset >= strings.size()) throw Error("string offset out of range");
        size_t end = strings.find('\0', offset);
        if (end == std::string::npos) throw Error("unterminated string table entry");
        return strings.substr(offset, end - offset);
    }
    std::string DirectoryPath(uint32_t index) const {
        std::vector<std::string> parts;
        std::vector<bool> seen(dirs.size(), false);
        while (index != 0xFFFFFFFF) {
            if (index >= dirs.size() || seen[index]) throw Error("invalid/cyclic directory hierarchy");
            seen[index] = true;
            auto name = Name(dirs[index].name);
            if (!name.empty()) parts.push_back(name);
            index = dirs[index].parent;
        }
        std::string result;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!result.empty()) result += '/';
            result += *it;
        }
        return result;
    }
    std::string FilePath(const FileEntry& e) const {
        std::string parent = DirectoryPath(e.parent), name = Name(e.name);
        std::replace(name.begin(), name.end(), '\\', '/');
        std::string path = parent.empty() ? name : parent + "/" + name;
        std::string normalized; size_t pos = 0;
        while (pos <= path.size()) {
            size_t end = path.find('/', pos);
            if (end == std::string::npos) end = path.size();
            std::string_view part(path.data() + pos, end - pos);
            if (part == "..") throw Error("unsafe parent path in archive entry");
            if (!part.empty() && part != ".") { if (!normalized.empty()) normalized += '/'; normalized.append(part); }
            pos = end + 1;
            if (end == path.size()) break;
        }
        if (normalized.empty() || normalized.find(':') != std::string::npos) throw Error("unsafe archive path: " + normalized);
        return normalized;
    }
    bool Compressed(const FileEntry& e) const {
        return (header.flags & kInternalCompression) && e.logicalSize != e.storedSize;
    }
};

struct BitReader {
    const uint8_t* data; size_t size; size_t pos = 0; uint64_t bits = 0; unsigned available = 0;
    BitReader(const uint8_t* p, size_t n) : data(p), size(n) { Refill(); Refill(); }
    void Refill() {
        if (pos + 4 <= size) {
            uint32_t word = ReadBE32(data + pos); pos += 4;
            bits |= uint64_t(word) << available; available += 32;
        }
    }
    uint32_t Read(unsigned count) {
        if (count > 32) throw Error("invalid LZD bit count");
        if (available <= 32 && pos + 4 <= size) Refill();
        if (available < count) throw Error("truncated LZD bitstream");
        uint32_t value = count == 32 ? uint32_t(bits) : (count ? uint32_t(bits & ((uint64_t(1) << count) - 1)) : 0);
        bits >>= count; available -= count; return value;
    }
    void PrepareLookup() { if (available <= 32 && pos + 4 <= size) Refill(); }
};

struct LzdTree {
    std::array<std::array<int,2>,512> nodes{};
    std::array<std::pair<int,uint8_t>,1024> decode{};
    size_t bitStart = 0;
};

static LzdTree BuildLzdTree(const uint8_t* data, size_t size) {
    if (!size) throw Error("empty LZD block");
    uint32_t count = data[0];
    size_t bitStart = size_t(2) * count + 2;
    if (bitStart > size) throw Error("truncated LZD tree");
    LzdTree t{}; t.bitStart = bitStart;
    for (auto& n : t.nodes) n = {0,0};
    uint32_t active = 0;
    auto parse = [&](auto&& self, size_t treePos, size_t symbolPos, int node, uint32_t previous, unsigned depth, uint32_t code) -> size_t {
        if (++active > 4096 || depth > 512 || node < 0 || node >= int(t.nodes.size())) throw Error("invalid LZD prefix tree");
        if (treePos >= size || symbolPos >= size) throw Error("truncated LZD tree data");
        uint32_t marker = data[treePos];
        size_t nextSymbol;
        if (marker) {
            t.nodes[size_t(node)][0] = node - 1;
            nextSymbol = self(self, treePos + 1, symbolPos, node - 1, marker, depth + 1, code);
        } else {
            t.nodes[size_t(node)][0] = data[symbolPos];
            nextSymbol = symbolPos + 1;
        }
        code = depth < 32 ? ((uint32_t(1) << depth) | code) : 0;
        if (uint32_t(previous - marker) <= 1) {
            if (nextSymbol >= size) throw Error("truncated LZD leaf table");
            t.nodes[size_t(node)][1] = data[nextSymbol++];
        } else {
            int child = node - int(marker) - 1;
            if (child < 0) throw Error("invalid LZD child node");
            t.nodes[size_t(node)][1] = child;
            nextSymbol = self(self, treePos + marker + 1, nextSymbol, child, previous - marker - 1, depth + 1, code);
        }
        return nextSymbol;
    };
    const int root = int(count) + 255;
    parse(parse, size_t(count) + 2, 1, root, count, 0, 0);
    for (uint32_t slot = 0; slot < t.decode.size(); ++slot) {
        int node = root; uint8_t bits = 0;
        while (node > 0xFF && bits < 10) {
            if (node >= int(t.nodes.size())) throw Error("bad LZD fast table node");
            node = t.nodes[size_t(node)][(slot >> bits) & 1]; ++bits;
        }
        t.decode[slot] = {node, bits};
    }
    return t;
}

static std::vector<uint8_t> LzdBlock(const uint8_t* data, size_t size, uint32_t expected) {
    if (size == expected) return std::vector<uint8_t>(data, data + size);
    if (expected > 512u * 1024u * 1024u) throw Error("unreasonable LZD output size");
    auto tree = BuildLzdTree(data, size);
    BitReader br(data + tree.bitStart, size - tree.bitStart);
    uint32_t commandCount = br.Read(32), outputSize = br.Read(32), literalOffset = br.Read(32);
    if (outputSize != expected || literalOffset > size) throw Error("invalid LZD block header");
    auto symbol = [&]() -> uint32_t {
        br.PrepareLookup();
        auto [node0, bitCount] = tree.decode[size_t(br.bits & 0x3FF)];
        int node = node0;
        if (node <= 0xFF) {
            if (!bitCount) throw Error("invalid LZD fast lookup");
            return br.Read(bitCount), uint32_t(node);
        }
        br.Read(10);
        unsigned steps = 0;
        while (node > 0xFF) {
            if (node < 0 || node >= int(tree.nodes.size()) || ++steps > 512) throw Error("invalid LZD prefix code");
            node = tree.nodes[size_t(node)][br.Read(1)];
        }
        return uint32_t(node);
    };
    const uint8_t* literals = data + literalOffset;
    size_t literalSize = size - literalOffset, literalPos = 0;
    std::vector<uint8_t> out; out.reserve(expected);
    auto appendLiterals = [&](uint32_t amount) {
        if (amount > expected - out.size() || amount > literalSize - literalPos) throw Error("LZD literal run out of range");
        out.insert(out.end(), literals + literalPos, literals + literalPos + amount); literalPos += amount;
    };
    auto copyMatch = [&](uint32_t distance, uint32_t amount) {
        if (!distance || distance > out.size() || amount > expected - out.size()) throw Error("invalid LZD back-reference");
        for (uint32_t i = 0; i < amount; ++i) out.push_back(out[out.size() - distance]);
    };
    for (uint32_t i = 0; i < commandCount; ++i) {
        uint32_t token = symbol();
        switch (token & 0xC0) {
        case 0x00: appendLiterals(token + 3); break;
        case 0xC0: {
            uint32_t distanceHigh = symbol();
            uint32_t distance = (distanceHigh | (token << 8)) & 0x3FFF;
            copyMatch(distance, symbol() + 6); break;
        }
        case 0x80: copyMatch(token & 0x3F, symbol() + 6); break;
        case 0x40: copyMatch(symbol(), (token & 0x3F) + 6); break;
        }
    }
    appendLiterals(symbol());
    if (out.size() != expected) throw Error("LZD output size mismatch");
    return out;
}

static uint32_t ChecksumStreamCompression(const uint8_t* data, const std::vector<std::array<uint32_t,3>>& records) {
    uint32_t state = 0x811C9DC5;
    for (unsigned i = 1; i <= 6; ++i) state = state * 16777619u ^ ReadBE32(data + i * 4);
    for (const auto& r : records) for (uint32_t v : r) state = state * 16777619u ^ v;
    return state;
}

static std::vector<uint8_t> LzdResource(const uint8_t* data, size_t size, uint32_t expected) {
    if (size < 32 || ReadBE32(data + 4) != kCodecLzd) return LzdBlock(data, size, expected);
    uint32_t checksum=ReadBE32(data), codec=ReadBE32(data+4), count=ReadBE32(data+8), total=ReadBE32(data+12);
    uint32_t maxInput=ReadBE32(data+16), blockSize=ReadBE32(data+20), aux=ReadBE32(data+24);
    (void)maxInput; (void)aux;
    if (codec != kCodecLzd || total != expected || !count || count > 1'000'000 || !blockSize) throw Error("invalid LZD StreamCompression header");
    if (uint64_t(32) + uint64_t(count) * 12 > size) throw Error("truncated LZD block table");
    std::vector<std::array<uint32_t,3>> records; records.reserve(count);
    for (uint32_t i=0;i<count;++i) {
        const uint8_t* r=data+32+size_t(i)*12;
        records.push_back({ReadBE32(r),ReadBE32(r+4),ReadBE32(r+8)});
    }
    if (ChecksumStreamCompression(data, records) != checksum) throw Error("LZD StreamCompression checksum mismatch");
    std::vector<std::vector<uint8_t>> blocks(count);
    std::vector<uint32_t> lengths(count);
    uint64_t outTotal=0;
    for (uint32_t i=0;i<count;++i) {
        auto [offset, packed, reserved]=records[i]; (void)reserved;
        if (offset < 32 + uint64_t(count)*12 || offset > size || packed > size-offset) throw Error("LZD block range outside resource");
        uint32_t wanted=uint32_t(std::min<uint64_t>(blockSize,total-outTotal));
        if (!wanted) throw Error("too many LZD blocks");
        lengths[i]=wanted; outTotal+=wanted;
    }
    if (outTotal != total) throw Error("LZD blocks do not cover declared output");
    // Entry-level workers already run concurrently. Keep blocks within one
    // resource sequential to avoid nested-thread oversubscription.
    for(uint32_t i=0;i<count;++i) blocks[i]=LzdBlock(data+records[i][0],records[i][1],lengths[i]);
    std::vector<uint8_t> output; output.reserve(total);
    for(auto& b:blocks)output.insert(output.end(),b.begin(),b.end());
    return output;
}

// zlib ABI loaded dynamically from a side-by-side zlib1.dll (Python's redistributable copy).
using z_uInt = unsigned int; using z_uLong = unsigned long; using z_Bytef = unsigned char;
struct z_stream_s;
using z_alloc_func = void* (__cdecl*)(void*, z_uInt, z_uInt);
using z_free_func = void (__cdecl*)(void*, void*);
struct z_stream_s {
    z_Bytef* next_in; z_uInt avail_in; z_uLong total_in;
    z_Bytef* next_out; z_uInt avail_out; z_uLong total_out;
    char* msg; void* state; z_alloc_func zalloc; z_free_func zfree; void* opaque;
    int data_type; z_uLong adler; z_uLong reserved;
};
struct ZlibApi {
    HMODULE module=nullptr;
    const char* (__cdecl* version)()=nullptr;
    int (__cdecl* inflateInit2_)(z_stream_s*,int,const char*,int)=nullptr;
    int (__cdecl* inflate)(z_stream_s*,int)=nullptr;
    int (__cdecl* inflateEnd)(z_stream_s*)=nullptr;
    explicit ZlibApi(const fs::path& runtime) {
        fs::path dll=runtime/L"zlib1.dll";
        module=LoadLibraryW(dll.c_str());
        if(!module)throw Error("cannot load zlib1.dll from runtime folder");
        version=reinterpret_cast<decltype(version)>(GetProcAddress(module,"zlibVersion"));
        inflateInit2_=reinterpret_cast<decltype(inflateInit2_)>(GetProcAddress(module,"inflateInit2_"));
        inflate=reinterpret_cast<decltype(inflate)>(GetProcAddress(module,"inflate"));
        inflateEnd=reinterpret_cast<decltype(inflateEnd)>(GetProcAddress(module,"inflateEnd"));
        if(!version||!inflateInit2_||!inflate||!inflateEnd)throw Error("zlib1.dll lacks required inflate API");
    }
    ~ZlibApi(){if(module)FreeLibrary(module);}
    std::vector<uint8_t> Decode(const uint8_t* input,size_t inputSize,uint32_t expected) const {
        if(inputSize>UINT_MAX||expected>UINT_MAX)throw Error("zlib block exceeds API limits");
        std::vector<uint8_t> output(expected);
        z_stream_s s{}; s.next_in=const_cast<z_Bytef*>(input); s.avail_in=z_uInt(inputSize);
        s.next_out=output.data(); s.avail_out=z_uInt(expected);
        int rc=inflateInit2_(&s,15,version(),sizeof(s)); if(rc!=0)throw Error("zlib inflateInit2 failed");
        rc=inflate(&s, kZlibFinish); inflateEnd(&s);
        if(rc!=1||s.total_out!=expected||s.avail_in!=0)throw Error("zlib decode failed or size mismatch");
        return output;
    }
};

static std::vector<uint8_t> ZlibResource(const uint8_t* data,size_t size,uint32_t expected,const ZlibApi& z) {
    if(size<32||ReadBE32(data+4)!=kCodecZlib)return z.Decode(data,size,expected);
    uint32_t checksum=ReadBE32(data),codec=ReadBE32(data+4),count=ReadBE32(data+8),total=ReadBE32(data+12),blockSize=ReadBE32(data+20);
    if(codec!=kCodecZlib||total!=expected||!count||count>1'000'000||!blockSize)throw Error("invalid ZLib StreamCompression header");
    if(uint64_t(32)+uint64_t(count)*12>size)throw Error("truncated ZLib block table");
    std::vector<std::array<uint32_t,3>> records;records.reserve(count);
    for(uint32_t i=0;i<count;++i){const uint8_t*r=data+32+size_t(i)*12;records.push_back({ReadBE32(r),ReadBE32(r+4),ReadBE32(r+8)});}
    if(ChecksumStreamCompression(data,records)!=checksum)throw Error("ZLib StreamCompression checksum mismatch");
    std::vector<uint32_t> lengths(count);uint64_t out=0;
    for(uint32_t i=0;i<count;++i){auto [off,packed,res]=records[i];(void)res;if(off<32+uint64_t(count)*12||off>size||packed>size-off)throw Error("invalid ZLib block range");uint32_t wanted=uint32_t(std::min<uint64_t>(blockSize,total-out));if(!wanted)throw Error("too many ZLib blocks");lengths[i]=wanted;out+=wanted;}
    if(out!=total)throw Error("ZLib blocks do not cover output");
    // Blocks are decoded in parallel by the extraction worker pool at resource granularity.
    std::vector<std::vector<uint8_t>> blocks(count);
    for(uint32_t i=0;i<count;++i)blocks[i]=z.Decode(data+records[i][0],records[i][1],lengths[i]);
    std::vector<uint8_t> result;result.reserve(total);
    for(auto& block:blocks)result.insert(result.end(),block.begin(),block.end());
    return result;
}

struct Options { fs::path archive, output; bool list=false, raw=false; std::string filter; unsigned workers=0; };
[[maybe_unused]] static Options ParseArgs(int argc,wchar_t** argv){
    Options o;
    for(int i=1;i<argc;++i){std::wstring a=argv[i];
        if(a==L"--list")o.list=true;
        else if(a==L"--raw-compressed")o.raw=true;
        else if(a==L"--extract"&&i+1<argc)o.output=argv[++i];
        else if(a==L"--filter"&&i+1<argc)o.filter=WideToUtf8(argv[++i]);
        else if(a==L"--threads"&&i+1<argc){o.workers=std::stoul(argv[++i]);if(!o.workers)throw Error("--threads must be positive");}
        else if(a==L"--help"||a==L"-h"){std::wcout<<L"usage: dspack_cpp.exe archive [--list] [--extract DIR] [--filter TEXT] [--raw-compressed] [--threads N]\n";ExitProcess(0);}
        else if(o.archive.empty())o.archive=a;
        else throw Error("unknown or incomplete argument");
    }
    if(o.archive.empty())throw Error("archive path is required");
    if(!o.list&&o.output.empty())throw Error("specify --list or --extract DIR");
    return o;
}

static std::string LowerAscii(std::string s){for(char&c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
static fs::path SafeTarget(const fs::path& root,const std::string& name,bool raw){
    fs::path rel=fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size())); if(rel.is_absolute()||rel.has_root_name())throw Error("absolute entry path rejected");
    for(auto& part:rel)if(part==L"..")throw Error("parent path rejected");
    fs::path target=root/rel;if(raw)target+=L".packed";return target;
}

struct ExtractStats { uint64_t extracted=0, raw=0, skipped=0, processed=0, total=0; bool cancelled=false; std::string error; };
using ProgressCallback=std::function<void(uint64_t,uint64_t,const std::string&)>;
static ExtractStats ExtractSingleArchive(const fs::path& archivePath,const fs::path& destination,const std::wstring& filterText,unsigned requestedWorkers,bool rawCompressed,const std::atomic<bool>* cancel,const ProgressCallback& progress={}) {
    MappedFile archive(archivePath);Pack pack(archive);std::string filter=LowerAscii(WideToUtf8(filterText));
    std::vector<std::pair<uint32_t,std::string>> selected;selected.reserve(pack.files.size());
    for(uint32_t i=0;i<pack.files.size();++i){auto name=pack.FilePath(pack.files[i]);if(filter.empty()||LowerAscii(name).find(filter)!=std::string::npos)selected.emplace_back(i,std::move(name));}
    ExtractStats result;result.total=selected.size();fs::create_directories(destination);
    std::vector<fs::path> targets(selected.size());std::vector<uint8_t> shouldRun(selected.size(),1);std::unordered_set<std::wstring> uniqueTargets;
    for(size_t i=0;i<selected.size();++i){const auto&e=pack.files[selected[i].first];bool compressed=pack.Compressed(e);targets[i]=SafeTarget(destination,selected[i].second,rawCompressed&&compressed);std::wstring key=targets[i].lexically_normal().native();for(auto&c:key)c=std::towlower(c);if(!uniqueTargets.insert(key).second)throw Error("duplicate output path collision: "+selected[i].second);if(compressed&&!rawCompressed&&pack.codec!="lzd"&&pack.codec!="zlib")shouldRun[i]=0;}
    if(selected.empty())return result;
    unsigned workerCount=requestedWorkers?requestedWorkers:std::max(1u,std::thread::hardware_concurrency());workerCount=std::min<unsigned>(workerCount,32);workerCount=std::max(1u,std::min<unsigned>(workerCount,unsigned(selected.size())));
    std::unique_ptr<ZlibApi> zlib;
    // Resolve the side-by-side runtime relative to the executable instead of the working directory.
    if(pack.codec=="zlib"&&!rawCompressed){wchar_t exePath[MAX_PATH]{};DWORD len=GetModuleFileNameW(nullptr,exePath,MAX_PATH);if(!len||len>=MAX_PATH)throw Error("cannot resolve executable path for zlib runtime");zlib=std::make_unique<ZlibApi>(fs::path(exePath).parent_path()/L"dspack_cpp_runtime");}
    std::atomic<size_t> next{0};std::atomic<uint64_t> extracted{0},raw{0},skipped{0},done{0};std::atomic<bool> stop{false};std::mutex errorMutex;std::string error;
    auto worker=[&]{while(!stop.load(std::memory_order_relaxed)){
        if(cancel&&cancel->load(std::memory_order_relaxed)){stop=true;break;}
        size_t i=next.fetch_add(1);if(i>=selected.size())break;
        try{uint32_t idx=selected[i].first;const auto&e=pack.files[idx];bool compressed=pack.Compressed(e);
            if(!shouldRun[i])++skipped;
            else {const uint8_t*input=archive.data+e.offset;size_t inputSize=e.storedSize;std::vector<uint8_t> decoded;
                if(compressed&&!rawCompressed){if(pack.codec=="lzd")decoded=LzdResource(input,inputSize,e.logicalSize);else if(pack.codec=="zlib")decoded=ZlibResource(input,inputSize,e.logicalSize,*zlib);else throw Error("unsupported compressed codec");input=decoded.data();inputSize=decoded.size();}
                fs::create_directories(targets[i].parent_path());std::ofstream out(targets[i],std::ios::binary|std::ios::trunc);if(!out)throw Error("cannot open output file");out.write(reinterpret_cast<const char*>(input),std::streamsize(inputSize));if(!out)throw Error("write failed");
                if(compressed&&rawCompressed)++raw;else ++extracted;
            }
            uint64_t n=++done;if(progress&&(n%32==0||n==selected.size()))progress(n,selected.size(),selected[i].second);
        }catch(const std::exception&ex){std::lock_guard lock(errorMutex);if(error.empty())error=selected[i].second+": "+ex.what();stop=true;}
    }};
    std::vector<std::thread> pool;for(unsigned i=0;i<workerCount;++i)pool.emplace_back(worker);for(auto&t:pool)t.join();
    result.extracted=extracted;result.raw=raw;result.skipped=skipped;result.processed=done;result.cancelled=cancel&&cancel->load();result.error=std::move(error);
    if(progress)progress(result.processed,result.total,{});
    return result;
}

static std::vector<fs::path> FindArchives(const fs::path& root) {
    if(!fs::exists(root)||!fs::is_directory(root))throw Error("game folder does not exist");
    std::vector<fs::path> paths;
    for(auto it=fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied);it!=fs::recursive_directory_iterator();++it){
        if(!it->is_regular_file())continue;
        std::wstring ext=it->path().extension().wstring();for(auto&c:ext)c=std::towlower(c);
        if(ext==L".dspack")paths.push_back(it->path());
    }
    std::sort(paths.begin(),paths.end());return paths;
}

#ifdef DSPACK_GUI_BUILD
enum : int { IDC_GAME=101,IDC_OUT,IDC_ARCHIVES,IDC_SCAN,IDC_GAME_BROWSE,IDC_OUT_BROWSE,IDC_ALL,IDC_NONE,IDC_THREADS,IDC_FILTER,IDC_RAW,IDC_EXTRACT,IDC_CANCEL,IDC_PROGRESS,IDC_STATUS,IDC_FILTER_LABEL=201,IDC_THREADS_LABEL=202,IDC_THREADS_EDIT=203 };
static constexpr UINT WM_GUI_PROGRESS=WM_APP+21,WM_GUI_DONE=WM_APP+22;
struct GuiProgress { uint64_t done,total; std::wstring text; };
struct GuiDone { uint64_t extracted=0,raw=0,skipped=0;bool cancelled=false;std::vector<std::wstring> errors; };
static HWND gWindow=nullptr,gGameEdit=nullptr,gOutEdit=nullptr,gArchiveList=nullptr,gThreadsEdit=nullptr,gFilterEdit=nullptr,gRawCheck=nullptr,gProgress=nullptr,gStatus=nullptr,gRunButton=nullptr,gCancelButton=nullptr,gScanButton=nullptr;
static std::vector<fs::path> gArchives;
static std::thread gWorker;
static std::atomic<bool> gCancel{false};
static bool gClosing=false,gBusy=false;

static fs::path BrowseFolder(HWND owner,const wchar_t* title){
    IFileOpenDialog* dialog=nullptr;HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(FAILED(hr))throw Error("cannot create Windows folder dialog");
    DWORD options=0;hr=dialog->GetOptions(&options);if(SUCCEEDED(hr))hr=dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
    if(SUCCEEDED(hr))hr=dialog->SetTitle(title);if(SUCCEEDED(hr))hr=dialog->Show(owner);
    fs::path result;
    if(SUCCEEDED(hr)){IShellItem* item=nullptr;hr=dialog->GetResult(&item);if(SUCCEEDED(hr)){PWSTR path=nullptr;hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);if(SUCCEEDED(hr)){result=path;CoTaskMemFree(path);}item->Release();}}
    dialog->Release();if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};if(FAILED(hr))throw Error("folder dialog failed");return result;
}

static std::wstring GetControlText(HWND control){int n=GetWindowTextLengthW(control);std::wstring s(size_t(n)+1,L'\0');GetWindowTextW(control,s.data(),n+1);s.resize(size_t(n));return s;}
static void SetStatus(const std::wstring& text){SetWindowTextW(gStatus,text.c_str());}
static HWND AddControl(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){return CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,gWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);}
static void Layout(HWND hwnd,int width,int height){
    const int pad=14,buttonW=104,rowH=28;
    MoveWindow(gGameEdit,pad,40,width-3*pad-buttonW,rowH,TRUE);MoveWindow(GetDlgItem(hwnd,IDC_GAME_BROWSE),width-pad-buttonW,40,buttonW,rowH,TRUE);
    MoveWindow(gOutEdit,pad,100,width-3*pad-buttonW,rowH,TRUE);MoveWindow(GetDlgItem(hwnd,IDC_OUT_BROWSE),width-pad-buttonW,100,buttonW,rowH,TRUE);
    int listTop=166,listHeight=std::max(100,height-310);
    MoveWindow(gArchiveList,pad,listTop,width-2*pad,listHeight,TRUE);
    MoveWindow(GetDlgItem(hwnd,IDC_SCAN),pad,listTop+listHeight+8,92,rowH,TRUE);MoveWindow(GetDlgItem(hwnd,IDC_ALL),pad+100,listTop+listHeight+8,92,rowH,TRUE);MoveWindow(GetDlgItem(hwnd,IDC_NONE),pad+200,listTop+listHeight+8,92,rowH,TRUE);
    MoveWindow(GetDlgItem(hwnd,IDC_FILTER_LABEL),pad+340,listTop+listHeight+8,45,rowH,TRUE);MoveWindow(gFilterEdit,pad+385,listTop+listHeight+8,180,rowH,TRUE);MoveWindow(GetDlgItem(hwnd,IDC_THREADS_LABEL),pad+625,listTop+listHeight+8,58,rowH,TRUE);MoveWindow(gThreadsEdit,pad+685,listTop+listHeight+8,44,rowH,TRUE);MoveWindow(gRawCheck,pad+740,listTop+listHeight+8,150,rowH,TRUE);
    int controlsY=listTop+listHeight+44;MoveWindow(gProgress,pad,controlsY,width-2*pad,20,TRUE);MoveWindow(gStatus,pad,controlsY+25,width-2*pad,24,TRUE);
    MoveWindow(gRunButton,width-pad-2*buttonW-8,controlsY+54,buttonW,rowH,TRUE);MoveWindow(gCancelButton,width-pad-buttonW,controlsY+54,buttonW,rowH,TRUE);
}

static void SetBusy(bool busy){gBusy=busy;EnableWindow(gScanButton,busy?FALSE:TRUE);EnableWindow(gRunButton,busy?FALSE:TRUE);EnableWindow(gGameEdit,busy?FALSE:TRUE);EnableWindow(gOutEdit,busy?FALSE:TRUE);EnableWindow(gArchiveList,busy?FALSE:TRUE);EnableWindow(gThreadsEdit,busy?FALSE:TRUE);EnableWindow(gFilterEdit,busy?FALSE:TRUE);EnableWindow(gRawCheck,busy?FALSE:TRUE);EnableWindow(gCancelButton,busy?TRUE:FALSE);}

static void ScanArchivesAsync(){
    if(gBusy)return;
    fs::path root=GetControlText(gGameEdit);if(root.empty()){SetStatus(L"Select the FEAR 3 installation folder first.");return;}
    SetBusy(true);SetStatus(L"Scanning for .dsPack archives...");
    gWorker=std::thread([root]{
        std::vector<fs::path> found;std::wstring error;
        try{found=FindArchives(root);}catch(const std::exception&e){error=Utf8ToWide(e.what());}
        auto* result=new std::pair<std::vector<fs::path>,std::wstring>(std::move(found),std::move(error));
        PostMessageW(gWindow,WM_GUI_DONE,1,reinterpret_cast<LPARAM>(result));
    });
}

static void StartExtraction(){
    if(gBusy)return;
    fs::path root=GetControlText(gGameEdit),out=GetControlText(gOutEdit);
    if(root.empty()||out.empty()){SetStatus(L"Choose both the game folder and output folder.");return;}
    std::vector<fs::path> selected;
    for(int i=0;i<int(gArchives.size());++i)if(SendMessageW(gArchiveList,LB_GETSEL,i,0)>0)selected.push_back(gArchives[size_t(i)]);
    if(selected.empty()){SetStatus(L"Select at least one archive.");return;}
    {std::unordered_set<std::wstring> roots;for(const auto&p:selected){std::wstring key=p.stem().wstring();for(auto&c:key)c=std::towlower(c);if(!roots.insert(key).second){SetStatus(L"Selected archives have duplicate names; choose a subset to avoid output collisions.");return;}}}
    std::wstring filter=GetControlText(gFilterEdit);unsigned threads=0;try{threads=std::stoul(GetControlText(gThreadsEdit));}catch(...){threads=0;}
    bool raw=SendMessageW(gRawCheck,BM_GETCHECK,0,0)==BST_CHECKED;
    gCancel=false;SetBusy(true);SendMessageW(gProgress,PBM_SETRANGE32,0,1000);SendMessageW(gProgress,PBM_SETPOS,0,0);SetStatus(L"Starting extraction...");
    gWorker=std::thread([selected=std::move(selected),out,filter,threads,raw]{
        auto* result=new GuiDone();uint64_t overallDone=0,overallTotal=0;
        try{
            const std::string filterUtf8=LowerAscii(WideToUtf8(filter));
            for(const auto&archive:selected){MappedFile m(archive);Pack pack(m);for(const auto&e:pack.files){const auto name=pack.FilePath(e);if(filterUtf8.empty()||LowerAscii(name).find(filterUtf8)!=std::string::npos)++overallTotal;}}
    for(const auto&archive:selected){
                if(gCancel.load())break;
                fs::path dest=out/archive.stem();
                const uint64_t archiveBase=overallDone;
                auto progress=[&](uint64_t done,uint64_t,const std::string& current){
                    GuiProgress* p=new GuiProgress{archiveBase+done,overallTotal,current.empty()?L"Working...":Utf8ToWide(current)};
                    if(!PostMessageW(gWindow,WM_GUI_PROGRESS,0,reinterpret_cast<LPARAM>(p)))delete p;
                };
                ExtractStats s=ExtractSingleArchive(archive,dest,filter,threads,raw,&gCancel,progress);
                result->extracted+=s.extracted;result->raw+=s.raw;result->skipped+=s.skipped;
                overallDone+=s.processed;
                overallTotal+=s.total;
                if(s.cancelled){result->cancelled=true;break;}
                if(!s.error.empty())result->errors.push_back(archive.filename().wstring()+L": "+Utf8ToWide(s.error));
            }
        }catch(const std::exception&e){result->errors.push_back(Utf8ToWide(e.what()));}
        PostMessageW(gWindow,WM_GUI_DONE,2,reinterpret_cast<LPARAM>(result));
    });
}

static LRESULT CALLBACK GuiProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        gWindow=hwnd;
        AddControl(L"STATIC",L"Game folder",0,14,18,120,20,0);gGameEdit=AddControl(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,14,40,600,28,IDC_GAME);AddControl(L"BUTTON",L"Browse...",BS_PUSHBUTTON,0,0,0,0,IDC_GAME_BROWSE);
        AddControl(L"STATIC",L"Output folder",0,14,78,140,20,0);gOutEdit=AddControl(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,14,100,600,28,IDC_OUT);AddControl(L"BUTTON",L"Browse...",BS_PUSHBUTTON,0,0,0,0,IDC_OUT_BROWSE);
        AddControl(L"STATIC",L"Select archives (each gets its own output subfolder)",0,14,140,550,20,0);gArchiveList=AddControl(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,0,0,0,0,IDC_ARCHIVES);
        gScanButton=AddControl(L"BUTTON",L"Scan",BS_PUSHBUTTON,0,0,0,0,IDC_SCAN);AddControl(L"BUTTON",L"Select all",BS_PUSHBUTTON,0,0,0,0,IDC_ALL);AddControl(L"BUTTON",L"Select none",BS_PUSHBUTTON,0,0,0,0,IDC_NONE);
        AddControl(L"STATIC",L"Filter",0,0,0,45,24,IDC_FILTER_LABEL);gFilterEdit=AddControl(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,0,0,0,0,IDC_FILTER);AddControl(L"STATIC",L"Threads",0,0,0,55,24,IDC_THREADS_LABEL);gThreadsEdit=AddControl(L"EDIT",L"",WS_BORDER|ES_NUMBER,0,0,0,0,IDC_THREADS_EDIT);SetWindowTextW(gThreadsEdit,std::to_wstring(std::max(1u,std::thread::hardware_concurrency())).c_str());
        gRawCheck=AddControl(L"BUTTON",L"Keep compressed",BS_AUTOCHECKBOX,0,0,0,0,IDC_RAW);gProgress=AddControl(PROGRESS_CLASSW,L"",0,0,0,0,0,IDC_PROGRESS);gStatus=AddControl(L"STATIC",L"Choose the game folder, scan, then select archives to extract.",0,0,0,0,0,IDC_STATUS);
        gRunButton=AddControl(L"BUTTON",L"Extract selected",BS_DEFPUSHBUTTON,0,0,0,0,IDC_EXTRACT);gCancelButton=AddControl(L"BUTTON",L"Cancel",BS_PUSHBUTTON,0,0,0,0,IDC_CANCEL);EnableWindow(gCancelButton,FALSE);
        {wchar_t exePath[MAX_PATH]{};DWORD n=GetModuleFileNameW(nullptr,exePath,MAX_PATH);if(n&&n<MAX_PATH){fs::path base=fs::path(exePath).parent_path();SetWindowTextW(gGameEdit,base.c_str());SetWindowTextW(gOutEdit,(base.parent_path()/L"fear3-out").c_str());PostMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IDC_SCAN,BN_CLICKED),0);}}
        RECT r{};GetClientRect(hwnd,&r);Layout(hwnd,r.right,r.bottom);return 0;}
    case WM_SIZE:Layout(hwnd,LOWORD(lp),HIWORD(lp));return 0;
    case WM_COMMAND:{int id=LOWORD(wp);
        try{
            if(id==IDC_GAME_BROWSE||id==IDC_OUT_BROWSE){auto p=BrowseFolder(hwnd,id==IDC_GAME_BROWSE?L"Select F.E.A.R. 3 game folder":L"Select extraction output folder");if(!p.empty())SetWindowTextW(id==IDC_GAME_BROWSE?gGameEdit:gOutEdit,p.c_str());}
            else if(id==IDC_SCAN)ScanArchivesAsync();
            else if(id==IDC_ALL){for(int i=0;i<int(gArchives.size());++i)SendMessageW(gArchiveList,LB_SETSEL,TRUE,i);}
            else if(id==IDC_NONE){SendMessageW(gArchiveList,LB_SETSEL,FALSE,-1);}
            else if(id==IDC_EXTRACT)StartExtraction();
            else if(id==IDC_CANCEL){gCancel=true;SetStatus(L"Cancelling after current file...");}
        }catch(const std::exception&e){MessageBoxW(hwnd,Utf8ToWide(e.what()).c_str(),L"FEAR 3 dsPack extractor",MB_ICONERROR|MB_OK);}return 0;}
    case WM_GUI_PROGRESS:{std::unique_ptr<GuiProgress>p(reinterpret_cast<GuiProgress*>(lp));if(p){int pos=p->total?int(std::min<uint64_t>(1000,p->done*1000/p->total)):0;SendMessageW(gProgress,PBM_SETPOS,pos,0);SetStatus(p->text.c_str());}return 0;}
    case WM_GUI_DONE:{
        if(wp==1){if(gWorker.joinable())gWorker.join();auto* rawResult=reinterpret_cast<std::pair<std::vector<fs::path>,std::wstring>*>(lp);std::unique_ptr<std::pair<std::vector<fs::path>,std::wstring>>r(rawResult);SetBusy(false);if(!r->second.empty()){SetStatus(r->second);if(gClosing)DestroyWindow(hwnd);return 0;}gArchives=std::move(r->first);SendMessageW(gArchiveList,LB_RESETCONTENT,0,0);for(const auto&p:gArchives){std::wstring row=p.filename().wstring()+L"    ["+p.parent_path().wstring()+L"]";int i=int(SendMessageW(gArchiveList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str())));SendMessageW(gArchiveList,LB_SETSEL,TRUE,i);}SetStatus(std::to_wstring(gArchives.size())+L" archive(s) found; all selected.");if(gClosing)DestroyWindow(hwnd);return 0;}
        if(wp==2){if(gWorker.joinable())gWorker.join();std::unique_ptr<GuiDone>r(reinterpret_cast<GuiDone*>(lp));SetBusy(false);if(r->cancelled)SetStatus(L"Cancelled. Completed files were kept; a current file may be partial.");else SetStatus(L"Done. Extracted "+std::to_wstring(r->extracted)+L"; raw "+std::to_wstring(r->raw)+L"; unsupported skipped "+std::to_wstring(r->skipped)+L".");if(!r->errors.empty()){std::wstring errorText;for(auto&e:r->errors)errorText+=e+L"\n";MessageBoxW(hwnd,errorText.c_str(),L"Some archives had errors",MB_ICONWARNING|MB_OK);}if(gClosing)DestroyWindow(hwnd);return 0;}
        return 0;}
    case WM_CLOSE:if(gBusy){gClosing=true;gCancel=true;SetStatus(L"Cancelling before closing...");return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE, PWSTR,int show){
    HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(co)){MessageBoxW(nullptr,L"COM initialization failed.",L"FEAR 3 dsPack extractor",MB_ICONERROR);return 2;}
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_PROGRESS_CLASS};InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=GuiProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);wc.lpszClassName=L"Fear3DsPackGui";RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"FEAR 3 dsPack Modder Toolkit",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,980,760,nullptr,nullptr,instance,nullptr);
    if(!hwnd){CoUninitialize();return 2;}ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(gWorker.joinable())gWorker.join();CoUninitialize();return int(msg.wParam);
}
#endif

#ifndef DSPACK_GUI_BUILD
static BOOL WINAPI ConsoleHandler(DWORD type){
    if(type==CTRL_C_EVENT||type==CTRL_BREAK_EVENT||type==CTRL_CLOSE_EVENT){gConsoleCancel=true;return TRUE;}
    return FALSE;
}
static int RunModernCli(int argc,wchar_t** argv){
    std::wstring command=argv[1];
    if(command!=L"scan"&&command!=L"extract"&&command!=L"list")return -1;
    fs::path game,out;std::vector<std::wstring> requested;std::wstring filter;unsigned threads=0;bool raw=false,flat=false,showProgress=false;
    for(int i=2;i<argc;++i){std::wstring a=argv[i];
        if((a==L"--game"||a==L"--out"||a==L"--archive"||a==L"--filter"||a==L"--threads")&&i+1>=argc)throw Error("missing value for command option");
        if(a==L"--game")game=argv[++i];else if(a==L"--out")out=argv[++i];else if(a==L"--archive")requested.push_back(argv[++i]);
        else if(a==L"--filter")filter=argv[++i];else if(a==L"--threads")threads=std::stoul(argv[++i]);else if(a==L"--raw-compressed")raw=true;else if(a==L"--flat")flat=true;else if(a==L"--progress")showProgress=true;
        else throw Error("unknown command option: "+WideToUtf8(a));
    }
    if(game.empty())throw Error("--game folder is required");
    auto archives=FindArchives(game);
    if(command==L"scan"){
        std::wcout<<L"Found "<<archives.size()<<L" .dsPack archive(s) in "<<game<<L"\n";
        for(const auto&p:archives){try{MappedFile m(p);Pack pack(m);std::wcout<<p.filename().wstring()<<L"\t"<<pack.files.size()<<L" files\t"<<Utf8ToWide(pack.codec)<<L"\t"<<m.size/1024/1024<<L" MiB\n";}catch(const std::exception&e){std::wcout<<p.filename().wstring()<<L"\tERROR: "<<Utf8ToWide(e.what())<<L"\n";}}
        return 0;
    }
    if(command==L"extract"&&out.empty())throw Error("extract requires --out DIR");
    if(!requested.empty()){
        std::unordered_set<std::wstring> wanted;for(auto&n:requested){std::wstring k=n;for(auto&c:k)c=std::towlower(c);wanted.insert(k);}
        archives.erase(std::remove_if(archives.begin(),archives.end(),[&](const fs::path&p){std::wstring k=p.filename().wstring();for(auto&c:k)c=std::towlower(c);return !wanted.contains(k);}),archives.end());
        if(archives.size()!=wanted.size())throw Error("one or more requested archive names were not found");
    }
    if(command==L"list"){
        for(const auto&p:archives){MappedFile m(p);Pack pack(m);std::wcout<<L"\n== "<<p.filename().wstring()<<L" ==\n";for(const auto&e:pack.files){std::string name=pack.FilePath(e);if(!filter.empty()&&LowerAscii(name).find(LowerAscii(WideToUtf8(filter)))==std::string::npos)continue;std::wcout<<e.logicalSize<<L" logical  "<<e.storedSize<<L" stored  "<<Utf8ToWide(name)<<L"\n";}}
        return 0;
    }
    if(archives.empty())throw Error("no .dsPack archives selected");
    if(!flat){std::unordered_set<std::wstring> roots;for(const auto&p:archives){std::wstring key=p.stem().wstring();for(auto&c:key)c=std::towlower(c);if(!roots.insert(key).second)throw Error("selected archives share an output folder name: "+WideToUtf8(key));}}
    fs::create_directories(out);unsigned failures=0;gConsoleCancel=false;SetConsoleCtrlHandler(ConsoleHandler,TRUE);
    for(size_t i=0;i<archives.size();++i){
        std::wstring stem=archives[i].stem().wstring();fs::path dest=flat?out:out/stem;
        std::wcout<<L"["<<(i+1)<<L"/"<<archives.size()<<L"] Extracting "<<archives[i].filename().wstring()<<L" -> "<<dest<<L"\n";
        try{
            auto progress=[&](uint64_t done,uint64_t,const std::string& current){if(showProgress)std::wcout<<L"  "<<done<<L" "<<Utf8ToWide(current)<<L"\n";};
            ExtractStats stats=ExtractSingleArchive(archives[i],dest,filter,threads,raw,&gConsoleCancel,progress);
            std::wcout<<L"  extracted "<<stats.extracted<<L"; raw "<<stats.raw<<L"; unsupported skipped "<<stats.skipped<<L"\n";
            if(stats.cancelled){std::wcout<<L"  cancelled\n";++failures;break;}
            if(stats.cancelled||!stats.error.empty()){std::wcerr<<L"  FAILED: "<<Utf8ToWide(stats.error)<<L"\n";++failures;}
        }catch(const std::exception&e){std::wcerr<<L"  FAILED: "<<Utf8ToWide(e.what())<<L"\n";++failures;}
    }
    std::wcout<<L"Completed "<<(archives.size()-failures)<<L"/"<<archives.size()<<L" archives; output: "<<out<<L"\n";
    SetConsoleCtrlHandler(ConsoleHandler,FALSE);
    return failures?2:0;
}

static void PrintHelp(){
    std::wcout<<L"FEAR 3 dsPack extractor\n\n"
      <<L"  dspack_cpp.exe                 Open the graphical interface\n"
      <<L"  dspack_cpp.exe scan --game DIR\n"
      <<L"  dspack_cpp.exe list --game DIR [--archive NAME] [--filter TEXT]\n"
      <<L"  dspack_cpp.exe extract --game DIR --out DIR [--archive NAME] [options]\n\n"
      <<L"Options: --archive NAME (repeatable), --filter TEXT, --threads N, --progress,\n"
      <<L"         --raw-compressed, --flat (advanced; shared paths may overwrite)\n\n"
      <<L"Batch extraction creates one output subfolder per archive by default.\n";
}

int wmain(int argc,wchar_t** argv){
    SetConsoleOutputCP(CP_UTF8);
    try{
        if(argc>1&&(std::wstring(argv[1])==L"--help"||std::wstring(argv[1])==L"-h")){PrintHelp();return 0;}
        if(argc>1){int rc=RunModernCli(argc,argv);if(rc>=0)return rc;}
        else {
            fs::path gui=fs::path(argv[0]).parent_path()/L"dspack_gui.exe";
            if(fs::exists(gui)){SHELLEXECUTEINFOW se{};se.cbSize=sizeof(se);se.fMask=SEE_MASK_NOCLOSEPROCESS;se.lpFile=gui.c_str();se.nShow=SW_SHOWNORMAL;if(ShellExecuteExW(&se)){if(se.hProcess)CloseHandle(se.hProcess);return 0;}}
            PrintHelp();return 0;
        }
        Options options=ParseArgs(argc,argv);
        MappedFile archive(options.archive); Pack pack(archive);
        std::vector<std::pair<uint32_t,std::string>> selected;selected.reserve(pack.files.size());
        const std::string filter=LowerAscii(options.filter);
        for(uint32_t i=0;i<pack.files.size();++i){std::string name=pack.FilePath(pack.files[i]);if(filter.empty()||LowerAscii(name).find(filter)!=std::string::npos)selected.emplace_back(i,std::move(name));}
        std::wcout<<L"archive: "<<options.archive<<L" ("<<archive.size<<L" bytes)\nversion: 0x"<<std::hex<<pack.header.version<<std::dec<<L"; files: "<<pack.files.size()<<L"; dirs: "<<pack.dirs.size()<<L"\n";
        std::wcout<<L"flags: 0x"<<std::hex<<pack.header.flags<<std::dec<<L"; codec: "<<Utf8ToWide(pack.codec)<<L"; block size: "<<pack.header.blockSize<<L'\n';
        if(options.list){for(auto [idx,name]:selected){const auto&e=pack.files[idx];bool c=pack.Compressed(e);std::wcout<<e.logicalSize<<L" logical  "<<e.storedSize<<L" stored  "<<(c?L"compressed":L"stored")<<L"  "<<Utf8ToWide(name)<<L'\n';}}
        if(!options.output.empty()){
            fs::create_directories(options.output);
            std::vector<fs::path> targets(selected.size());
            std::vector<uint8_t> shouldRun(selected.size(),1);
            std::unordered_set<std::wstring> uniqueTargets;
            for(size_t i=0;i<selected.size();++i){const auto&e=pack.files[selected[i].first];bool c=pack.Compressed(e);targets[i]=SafeTarget(options.output,selected[i].second,options.raw&&c);std::wstring key=targets[i].lexically_normal().native();for(auto&ch:key)ch=std::towlower(ch);if(!uniqueTargets.insert(key).second)throw Error("duplicate output path collision: "+selected[i].second);if(c&&!options.raw&&pack.codec!="lzd"&&pack.codec!="zlib"){shouldRun[i]=0;}}
            std::atomic<size_t> next{0};std::atomic<uint64_t> extracted{0},raw{0},skipped{0},done{0};std::atomic<bool> stop{false};std::mutex errMutex;std::string error;
            unsigned workerCount=options.workers?options.workers:std::max(1u,std::thread::hardware_concurrency());workerCount=std::min<unsigned>(workerCount,32);workerCount=std::max(1u,std::min<unsigned>(workerCount,unsigned(selected.size()?selected.size():1)));
            fs::path exeDir=fs::path(argv[0]).parent_path(); std::unique_ptr<ZlibApi> zlib;
            if(pack.codec=="zlib")zlib=std::make_unique<ZlibApi>(exeDir/L"dspack_cpp_runtime");
            auto worker=[&]{while(!stop.load(std::memory_order_relaxed)){size_t i=next.fetch_add(1);if(i>=selected.size())break;try{
                uint32_t idx=selected[i].first;const auto&e=pack.files[idx];bool compressed=pack.Compressed(e);
                if(!shouldRun[i]){++skipped;++done;continue;}
                const uint8_t* input=archive.data+e.offset;size_t inputSize=e.storedSize;std::vector<uint8_t> decoded;
                if(compressed&&!options.raw){if(pack.codec=="lzd")decoded=LzdResource(input,inputSize,e.logicalSize);else if(pack.codec=="zlib")decoded=ZlibResource(input,inputSize,e.logicalSize,*zlib);else throw Error("unsupported compressed codec");input=decoded.data();inputSize=decoded.size();}
                fs::create_directories(targets[i].parent_path());std::ofstream out(targets[i],std::ios::binary|std::ios::trunc);if(!out)throw Error("cannot open output file");out.write(reinterpret_cast<const char*>(input),std::streamsize(inputSize));if(!out)throw Error("write failed");
                if(compressed&&options.raw)++raw;else ++extracted;++done;
            }catch(const std::exception&ex){std::lock_guard lock(errMutex);if(error.empty())error=selected[i].second+": "+ex.what();stop=true;}}};
            std::vector<std::thread> pool;for(unsigned i=0;i<workerCount;++i)pool.emplace_back(worker);for(auto&t:pool)t.join();
            std::wcout<<L"workers: "<<workerCount<<L"; extracted: "<<extracted<<L"; raw compressed: "<<raw<<L"; skipped unsupported: "<<skipped<<L"; processed: "<<done<<L"/"<<selected.size()<<L'\n';
            if(!error.empty())throw Error(error);
        }
        return 0;
    }catch(const std::exception&ex){std::cerr<<"error: "<<ex.what()<<'\n';return 2;}
}
#endif
