#include "analysis/key.hpp"

#include "waveform/fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace melodeus::analysis {

namespace {

constexpr int kFftSize = 4096;
constexpr int kHop = 2048;
constexpr std::int64_t kMinWindows = 8;

// Perfiles Krumhansl-Schmuckler (12 pesos desde C).
constexpr std::array<double, 12> kMajorProfile = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                                  2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinorProfile = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                                  2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

constexpr const char* kTraditional[12] = {"C",  "C#", "D",  "Eb", "E",  "F",
                                          "F#", "G",  "Ab", "A",  "Bb", "B"};

// Pitch class → número Camelot (índice [pc][0]=mayor, [pc][1]=menor).
constexpr int kCamelot[12][2] = {
    {8, 5},  // C  → 8B / Cm 5A
    {3, 12}, // C# → 3B / C#m 12A
    {10, 7}, // D  → 10B / Dm 7A
    {5, 2},  // Eb → 5B / Ebm 2A
    {12, 9}, // E  → 12B / Em 9A
    {7, 4},  // F  → 7B / Fm 4A
    {2, 11}, // F# → 2B / F#m 11A
    {9, 6},  // G  → 9B / Gm 6A
    {4, 1},  // Ab → 4B / Abm 1A
    {11, 8}, // A  → 11B / Am 8A
    {6, 3},  // Bb → 6B / Bbm 3A
    {1, 10}, // B  → 1B / Bm 10A
};

/// Correlación de Pearson entre croma rotado y perfil.
double pearson(const std::array<double, 12>& chroma, const std::array<double, 12>& profile,
               int rotation) {
    double sum_xy = 0.0;
    double sum_x2 = 0.0;
    double sum_y2 = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double x = chroma[static_cast<std::size_t>((i + rotation) % 12)];
        const double y = profile[static_cast<std::size_t>(i)];
        sum_xy += x * y;
        sum_x2 += x * x;
        sum_y2 += y * y;
    }
    const double denom = std::sqrt(sum_x2 * sum_y2);
    return (denom > 1e-12) ? (sum_xy / denom) : 0.0;
}

} // namespace

const char* KeyInfo::traditional_name() const {
    static const char* kMinorNames[12] = {"Cm",  "C#m", "Dm",  "Ebm", "Em",  "Fm",
                                          "F#m", "Gm",  "Abm", "Am",  "Bbm", "Bm"};
    const int pc = ((root_pc % 12) + 12) % 12;
    return minor ? kMinorNames[pc] : kTraditional[pc];
}

const char* KeyInfo::camelot_name() const {
    static thread_local char buf[8];
    const int pc = ((root_pc % 12) + 12) % 12;
    const int num = kCamelot[pc][minor ? 1 : 0];
    // snprintf manual: el número es siempre 1..12 y el sufijo A/B.
    int n = 0;
    if (num >= 10) {
        buf[n++] = static_cast<char>('1');
        buf[n++] = static_cast<char>('0' + (num - 10));
    } else {
        buf[n++] = static_cast<char>('0' + num);
    }
    buf[n++] = minor ? 'A' : 'B';
    buf[n] = '\0';
    return buf;
}

KeyInfo detect_key(const float* mono, std::int64_t frames, double sample_rate) {
    KeyInfo out;
    if (mono == nullptr || frames < kFftSize || sample_rate <= 0.0) {
        return out;
    }

    // Croma STFT: promedio de magnitudes por pitch class sobre ventanas
    // Hann; el rango tonal útil es C2..C6 (pc 24..96 en MIDI).
    std::array<double, 12> chroma{};
    int windows = 0;
    wave::FftScratch fft(kFftSize);
    std::vector<float> window_input(static_cast<std::size_t>(kFftSize));

    for (std::int64_t start = 0; start + kFftSize <= frames; start += kHop) {
        double frame_energy = 0.0;
        for (int i = 0; i < kFftSize; ++i) {
            const float s = mono[start + i];
            frame_energy += static_cast<double>(s) * s;
            const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 *
                                                     static_cast<double>(i) / (kFftSize - 1));
            window_input[static_cast<std::size_t>(i)] = static_cast<float>(s * hann);
        }
        if (frame_energy < 1e-8) {
            continue;
        }
        fft.compute(window_input.data(), kFftSize);
        const float* mag = fft.magnitudes();
        const int bins = fft.bins();
        for (int b = 1; b < bins; ++b) {
            const double freq = static_cast<double>(b) * sample_rate / kFftSize;
            if (freq < 60.0 || freq > 2100.0) {
                continue; // C2 (~65 Hz) .. C6 (~1046 Hz) útil
            }
            const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
            const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
            chroma[static_cast<std::size_t>(pc)] += static_cast<double>(mag[b]);
        }
        ++windows;
    }
    if (windows < kMinWindows) {
        return out;
    }

    // Normalizar el croma a suma 1.
    double total = 0.0;
    for (double v : chroma) {
        total += v;
    }
    if (total <= 1e-12) {
        return out;
    }
    for (double& v : chroma) {
        v /= total;
    }

    // Mejor correlación entre las 24 claves candidatas.
    double best = -2.0;
    int best_root = 0;
    bool best_minor = false;
    for (int root = 0; root < 12; ++root) {
        const double maj = pearson(chroma, kMajorProfile, root);
        if (maj > best) {
            best = maj;
            best_root = root;
            best_minor = false;
        }
        const double min = pearson(chroma, kMinorProfile, root);
        if (min > best) {
            best = min;
            best_root = root;
            best_minor = true;
        }
    }
    out.root_pc = best_root;
    out.minor = best_minor;
    const int num = kCamelot[best_root][best_minor ? 1 : 0];
    out.camelot = num;
    out.camelot_flat = best_minor;
    out.confidence = static_cast<float>((std::max)(0.0, best));
    return out;
}

} // namespace melodeus::analysis
