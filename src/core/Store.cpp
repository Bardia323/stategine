#include "sg/core/Store.hpp"

#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sg {

long long file_stamp(const std::filesystem::path& file) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &d)) return 0;
    return static_cast<long long>((uint64_t(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime);
#else
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(file, ec);
    return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
#endif
}

const std::string& TextStore::bind(const std::string& key, const std::filesystem::path& file, const std::string& initial) {
    Entry& e = entries_[key];
    const bool again = e.file == file;
    if (!again) order_.push_back(key);
    e.file = file;
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        // Bound to it already, and its time stamp has not moved since it was
        // last read or written: what is kept is what it says.
        if (again && e.stamp != std::filesystem::file_time_type{}) {
            const auto stamp = std::filesystem::last_write_time(file, ec);
            if (!ec && stamp == e.stamp) return e.text;
        }
        e.text = read(file);
        e.stamp = std::filesystem::last_write_time(file, ec);
    } else {
        e.text = initial;
        write(e);
    }
    return e.text;
}

const std::string& TextStore::bind_read(const std::string& key, const std::filesystem::path& file, std::string text) {
    Entry& e = entries_[key];
    if (e.file != file) order_.push_back(key);
    e.file = file;
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    e.text = std::move(text);
    std::error_code ec;
    e.stamp = std::filesystem::last_write_time(file, ec);
    return e.text;
}

const std::string& TextStore::bind(const std::string& key, const Assets& assets, const std::string& owner,
                                    const std::string& name, const std::string& initial) {
    return bind(key, assets.file(owner, name), initial);
}

const std::string* TextStore::get(const std::string& key) const {
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second.text;
}

std::filesystem::path TextStore::file_of(const std::string& key) const {
    auto it = entries_.find(key);
    return it == entries_.end() ? std::filesystem::path{} : it->second.file;
}

void TextStore::put(const std::string& key, const std::string& text) {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.text == text) return;
    it->second.text = text;
    write(it->second);
}

std::vector<std::string> TextStore::poll(double now, double interval, int budget) {
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

std::string TextStore::read(const std::filesystem::path& file) {
    // (Whole, by its size, in one go.)
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    std::string text;
    if (!in) return text;
    const std::streamoff n = in.tellg();
    in.seekg(0);
    if (n > 0) {
        text.resize(static_cast<std::size_t>(n));
        in.read(text.data(), n);
        text.resize(static_cast<std::size_t>(in.gcount()));
    } else {
        std::ostringstream s;
        s << in.rdbuf();
        text = s.str();
    }
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

void TextStore::write(Entry& e) {
    std::error_code ec;
    if (e.file.has_parent_path()) std::filesystem::create_directories(e.file.parent_path(), ec);
    {
        std::ofstream out(e.file, std::ios::binary | std::ios::trunc);
        out << e.text;
    }
    e.stamp = std::filesystem::last_write_time(e.file, ec);
}

}  // namespace sg
