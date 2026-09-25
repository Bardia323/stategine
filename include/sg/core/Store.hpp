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

namespace sg {

class TextStore {
public:
    // Keep `key` in `file`: what is there if there is one (the file wins -
    // it may have been written by hand), else `initial`, written there.
    const std::string& bind(const std::string& key, const std::filesystem::path& file, const std::string& initial = {}) {
        Entry& e = entries_[key];
        if (e.file != file) order_.push_back(key);
        e.file = file;
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            e.text = read(file);
            e.stamp = std::filesystem::last_write_time(file, ec);
        } else {
            e.text = initial;
            write(e);
        }
        return e.text;
    }

    bool has(const std::string& key) const { return entries_.count(key) != 0; }
    const std::string* get(const std::string& key) const {
        auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : &it->second.text;
    }
    std::filesystem::path file_of(const std::string& key) const {
        auto it = entries_.find(key);
        return it == entries_.end() ? std::filesystem::path{} : it->second.file;
    }

    // Change what `key` says; its file is written only if it differs.
    void put(const std::string& key, const std::string& text) {
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.text == text) return;
        it->second.text = text;
        write(it->second);
    }

    void forget(const std::string& key) { entries_.erase(key); }

    // The keys whose files changed on disk since they were last read (now
    // read again). `now` is any clock in seconds; each file is looked at no
    // more often than every `interval`, and no more than `budget` a call.
    std::vector<std::string> poll(double now, double interval = 0.5, int budget = 16) {
        std::vector<std::string> changed;
        if (order_.empty()) return changed;
        for (int looked = 0, n = static_cast<int>(order_.size()); looked < std::min(budget, n); ++looked) {
            next_ %= order_.size();
            const std::string key = order_[next_++];
            auto it = entries_.find(key);
            if (it == entries_.end()) continue;
            Entry& e = it->second;
            if (now - e.checked < interval) continue;
            e.checked = now;
            std::error_code ec;
            const auto stamp = std::filesystem::last_write_time(e.file, ec);
            if (ec || stamp == e.stamp) continue;
            e.stamp = stamp;
            std::string text = read(e.file);
            if (text == e.text) continue;
            e.text = std::move(text);
            changed.push_back(key);
        }
        // Keys forgotten or bound twice drop out of the rota.
        if (order_.size() > entries_.size() * 2 + 8) {
            std::vector<std::string> keep;
            for (const auto& [k, e] : entries_) keep.push_back(k);
            order_ = std::move(keep);
            next_ = 0;
        }
        return changed;
    }

private:
    struct Entry {
        std::filesystem::path file;
        std::string text;
        std::filesystem::file_time_type stamp{};
        double checked = -1e9;
    };

    // Read as it is, but for line ends: a file edited on Windows ends its
    // lines with a carriage return too, and the text is the same text.
    static std::string read(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        std::string text = s.str();
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    void write(Entry& e) {
        std::error_code ec;
        if (e.file.has_parent_path()) std::filesystem::create_directories(e.file.parent_path(), ec);
        {
            std::ofstream out(e.file, std::ios::binary | std::ios::trunc);
            out << e.text;
        }
        e.stamp = std::filesystem::last_write_time(e.file, ec);
    }

    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> order_;
    std::size_t next_ = 0;
};

}  // namespace sg
