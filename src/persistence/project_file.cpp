#include "persistence/project_file.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "core/ids.h"

namespace persistence {
namespace {

using namespace core;

// ---------------------------------------------------------------------------
// Layout
//
//   magic[8]        FCS_APP_ID, zero-padded. Never a product name.
//   u32 version     kFormatVersion
//   u64 payload_len
//   u32 payload_crc CRC-32 (IEEE) of the payload
//   payload         the Project, little-endian, length-prefixed strings
//
// The CRC is what turns a truncated or bit-rotted file into a clean error
// instead of a silently wrong Project.

constexpr std::size_t kMagicSize = 8;
constexpr std::size_t kHeaderSize = kMagicSize + 4 + 8 + 4;

std::array<std::byte, kMagicSize> file_magic() {
    std::array<std::byte, kMagicSize> magic{};
    const char* app_id = FCS_APP_ID_STR;
    for (std::size_t i = 0; i < kMagicSize && app_id[i] != '\0'; ++i) {
        magic[i] = static_cast<std::byte>(app_id[i]);
    }
    return magic;
}

std::uint32_t crc32(std::span<const std::byte> bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::byte b : bytes) {
        crc = table[(crc ^ static_cast<std::uint8_t>(b)) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Writer

class Writer {
public:
    std::vector<std::byte> out;

    void u8(std::uint8_t v) { out.push_back(static_cast<std::byte>(v)); }
    void u32(std::uint32_t v) { little_endian(v); }
    void i32(std::int32_t v) { little_endian(static_cast<std::uint32_t>(v)); }
    void u64(std::uint64_t v) { little_endian(v); }
    void i64(std::int64_t v) { little_endian(static_cast<std::uint64_t>(v)); }
    void f32(float v) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        little_endian(bits);
    }
    void f64(double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        little_endian(bits);
    }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (char c : s) out.push_back(static_cast<std::byte>(c));
    }
    void id(const Id& v) {
        u64(v.hi);
        u64(v.lo);
    }
    void count(std::size_t n) { u32(static_cast<std::uint32_t>(n)); }

private:
    template <typename U>
    void little_endian(U v) {
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFu));
        }
    }
};

void write_provenance(Writer& w, const Provenance& p) {
    w.str(p.generated_by);
    w.i64(p.generated_at);
}

void write_colour(Writer& w, const std::optional<Colour>& c) {
    w.u8(c ? 1 : 0);
    w.u32(c ? *c : 0);
}

void write_effects(Writer& w, const std::vector<Effect>& chain) {
    w.count(chain.size());
    for (const Effect& e : chain) {
        w.id(e.id);
        w.u8(static_cast<std::uint8_t>(e.type));
        w.u8(e.enabled ? 1 : 0);
        w.count(e.params.size());
        for (const auto& [name, value] : e.params) {
            w.str(name);
            w.f32(value);
        }
    }
}

void write_sample(Writer& w, const Sample& s) {
    w.id(s.id);
    w.str(s.name);
    write_provenance(w, s.provenance);
    w.u8(s.source ? 1 : 0);
    if (s.source) {
        w.f64(s.source->sample_rate);
        w.i32(s.source->channels);
        w.u64(s.source->frames.size());
        for (float f : s.source->frames) w.f32(f);
    }
}

void write_instrument(Writer& w, const Instrument& i) {
    w.id(i.id);
    w.str(i.name);
    w.id(i.params.sample);
    w.u8(i.params.root_pitch);
    w.i64(i.params.start_offset);
    w.i64(i.params.end_offset);
    w.u8(static_cast<std::uint8_t>(i.params.mode));
    w.f32(i.params.gain);
    w.f32(i.params.pan);
    write_effects(w, i.chain);
}

void write_pattern(Writer& w, const Pattern& p) {
    w.id(p.id);
    w.str(p.name);
    w.i64(p.length);
    write_provenance(w, p.provenance);
    write_colour(w, p.colour);
    w.count(p.parts.size());
    for (const Part& part : p.parts) {
        w.id(part.id);
        w.id(part.instrument);
        w.u8(part.muted ? 1 : 0);
        w.count(part.events.size());
        for (const Event& e : part.events) {
            w.id(e.id);
            w.i64(e.start);
            w.i64(e.duration);
            w.u8(e.pitch);
            w.u8(e.velocity);
        }
    }
}

