#include <zephyr/ztest.h>
#include <math.h>

#include "hr_filter.h"

static float rms_tail(const float* v, size_t len, size_t tail_samples)
{
    if (!v || len == 0) return 0.0f;
    size_t start = len > tail_samples ? (len - tail_samples) : 0;
    double acc = 0.0;
    size_t n = len - start;
    for (size_t i = start; i < len; ++i) {
        double x = v[i];
        acc += x * x;
    }
    return (float)sqrt(acc / (n ? n : 1));
}

ZTEST_SUITE(hr_filter, NULL, NULL, NULL, NULL, NULL);

ZTEST(hr_filter, test_dc_reject)
{
    const size_t N = 3000; // 30s @100 Hz, allow longer settling
    float in[N];
    float out[N];
    for (size_t i = 0; i < N; ++i) { in[i] = 1.0f; out[i] = 0.0f; }

    hr_filter_init();
    hr_filter_process(in, out, N);

    float dc_rms = rms_tail(out, N, 2500);
    /* Expect strong DC rejection after transient; allow small numerical ripple */
    zassert_true(dc_rms < 5e-2f, "DC RMS too high: %f", (double)dc_rms);
}

ZTEST(hr_filter, test_1hz_pass)
{
    const float fs = 100.0f;
    const float PI = 3.14159265358979323846f;
    const size_t N = 2000; // 20s
    float in[N];
    float out[N];
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        in[i] = sinf(2.0f * PI * 1.0f * t);
    }

    hr_filter_init();
    hr_filter_process(in, out, N);

    /* Discard first 5s (500 samples) */
    auto rms_tail_from = [](const float* v, size_t len, size_t start) {
        if (!v || start >= len) return 0.0f;
        double acc = 0.0;
        size_t n = len - start;
        for (size_t i = start; i < len; ++i) acc += (double)v[i] * (double)v[i];
        return n ? (float)sqrt(acc / n) : 0.0f;
    };

    float in_rms = rms_tail_from(in, N, 500);
    float out_rms = rms_tail_from(out, N, 500);
    float gain = out_rms / (in_rms + 1e-12f);

    /* Expect near-unity within a few dB */
    zassert_true(gain > 0.7f && gain < 1.1f, "1Hz gain out of range: %f", (double)gain);
}

ZTEST(hr_filter, test_10hz_atten)
{
    const float fs = 100.0f;
    const float PI = 3.14159265358979323846f;
    const size_t N = 2000; // 20s

    /* Compute 1Hz baseline gain for fair comparison */
    float in1[N];
    float out1[N];
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        in1[i] = sinf(2.0f * PI * 1.0f * t);
    }
    hr_filter_init();
    hr_filter_process(in1, out1, N);
    float out1_rms = rms_tail(out1, N, 1500); // last 5s

    /* Now the 10Hz tone */
    float in10[N];
    float out10[N];
    for (size_t i = 0; i < N; ++i) {
        float t = (float)i / fs;
        in10[i] = sinf(2.0f * PI * 10.0f * t);
    }
    hr_filter_init();
    hr_filter_process(in10, out10, N);
    float out10_rms = rms_tail(out10, N, 1500);

    float rel = out10_rms / (out1_rms + 1e-12f);
    /* Expect significant attenuation vs. 1Hz (e.g., <= -6 dB) */
    zassert_true(rel <= 0.5f, "10Hz attenuation too small, rel=%.3f", (double)rel);
}
