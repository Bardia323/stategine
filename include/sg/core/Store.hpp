// Stategine - Store: texts kept in files, cached.
//
// What a thing says can live apart from the thing: a sheet's words in a file
// beside the room it lies in, a room's source (sg::to_text) in a file of its
// own. A TextStore keeps each text under a key, bound to a file, and keeps them
// in step both ways without going to the disk when it need not:
//
//   - a text is read once, when it is bound, and then served from memory;
//   - `put` writes a file only when the text has changed;
//   - `poll` notices a file changed from outside - by hand, by another
//     program - by its time stamp alone, a few files a call, each at most
//     every `interval` seconds, and only then reads it again.
//
//   sg::TextStore store;
//   store.bind("paper.3", "papers/paper.3.txt", "what it said when made");
//   for (const std::string& key : store.poll(now)) show(key, *store.get(key));
//   store.put("paper.3", "new words");     // written, as they differ
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "sg/core/Assets.hpp"

namespace sg {

// When a file was last written, as a number that moves when it is written
// again - asked of the file system once, without opening the file - or 0 if
// it is not there. Only for comparing with itself.
long long file_stamp(const std::filesystem::path& file);

class TextStore {
public:
    // Keep `key` in `file`: what is there if there is one (the file wins -
    // it may have been written by hand), else `initial`, written there.
    const std::string& bind(const std::string& key, const std::filesystem::path& file, const std::string& initial = {});

    // Keep `key` in `file`, whose bytes the caller has just read whole
    // (`text`, as the file has them): what `bind` would keep for a file that
    // is there, without reading it a second time.
    const std::string& bind_read(const std::string& key, const std::filesystem::path& file, std::string text);

    // The same, in the folder `owner` keeps its files in (sg::Assets).
    const std::string& bind(const std::string& key, const Assets& assets, const std::string& owner,
                            const std::string& name, const std::string& initial = {});

    bool has(const std::string& key) const { return entries_.count(key) != 0; }
    const std::string* get(const std::string& key) const;
    std::filesystem::path file_of(const std::string& key) const;

    // Change what `key` says; its file is written only if it differs.
    void put(const std::string& key, const std::string& text);

    void forget(const std::string& key) { entries_.erase(key); }

    // The keys whose files changed on disk since they were last read (now
    // read again). `now` is any clock in seconds; each file is looked at no
    // more often than every `interval`, and no more than `budget` a call.
    std::vector<std::string> poll(double now, double interval = 0.5, int budget = 16);

private:
    struct Entry {
        std::filesystem::path file;
        std::string text;
        std::filesystem::file_time_type stamp{};
        double checked = -1e9;
    };

    // Read as it is, but for line ends: a file edited on Windows ends its
    // lines with a carriage return too, and the text is the same text.
    static std::string read(const std::filesystem::path& file);

    void write(Entry& e);

    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> order_;
    std::size_t next_ = 0;
};

}  // namespace sg
