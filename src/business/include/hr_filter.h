#ifndef HR_FILTER_H
#define HR_FILTER_H

#include <cstddef>
#include <array>
#include <stdexcept>

struct BiquadDF2T
{
    // Direct form II
    // y[n] = a0*x[n] + a1*x[n-1] + a2*x[n-2] – b1*y[n-1] – b2*y[n-2]
    float b0=0, b1=0, b2=0, a1=0, a2=0;
    float s1=0, s2=0; // two delay states (transposed)

    inline float process(float x) {
        float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    inline void reset() noexcept { s1 = 0.0f; s2 = 0.0f; }
};

template <std::size_t N>
struct BiquadCascadeDF2T {
    std::array<BiquadDF2T, N> sec{};

    inline float process(float x) {
        for (auto &s : sec) x = s.process(x);
        return x;
    }

    inline void processBuffer(const float* in, float* out, std::size_t len) {
        if (!in || !out) {
            return; // no-op if invalid
        }
        for (std::size_t i = 0; i < len; ++i) {
            float y = in[i];
            for (auto &s : sec) y = s.process(y);
            out[i] = y;
        }
    }

    inline void reset() noexcept { for (auto &s : sec) s.reset(); }
};

// API to initialize and run the bandpass cascade
void hr_filter_init();
void hr_filter_process(const float* in, float* out, std::size_t n);

#endif /* HR_FILTER_H */