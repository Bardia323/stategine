// Stategine - Cache: derived data kept on disk, by what it was made from.
//
// Some of what a state shows is costly to make and a pure function of its
// data: a recipe's mesh, a painting's pixels. Made once, it can be kept: under
// a digest of everything it was made from (the text, the sizes, the version of
// the code that made it), so the same inputs read back the same bytes, and any
// other inputs are another digest - a miss, made again.
//
// It is never a source of truth. No folder, a missing file, a damaged one, a
// file of another key: each is a miss, and the caller makes the thing as if
// there were no cache. Deleting the folder changes nothing but time. Files are
// written whole to a temporary name and renamed into place, so two programs
// sharing the folder (the game and a test) never read half a file.
//
//   sg::Digest key = sg::Hasher{}.text("paint").text(strokes).integer(w).digest();
//   std::string bytes;
//   if (!sg::cache::load("paint", key, bytes)) { bytes = make(); sg::cache::store("paint", key, bytes); }
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sg {

// 128 bits of what something was made from.
struct Digest {
    uint64_t hi = 0, lo = 0;
    std::string hex() const;
    bool operator==(const Digest& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const Digest& o) const { return !(*this == o); }
};

// A digest made by feeding it, in order; each piece says its length, so
// "ab" + "c" is not "a" + "bc".
class Hasher {
public:
    Hasher& bytes(const void* data, std::size_t n);
    Hasher& text(std::string_view s);
    Hasher& integer(int64_t v);
    Hasher& number(double v);  // its exact bits
    Digest digest() const;

private:
    uint64_t a_ = 0x9e3779b97f4a7c15ULL, b_ = 0xc2b2ae3d27d4eb4fULL, n_ = 0;
    void word(uint64_t w);
};

namespace cache {

// The folder kept in (each kind a folder in it); empty, nothing is kept.
void set_folder(const std::string& dir);
std::string folder();

// The bytes kept for `key`, if they are there and whole.
bool load(const std::string& kind, const Digest& key, std::string& out);
// Keep them (atomically: written aside, then renamed into place). False if
// nothing could be kept - which is only time lost.
bool store(const std::string& kind, const Digest& key, std::string_view bytes);

struct Stats {
    std::size_t hits = 0, misses = 0, stored = 0, rejected = 0;  // rejected: there, but damaged or of another key
};
Stats stats();

// Work that fills a cache on many cores at once (painting, building meshes):
// how many hands to use - every core but one, so the machine stays its
// user's - and the calling thread lowered below normal priority for it.
unsigned hands();
void background_priority();

}  // namespace cache

}  // namespace sg