void write_arrangement(Writer& w, const Arrangement& a) {
    w.id(a.id);
    w.str(a.name);
    w.count(a.tracks.size());
    for (const Track& t : a.tracks) {
        w.id(t.id);
        w.str(t.name);
        w.u8(t.muted ? 1 : 0);
        write_colour(w, t.colour);
        w.count(t.placements.size());
        for (const Placement& p : t.placements) {
            w.id(p.id);
            w.id(p.pattern);
            w.i64(p.start);
            w.i64(p.length);
            w.u8(p.muted ? 1 : 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Reader — every read is bounds-checked, so a hostile or damaged file can
// only ever produce a FormatError, never an out-of-range read or a giant
// allocation driven by a forged count.

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool at_end() const { return pos_ == bytes_.size(); }
    std::size_t remaining() const { return bytes_.size() - pos_; }

    std::uint8_t u8() { return static_cast<std::uint8_t>(take(1)[0]); }
    std::uint32_t u32() { return little_endian<std::uint32_t>(); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::uint64_t u64() { return little_endian<std::uint64_t>(); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    float f32() {
        std::uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    double f64() {
        std::uint64_t bits = u64();
        double v;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
    std::string str() {
        std::uint32_t n = u32();
        std::span<const std::byte> s = take(n);
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    }
    Id id() {
        Id v;
        v.hi = u64();
        v.lo = u64();
        return v;
    }
    bool flag() {
        std::uint8_t v = u8();
        if (v > 1) throw FormatError("flag byte out of range");
        return v == 1;
    }
    // An element count, checked against the bytes left so that a forged count
    // cannot drive a huge allocation. `min_bytes` is each element's floor.
    std::size_t count(std::size_t min_bytes) {
        std::uint32_t n = u32();
        if (min_bytes > 0 && n > remaining() / min_bytes) {
            throw FormatError("element count exceeds file size");
        }
        return n;
    }
    std::span<const std::byte> take(std::size_t n) {
        if (n > remaining()) throw FormatError("unexpected end of data");
        std::span<const std::byte> s = bytes_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

private:
    template <typename U>
    U little_endian() {
        std::span<const std::byte> s = take(sizeof(U));
        U v = 0;
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            v |= static_cast<U>(static_cast<std::uint8_t>(s[i])) << (8 * i);
        }
        return v;
    }

    std::span<const std::byte> bytes_;
    std::size_t pos_ = 0;
};

Pitch read_pitch(Reader& r) {
    std::uint8_t v = r.u8();
    if (v > 127) throw FormatError("pitch out of range");
    return v;
}

Velocity read_velocity(Reader& r) {
    std::uint8_t v = r.u8();
    if (v > 127) throw FormatError("velocity out of range");
    return v;
}

Provenance read_provenance(Reader& r) {
    Provenance p;
    p.generated_by = r.str();
    p.generated_at = r.i64();
    return p;
}

std::optional<Colour> read_colour(Reader& r) {
    bool has = r.flag();
    Colour c = r.u32();
    return has ? std::optional<Colour>(c) : std::nullopt;
}

std::vector<Effect> read_effects(Reader& r) {
    std::vector<Effect> chain;
    std::size_t n = r.count(16 + 1 + 1 + 4);
    for (std::size_t i = 0; i < n; ++i) {
        Effect e;
        e.id = r.id();
        std::uint8_t type = r.u8();
        if (type > static_cast<std::uint8_t>(EffectType::Distortion)) {
            throw FormatError("unknown Effect type");
        }
        e.type = static_cast<EffectType>(type);
        e.enabled = r.flag();
        std::size_t params = r.count(4 + 4);
        for (std::size_t k = 0; k < params; ++k) {
            std::string name = r.str();
            e.params[name] = r.f32();
        }
        chain.push_back(std::move(e));
    }
    return chain;
}

Sample read_sample(Reader& r) {
    Sample s;
    s.id = r.id();
    s.name = r.str();
    s.provenance = read_provenance(r);
    if (r.flag()) {
        auto audio = std::make_shared<AudioData>();
        audio->sample_rate = r.f64();
        audio->channels = r.i32();
        if (audio->channels < 0) throw FormatError("negative channel count");
        std::uint64_t frames = r.u64();
        if (frames > r.remaining() / sizeof(float)) {
            throw FormatError("audio length exceeds file size");
        }
        audio->frames.resize(static_cast<std::size_t>(frames));
        for (float& f : audio->frames) f = r.f32();
        s.source = std::move(audio);
    }
    return s;
}

Instrument read_instrument(Reader& r) {
    Instrument i;
    i.id = r.id();
    i.name = r.str();
    i.params.sample = r.id();
    i.params.root_pitch = read_pitch(r);
    i.params.start_offset = r.i64();
    i.params.end_offset = r.i64();
    std::uint8_t mode = r.u8();
    if (mode > static_cast<std::uint8_t>(SamplerMode::Sustain)) {
        throw FormatError("unknown Sampler mode");
    }
    i.params.mode = static_cast<SamplerMode>(mode);
    i.params.gain = r.f32();
    i.params.pan = r.f32();
    i.chain = read_effects(r);
    return i;
}

Pattern read_pattern(Reader& r) {
    Pattern p;
    p.id = r.id();
    p.name = r.str();
    p.length = r.i64();
    p.provenance = read_provenance(r);
    p.colour = read_colour(r);
    std::size_t parts = r.count(16 + 16 + 1 + 4);
    for (std::size_t i = 0; i < parts; ++i) {
        Part part;
        part.id = r.id();
        part.instrument = r.id();
        part.muted = r.flag();
        std::size_t events = r.count(16 + 8 + 8 + 1 + 1);
        for (std::size_t k = 0; k < events; ++k) {
            Event e;
            e.id = r.id();
            e.start = r.i64();
            e.duration = r.i64();
            e.pitch = read_pitch(r);
            e.velocity = read_velocity(r);
            part.events.push_back(e);
        }
        p.parts.push_back(std::move(part));
    }
    return p;
}

Arrangement read_arrangement(Reader& r) {
    Arrangement a;
    a.id = r.id();
    a.name = r.str();
    std::size_t tracks = r.count(16 + 4 + 1 + 5 + 4);
    for (std::size_t i = 0; i < tracks; ++i) {
        Track t;
        t.id = r.id();
        t.name = r.str();
        t.muted = r.flag();
        t.colour = read_colour(r);
        std::size_t placements = r.count(16 + 16 + 8 + 8 + 1);
        for (std::size_t k = 0; k < placements; ++k) {
            Placement p;
            p.id = r.id();
            p.pattern = r.id();
            p.start = r.i64();
            p.length = r.i64();
            p.muted = r.flag();
            t.placements.push_back(p);
        }
        a.tracks.push_back(std::move(t));
    }
    return a;
}

Project read_project(Reader& r) {
    Project p;
    p.format_version = static_cast<int>(kFormatVersion);
    p.id = r.id();
    p.title = r.str();
    p.tempo = r.f64();
    p.time_signature.first = r.i32();
    p.time_signature.second = r.i32();

    std::size_t n = r.count(16 + 4 + 4 + 8 + 1);
    for (std::size_t i = 0; i < n; ++i) p.samples.items.push_back(read_sample(r));
    n = r.count(16 + 4 + 16 + 1 + 8 + 8 + 1 + 4 + 4 + 4);
    for (std::size_t i = 0; i < n; ++i) p.instruments.items.push_back(read_instrument(r));
    n = r.count(16 + 4 + 8 + 4 + 8 + 5 + 4);
    for (std::size_t i = 0; i < n; ++i) p.patterns.items.push_back(read_pattern(r));
    n = r.count(16 + 4 + 4);
    for (std::size_t i = 0; i < n; ++i) p.arrangements.items.push_back(read_arrangement(r));
    p.master_chain = read_effects(r);

    if (!r.at_end()) throw FormatError("trailing bytes after Project");
    return p;
}

// ---------------------------------------------------------------------------
// Disk

struct FileCloser {
    void operator()(std::FILE* f) const {
        if (f) std::fclose(f);
    }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

File open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wmode(mode, mode + std::strlen(mode));
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), wmode.c_str()) != 0) return File(nullptr);
    return File(f);
#else
    return File(std::fopen(path.c_str(), mode));
#endif
}

// Writes and flushes all the way to the device, so a power loss after
// save_project returns cannot lose the bytes the rename is about to publish.
bool write_fully(const std::filesystem::path& path, std::span<const std::byte> bytes,
                 std::string& error) {
    File f = open_file(path, "wb");
    if (!f) {
        error = "cannot create " + path.string();
        return false;
    }
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), f.get()) != bytes.size()) {
        error = "short write to " + path.string();
        return false;
    }
    if (std::fflush(f.get()) != 0) {
        error = "flush failed for " + path.string();
        return false;
    }
#ifdef _WIN32
    if (_commit(_fileno(f.get())) != 0) {
#else
    if (::fsync(fileno(f.get())) != 0) {
#endif
        error = "sync to disk failed for " + path.string();
        return false;
    }
    return true;
}

bool read_fully(const std::filesystem::path& path, std::vector<std::byte>& bytes,
                std::string& error) {
    File f = open_file(path, "rb");
    if (!f) {
        error = "cannot open " + path.string();
        return false;
    }
    std::array<std::byte, 64 * 1024> chunk;
    for (;;) {
        std::size_t n = std::fread(chunk.data(), 1, chunk.size(), f.get());
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n));
        if (n < chunk.size()) break;
    }
    if (std::ferror(f.get())) {
        error = "read error on " + path.string();
        return false;
    }
    return true;
}

