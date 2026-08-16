#pragma once
#include <vector>
#include <complex>

namespace fft {

using Complex = std::complex<float>;

// In-place iterative radix-2 Cooley-Tukey FFT.
// data.size() MUST be a power of two.
void Transform(std::vector<Complex>& data);

} // namespace fft
