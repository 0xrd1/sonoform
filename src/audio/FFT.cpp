#include "FFT.h"
#include <cmath>
#include <cstdint>

namespace fft {

void Transform(std::vector<Complex>& a) {
    const size_t n = a.size();
    if (n <= 1) return;

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    // Iterative Cooley-Tukey butterflies.
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * 3.14159265358979323846f / static_cast<float>(len);
        const Complex wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            Complex w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; k++) {
                Complex u = a[i + k];
                Complex v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace fft