#ifndef _WIN32
// A rename is durable only once the directory entry is on disk too.
void sync_directory(const std::filesystem::path& dir) {
    int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
}
#endif

}  // namespace

std::filesystem::path previous_version_path(const std::filesystem::path& file) {
    std::filesystem::path p = file;
    p += ".previous";
    return p;
}

std::vector<std::byte> encode(const Project& project) {
    Writer body;
    body.id(project.id);
    body.str(project.title);
    body.f64(project.tempo);
    body.i32(project.time_signature.first);
    body.i32(project.time_signature.second);
    body.count(project.samples.items.size());
    for (const Sample& s : project.samples.items) write_sample(body, s);
    body.count(project.instruments.items.size());
    for (const Instrument& i : project.instruments.items) write_instrument(body, i);
    body.count(project.patterns.items.size());
    for (const Pattern& p : project.patterns.items) write_pattern(body, p);
    body.count(project.arrangements.items.size());
    for (const Arrangement& a : project.arrangements.items) write_arrangement(body, a);
    write_effects(body, project.master_chain);

    Writer file;
    for (std::byte b : file_magic()) file.out.push_back(b);
    file.u32(kFormatVersion);
    file.u64(body.out.size());
    file.u32(crc32(body.out));
    file.out.insert(file.out.end(), body.out.begin(), body.out.end());
    return std::move(file.out);
}

