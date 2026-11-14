// Noise floor of a spectrum slice, for scaling a waterfall.
//
// A percentile over the whole slice is wrong when the radio's filter is narrower than
// the slice: most bins then hold the filter's stopband, far below the band noise, the
// "floor" lands there, and the noise inside the passband is drawn as if it were signal.
//
// So the slice is cut into overlapping blocks about 200 Hz wide. Each block's lower
// third is its noise level now. A second value per block follows that level down at
// once but up only slowly, so it outlasts the transmissions that come and go in the
// block and settles on what is always there: band noise inside the radio's passband,
// next to nothing outside it. Blocks whose settled level is within kPassbandDb of the
// highest are the passband, and the floor is the median of their current levels.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gtty {

class NoiseFloorTracker {
 public:
  // db: n bins of binHz each. dtSec: time since the previous call. Returns the floor in dB.
  float update(const float* db, size_t n, double binHz, double dtSec) {
    if (n == 0 || binHz <= 0) return floor_;
    size_t width = std::clamp<size_t>(static_cast<size_t>(std::lround(kBlockHz / binHz)), 1, n);
    size_t step = std::max<size_t>(1, width / 2);
    size_t blocks = (n - width) / step + 1;
    if (blocks != settled_.size() || n != bins_) { settled_.clear(); bins_ = n; }
    bool first = settled_.empty();
    if (first) settled_.resize(blocks);

    now_.resize(blocks);
    float rise = static_cast<float>(kRiseDbPerSec * std::clamp(dtSec, 0.0, 1.0));
    for (size_t b = 0; b < blocks; ++b) {
      tmp_.assign(db + b * step, db + b * step + width);
      auto third = tmp_.begin() + static_cast<std::ptrdiff_t>(tmp_.size() / 3);
      std::nth_element(tmp_.begin(), third, tmp_.end());
      float level = *third;
      now_[b] = level;
      if (first || level < settled_[b]) settled_[b] = level;
      else settled_[b] = std::min(level, settled_[b] + rise);
    }

    float top = *std::max_element(settled_.begin(), settled_.end());
    tmp_.clear();
    for (size_t b = 0; b < blocks; ++b)
      if (settled_[b] >= top - kPassbandDb) tmp_.push_back(now_[b]);
    auto mid = tmp_.begin() + static_cast<std::ptrdiff_t>(tmp_.size() / 2);
    std::nth_element(tmp_.begin(), mid, tmp_.end());
    floor_ = *mid;
    return floor_;
  }
  void reset() { settled_.clear(); }

 private:
  static constexpr double kBlockHz = 200.0;
  static constexpr float kPassbandDb = 15.f;
  // Slow enough that a contest exchange, or a CQ a minute long, does not pass for band noise.
  static constexpr double kRiseDbPerSec = 0.2;
  std::vector<float> settled_, now_, tmp_;
  size_t bins_ = 0;
  float floor_ = -80.f;
};

}  // namespace gtty
