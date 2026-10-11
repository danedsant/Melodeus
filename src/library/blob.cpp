#include "library/blob.hpp"

#include <cstring>

namespace melodeus::library {

namespace {

void write_f64(std::vector<std::uint8_t>& out, double v) {
    std::uint8_t buf[8];
    std::memcpy(buf, &v, 8);
    out.insert(out.end(), buf, buf + 8);
}

double read_f64(const std::uint8_t* p) {
    double v;
    std::memcpy(&v, p, 8);
    return v;
}

} // namespace

std::vector<std::uint8_t> pack_beatgrid(const analysis::BeatGrid& grid) {
    std::vector<std::uint8_t> out;
    out.reserve(16);
    write_f64(out, grid.bpm);
    write_f64(out, grid.offset_sec);
    return out;
}

bool unpack_beatgrid(const std::uint8_t* data, std::size_t size, analysis::BeatGrid* out) {
    if (data == nullptr || out == nullptr || size < 16) {
        return false;
    }
    out->bpm = read_f64(data);
    out->offset_sec = read_f64(data + 8);
    return true;
}

} // namespace melodeus::library