LoadResult decode(std::span<const std::byte> bytes) {
    LoadResult result;
    try {
        Reader header(bytes);
        std::span<const std::byte> magic = header.take(kMagicSize);
        if (!std::equal(magic.begin(), magic.end(), file_magic().begin())) {
            throw FormatError("not a Project file");
        }
        std::uint32_t version = header.u32();
        if (version == 0 || version > kFormatVersion) {
            throw FormatError("Project file version " + std::to_string(version) +
                              " is newer than this Studio understands (" +
                              std::to_string(kFormatVersion) + ")");
        }
        std::uint64_t length = header.u64();
        std::uint32_t crc = header.u32();
        if (length != header.remaining()) throw FormatError("file is truncated");
        std::span<const std::byte> payload = header.take(static_cast<std::size_t>(length));
        if (crc32(payload) != crc) throw FormatError("file is damaged (checksum mismatch)");

        Reader body(payload);
        result.project = read_project(body);
    } catch (const FormatError& e) {
        result.error = e.what();
    } catch (const std::bad_alloc&) {
        result.error = "out of memory while reading the Project";
    }
    return result;
}

SaveResult save_project(const Project& project, const std::filesystem::path& file) {
    namespace fs = std::filesystem;
    SaveResult result;
    std::error_code ec;

    std::vector<std::byte> bytes = encode(project);

    // A unique temporary sibling: same directory, so the final rename is a
    // metadata operation within one volume and therefore atomic.
    fs::path temp = file;
    temp += ".tmp-" + to_string(new_id());
    struct TempCleanup {
        const fs::path& path;
        ~TempCleanup() {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    } cleanup{temp};

    if (!write_fully(temp, bytes, result.error)) return result;

    // Prove the bytes on disk load before they replace anything.
    std::vector<std::byte> readback;
    if (!read_fully(temp, readback, result.error)) return result;
    LoadResult verified = decode(readback);
    if (!verified.ok()) {
        result.error = "written file failed verification: " + verified.error;
        return result;
    }

    // Retain the previous good version before publishing the new one. Copy
    // rather than rename, so the target is a complete file at every instant.
    // Only a file that still loads is retained: copying a damaged current
    // file would overwrite the last good previous version with damage, which
    // is exactly the loss the retained version exists to prevent.
    if (fs::exists(file, ec) && load_project(file).ok()) {
        if (!fs::copy_file(file, previous_version_path(file), fs::copy_options::overwrite_existing,
                           ec)) {
            result.error = "cannot retain previous version: " + ec.message();
            return result;
        }
    }

    fs::rename(temp, file, ec);
    if (ec) {
        result.error = "cannot replace " + file.string() + ": " + ec.message();
        return result;
    }
#ifndef _WIN32
    sync_directory(file.has_parent_path() ? file.parent_path() : fs::path("."));
#endif

    result.ok = true;
    return result;
}

LoadResult load_project(const std::filesystem::path& file) {
    LoadResult result;
    std::vector<std::byte> bytes;
    if (!read_fully(file, bytes, result.error)) return result;
    if (bytes.size() < kHeaderSize) {
        result.error = "file is too short to be a Project";
        return result;
    }
    return decode(bytes);
}

}  // namespace persistence
