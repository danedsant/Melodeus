#include "analysis/beatgrid.hpp"

#include <cmath>

namespace melodeus::analysis {

std::int64_t BeatGrid::beat_index(double pos_sec) const {
    if (!valid() || pos_sec < offset_sec) {
        return -1;
    }
    const double beat_period = 60.0 / bpm;
    return static_cast<std::int64_t>(std::floor((pos_sec - offset_sec) / beat_period));
}

double BeatGrid::beat_time(std::int64_t index) const {
    if (!valid() || index < 0) {
        return 0.0;
    }
    return offset_sec + static_cast<double>(index) * (60.0 / bpm);
}

BeatGrid make_beatgrid(double bpm, double onset_first_sec) {
    BeatGrid grid;
    if (bpm > 0.0) {
        grid.bpm = bpm;
        grid.offset_sec = (onset_first_sec >= 0.0) ? onset_first_sec : 0.0;
    }
    return grid;
}

} // namespace melodeus::analysis
