#include "streaming/disk_cache.hpp"

#include "core/sha1.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace melodeus::streaming {

namespace {

std::string to_hex(const std::array<std::uint8_t, 20>& bytes) {
    std::string out(40, '0');
    constexpr char kDigits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 20; ++i) {
        out[i * 2] = kDigits[bytes[i] >> 4];
        out[i * 2 + 1] = kDigits[bytes[i] & 0xF];
    }
    return out;
}

} // namespace

DiskCache::DiskCache(std::filesystem::path dir, std::uint64_t max_bytes)
    : dir_(std::move(dir)), max_bytes_(max_bytes) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::lock_guard<std::mutex> lock(mutex_);
    index_dir_locked();
}

std::string DiskCache::key_for(const std::string& url) {
    return to_hex(core::sha1(url.data(), url.size()));
}

bool DiskCache::contains(const std::string& url) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.find(key_for(url)) != index_.end();
}

std::filesystem::path DiskCache::path_for(const std::string& url) const {
    return dir_ / (key_for(url) + ".bin");
}

bool DiskCache::commit(const std::string& url, const std::filesystem::path& tmp_file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(tmp_file, ec);
    if (ec) {
        return false;
    }
    const auto dest = path_for(url);
    std::filesystem::rename(tmp_file, dest, ec);
    if (ec) {
        // rename cruza volúmenes: copia + borra.
        std::filesystem::copy_file(tmp_file, dest,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return false;
        }
        std::filesystem::remove(tmp_file, ec);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = key_for(url);
    auto it = index_.find(key);
    if (it != index_.end()) {
        total_bytes_ -= it->second.bytes;
        it->second.bytes = size;
        it->second.last_used = ++clock_;
        total_bytes_ += size;
    } else {
        index_[key] = Entry{size, ++clock_};
        total_bytes_ += size;
    }
    evict_locked();
    return true;
}

bool DiskCache::remove(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = key_for(url);
    auto it = index_.find(key);
    if (it == index_.end()) {
        return false;
    }
    total_bytes_ -= it->second.bytes;
    index_.erase(it);
    std::error_code ec;
    std::filesystem::remove(path_for(url), ec);
    return true;
}

std::uint64_t DiskCache::total_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

void DiskCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [key, entry] : index_) {
        (void)entry;
        std::error_code ec;
        std::filesystem::remove(dir_ / (key + ".bin"), ec);
    }
    index_.clear();
    total_bytes_ = 0;
}

void DiskCache::index_dir_locked() {
    std::error_code ec;
    for (const auto& d : std::filesystem::directory_iterator(dir_, ec)) {
        if (!d.is_regular_file(ec) || d.path().extension() != ".bin") {
            continue;
        }
        const auto size = d.file_size(ec);
        if (ec) {
            continue;
        }
        const std::string key = d.path().stem().string();
        index_[key] = Entry{size, ++clock_};
        total_bytes_ += size;
    }
}

void DiskCache::evict_locked() {
    while (total_bytes_ > max_bytes_ && !index_.empty()) {
        auto oldest = index_.begin();
        for (auto it = index_.begin(); it != index_.end(); ++it) {
            if (it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        total_bytes_ -= oldest->second.bytes;
        std::error_code ec;
        std::filesystem::remove(dir_ / (oldest->first + ".bin"), ec);
        index_.erase(oldest);
    }
}

} // namespace melodeus::streaming
