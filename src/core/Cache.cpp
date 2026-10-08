#include "sg/core/Cache.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sg {

namespace {
constexpr uint64_t kP1 = 0x9e3779b185ebca87ULL, kP2 = 0xc2b2ae3d27d4eb4fULL, kP3 = 0x165667b19e3779f9ULL;
uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
uint64_t fmix(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}
}  // namespace

std::string Digest::hex() const {
    char b[33];
    std::snprintf(b, sizeof b, "%016llx%016llx", static_cast<unsigned long long>(hi), static_cast<unsigned long long>(lo));
    return b;
}

void Hasher::word(uint64_t w) {
    // Two lanes, each mixing every word its own way.
    a_ = rotl((a_ ^ w) * kP1, 31) * kP2;
    b_ = rotl((b_ + w) * kP3, 27) * kP1 ^ a_;
    ++n_;
}

Hasher& Hasher::bytes(const void* data, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        word(w);
    }
    uint64_t tail = 0;
    for (std::size_t k = 0; i + k < n; ++k) tail |= uint64_t(p[i + k]) << (8 * k);
    word(tail ^ (uint64_t(n & 7) << 59));
    word(uint64_t(n));
    return *this;
}

Hasher& Hasher::text(std::string_view s) { return bytes(s.data(), s.size()); }

Hasher& Hasher::integer(int64_t v) {
    word(uint64_t(v));
    word(0x1);
    return *this;
}

Hasher& Hasher::number(double v) {
    uint64_t w;
    std::memcpy(&w, &v, 8);
    word(w);
    word(0x2);
    return *this;
}

Digest Hasher::digest() const {
    const uint64_t a = fmix(a_ ^ n_ * kP3), b = fmix(b_ + a);
    return Digest{a, b};
}

namespace cache {

namespace {
struct Shelf {
    std::mutex m;
    std::string dir;
    Stats stats;
};
Shelf& shelf() {
    static Shelf s;
    return s;
}
constexpr char kMagic[8] = {'S', 'G', 'C', 'A', 'C', 'H', 'E', '1'};
constexpr std::size_t kHead = 8 + 16 + 8 + 16;  // magic, key, size, the bytes' own digest

std::filesystem::path path_of(const std::string& dir, const std::string& kind, const Digest& key) {
    return std::filesystem::path(dir) / kind / (key.hex() + ".bin");
}
void count(std::size_t Stats::*which) {
    std::lock_guard<std::mutex> g(shelf().m);
    ++(shelf().stats.*which);
}
void put64(std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s.push_back(char((v >> (8 * i)) & 0xff));
}
uint64_t get64(const char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(static_cast<unsigned char>(p[i])) << (8 * i);
    return v;
}
unsigned long process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}
// Put `from` where `to` is, replacing it if it is there, in one step.
bool replace(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.wstring().c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}
}  // namespace

void set_folder(const std::string& dir) {
    std::lock_guard<std::mutex> g(shelf().m);
    shelf().dir = dir;
}

std::string folder() {
    std::lock_guard<std::mutex> g(shelf().m);
    return shelf().dir;
}

Stats stats() {
    std::lock_guard<std::mutex> g(shelf().m);
    return shelf().stats;
}

unsigned hands() {
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 2 ? cores - 1 : 1;
}

void background_priority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

namespace {
// A kept file: its head, then its bytes, each read whole in one go. False if
// it is not there, or not as long as its head says.
bool read_kept(const std::filesystem::path& p, char (&head)[kHead], std::string& bytes, bool& there) {
    there = false;
#ifdef _WIN32
    const HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    there = true;
    const auto take = [&](char* to, std::size_t n) {
        for (std::size_t at = 0; at < n;) {
            DWORD got = 0;
            const DWORD want = static_cast<DWORD>(std::min<std::size_t>(n - at, std::size_t(1) << 30));
            if (!ReadFile(f, to + at, want, &got, nullptr) || got == 0) return false;
            at += got;
        }
        return true;
    };
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && uint64_t(size.QuadPart) >= kHead && take(head, kHead);
    const uint64_t n = ok ? get64(head + 24) : 0;
    ok = ok && n <= (uint64_t(1) << 34) && n == uint64_t(size.QuadPart) - kHead;
    if (ok) {
        bytes.resize(static_cast<std::size_t>(n));
        ok = take(bytes.data(), bytes.size());
    }
    CloseHandle(f);
    return ok;
#else
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return false;
    there = true;
    const std::streamoff size = in.tellg();
    in.seekg(0);
    if (size < std::streamoff(kHead) || !in.read(head, kHead)) return false;
    const uint64_t n = get64(head + 24);
    if (n > (uint64_t(1) << 34) || n != uint64_t(size) - kHead) return false;
    bytes.resize(static_cast<std::size_t>(n));
    in.read(bytes.data(), std::streamsize(n));
    return in.gcount() == std::streamsize(n);
#endif
}
}  // namespace

bool load(const std::string& kind, const Digest& key, std::string& out) {
    const std::string dir = folder();
    if (dir.empty()) return false;
    char head[kHead];
    std::string bytes;
    bool there = false;
    const bool read = read_kept(path_of(dir, kind, key), head, bytes, there);
    if (!there) {
        count(&Stats::misses);
        return false;
    }
    const bool headed = read && std::memcmp(head, kMagic, 8) == 0 && get64(head + 8) == key.hi && get64(head + 16) == key.lo;
    if (!headed) {
        count(&Stats::rejected);
        return false;
    }
    const Digest own = Hasher{}.text(bytes).digest();
    if (own.hi != get64(head + 32) || own.lo != get64(head + 40)) {
        count(&Stats::rejected);
        return false;
    }
    out = std::move(bytes);
    count(&Stats::hits);
    return true;
}

bool store(const std::string& kind, const Digest& key, std::string_view bytes) {
    const std::string dir = folder();
    if (dir.empty()) return false;
    namespace fs = std::filesystem;
    const fs::path to = path_of(dir, kind, key);
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    static std::atomic<unsigned> serial{0};
    const fs::path aside = to.parent_path() / (key.hex() + "." + std::to_string(process_id()) + "." +
                                               std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffffff) + "." +
                                               std::to_string(serial++) + ".tmp");
    {
        std::string head(kMagic, 8);
        put64(head, key.hi), put64(head, key.lo), put64(head, bytes.size());
        const Digest own = Hasher{}.text(bytes).digest();
        put64(head, own.hi), put64(head, own.lo);
        std::ofstream f(aside, std::ios::binary | std::ios::trunc);
        f.write(head.data(), std::streamsize(head.size()));
        f.write(bytes.data(), std::streamsize(bytes.size()));
        f.close();
        if (!f) {
            fs::remove(aside, ec);
            return false;
        }
    }
    // Another program may have put the same bytes there first (the key says
    // what they are): either way what is there is whole.
    if (!replace(aside, to)) {
        fs::remove(aside, ec);
        return fs::exists(to, ec);
    }
    count(&Stats::stored);
    return true;
}

}  // namespace cache

}  // namespace sg
