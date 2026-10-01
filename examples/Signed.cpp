#include "Signed.hpp"
#include "../src/net/Canonical.hpp"
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace sg::examples {
net::Committee read_committee(const std::filesystem::path& folder) {
    std::ifstream in(folder/"committee.txt"); std::string version; net::Committee c;
    if (!(in >> version >> c.region >> c.generation >> c.quorum >> c.faults) || version != "sg-net-committee-v1") throw std::runtime_error("invalid regional committee manifest");
    std::string peer,key;
    while (in >> peer) {
        if (!(in >> key)) throw std::runtime_error("truncated committee manifest");
        if (c.members.size() >= 1024) throw std::length_error("oversized committee manifest");
        if (peer.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) throw std::runtime_error("invalid credential filename");
        c.members.push_back({peer,net::digest_from_hex(key)});
    }
    if (!in.eof()) throw std::runtime_error("truncated committee manifest");
    c.validate(); return c;
}
std::unique_ptr<net::SigningKey> read_signing_key(const std::filesystem::path& folder, const std::string& peer) {
    const auto c = read_committee(folder); if (!c.key(peer)) throw std::runtime_error("identity is absent from committee");
    std::ifstream in(folder/(peer+".seed"),std::ios::binary); net::Seed seed{}; in.read(reinterpret_cast<char*>(seed.data()),seed.size());
    if (!in || in.peek() != std::char_traits<char>::eof()) { net::wipe(seed); throw std::runtime_error("invalid private seed file"); }
    auto out = std::make_unique<net::SigningKey>(seed); net::wipe(seed);
    if (out->public_key() != *c.key(peer)) throw std::runtime_error("private key does not match the pinned committee identity");
    return out;
}
void make_credentials(const std::filesystem::path& folder, int count, int quorum, int faults, const std::string& region) {
    if (count < 1 || count > 1024 || std::filesystem::exists(folder/"committee.txt")) throw std::invalid_argument("credential folder already exists or member count is invalid");
    net::Committee c{region,0,{},static_cast<std::uint32_t>(quorum),static_cast<std::uint32_t>(faults)};
    // Validate policy before writing any secret material.
    for (int i = 0; i < count; ++i) {
        net::Seed dummy{}; dummy[0] = i%255; dummy[1] = i/255;
        net::SigningKey dummy_key(dummy); c.members.push_back({"p"+std::to_string(i),dummy_key.public_key()});
    }
    c.validate(); c.members.clear(); std::filesystem::create_directories(folder);
    for (int i = 0; i < count; ++i) {
        auto seed = net::random_seed(); net::SigningKey key(seed); const auto peer = "p"+std::to_string(i);
        const auto path = folder/(peer+".seed");
        std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(seed.data()),seed.size()); net::wipe(seed); if (!out) throw std::runtime_error("private seed write failed");
#ifndef _WIN32
        if (chmod(path.c_str(),0600)) throw std::runtime_error("private seed permissions failed");
#endif
        c.members.push_back({peer,key.public_key()});
    }
    c.validate(); std::ofstream out(folder/"committee.txt"); out << "sg-net-committee-v1\n" << c.region << ' ' << c.generation << ' ' << c.quorum << ' ' << c.faults << '\n';
    for (const auto& m : c.members) out << m.peer << ' ' << net::hex(m.key) << '\n';
    if (!out) throw std::runtime_error("committee manifest write failed");
}
struct Journal::Impl {
    std::filesystem::path path;
    std::uint64_t sequence = 0;
#ifdef _WIN32
    HANDLE lease = INVALID_HANDLE_VALUE;
    HANDLE data = INVALID_HANDLE_VALUE;
#else
    int lease = -1;
    int data = -1;
#endif
    Impl(const std::filesystem::path& folder,const std::string& peer) : path(folder/(peer+".journal")) {
        if (!read_committee(folder).key(peer)) throw std::invalid_argument("unknown journal identity");
        const auto lock = folder/(peer+".lease");
#ifdef _WIN32
        lease = CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (lease == INVALID_HANDLE_VALUE) throw std::runtime_error("identity is already in use or its lease cannot be acquired");
#else
        lease = open(lock.c_str(),O_CREAT|O_RDWR,0600);
        if (lease < 0 || flock(lease,LOCK_EX|LOCK_NB)) { if (lease >= 0) close(lease); lease = -1; throw std::runtime_error("identity is already in use or its lease cannot be acquired"); }
#endif
    }
    ~Impl() {
#ifdef _WIN32
        if (data != INVALID_HANDLE_VALUE) CloseHandle(data);
        if (lease != INVALID_HANDLE_VALUE) CloseHandle(lease);
#else
        if (data >= 0) close(data);
        if (lease >= 0) close(lease);
#endif
    }
};
Journal::Journal(const std::filesystem::path& folder,const std::string& peer) : impl_(std::make_unique<Impl>(folder,peer)) {
    if (std::filesystem::exists(impl_->path)) throw std::runtime_error("used identity requires its verified checkpoint and vote journal; this demo starts fresh sessions only");
#ifdef _WIN32
    impl_->data = CreateFileW(impl_->path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if (impl_->data == INVALID_HANDLE_VALUE) throw std::runtime_error("agreement journal creation failed");
#else
    impl_->data = open(impl_->path.c_str(),O_CREAT|O_EXCL|O_RDWR,0600);
    if (impl_->data < 0) throw std::runtime_error("agreement journal creation failed");
    const int directory = open(folder.c_str(),O_RDONLY);
    if (directory < 0) throw std::runtime_error("agreement journal directory sync failed");
    const bool synced = fsync(directory) == 0; close(directory);
    if (!synced) throw std::runtime_error("agreement journal directory sync failed");
#endif
}
Journal::~Journal() = default;
void Journal::store(const net::Bytes& bytes) {
    // Alternating checksummed records preserve the preceding durable record
    // through a torn write. Keep the handle: readers cannot block a rename,
    // and the filename is never deleted or replaced during a world step.
    constexpr std::uint64_t slot_size = 16777344;
    if (impl_->sequence == UINT64_MAX) throw std::overflow_error("agreement journal sequence exhausted");
    net::detail::Writer writer("sg.net.durable-record.v1");
    net::detail::Writer checksum("sg.net.durable-checksum.v1"); checksum.integer(impl_->sequence+1); checksum.bytes(bytes);
    writer.integer(impl_->sequence+1); writer.digest(net::hash(checksum.data())); writer.bytes(bytes);
    const auto& record = writer.data();
    if (record.size() > slot_size) throw std::length_error("agreement journal record too large");
    const auto offset = (impl_->sequence%2)*slot_size;
#ifdef _WIN32
    LARGE_INTEGER position; position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD count = 0;
    if (!SetFilePointerEx(impl_->data,position,nullptr,FILE_BEGIN) || !WriteFile(impl_->data,record.data(),static_cast<DWORD>(record.size()),&count,nullptr) || count != record.size() || !FlushFileBuffers(impl_->data)) throw std::runtime_error("agreement journal flush failed");
#else
    std::size_t done = 0;
    while (done < record.size()) { const auto n = pwrite(impl_->data,record.data()+done,record.size()-done,static_cast<off_t>(offset+done)); if (n <= 0) throw std::runtime_error("agreement journal write failed"); done += static_cast<std::size_t>(n); }
    if (fsync(impl_->data)) throw std::runtime_error("agreement journal flush failed");
#endif
    ++impl_->sequence;
}
}
