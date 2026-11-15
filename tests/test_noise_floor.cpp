#include "check.h"
#include "core/dsp/noise_floor.h"

#include <cmath>
#include <random>
#include <vector>

using namespace gtty;

namespace {
const double kBinHz = 5.86;        // 48 kHz / 8192
const size_t kBins = 460;          // 300..3000 Hz
const double kDt = 0.085;
std::mt19937 rng(7);

// Band noise at noiseDb between loHz and hiHz (offsets into the slice), stopDb outside.
std::vector<float> frame(double loHz, double hiHz, float noiseDb, float stopDb) {
  std::uniform_real_distribution<float> jitter(-4.f, 4.f);
  std::vector<float> db(kBins);
  for (size_t i = 0; i < kBins; ++i) {
    double hz = i * kBinHz;
    db[i] = (hz >= loHz && hz < hiHz ? noiseDb : stopDb) + jitter(rng);
  }
  return db;
}

// An RTTY signal: two tones 170 Hz apart, each some 60 Hz wide, on keying sidebands
// 25 dB down that fill the gap between the tones and 100 Hz either side.
void addSignal(std::vector<float>& db, double markHz, float levelDb) {
  for (size_t i = 0; i < kBins; ++i) {
    double hz = i * kBinHz;
    if (std::abs(hz - markHz) < 30.0 || std::abs(hz - markHz - 170.0) < 30.0) db[i] = levelDb;
    else if (hz > markHz - 100.0 && hz < markHz + 270.0) db[i] = std::max(db[i], levelDb - 25.f);
  }
}

// The estimate this replaces: the lower third of the whole slice.
float wholeSlice(std::vector<float> db) {
  std::nth_element(db.begin(), db.begin() + db.size() / 3, db.end());
  return db[db.size() / 3];
}
}  // namespace

int main() {
  // Open receiver: the floor is the band noise.
  {
    NoiseFloorTracker t;
    float fl = 0;
    for (int i = 0; i < 50; ++i) fl = t.update(frame(0, 1e9, -60, -60).data(), kBins, kBinHz, kDt);
    CHECK(std::abs(fl - -60.f) < 4.f);
  }

  // A 400 Hz filter in the radio: the floor is the noise inside the passband, not the
  // dead stopband that fills most of the slice (which is what made the display all yellow).
  {
    NoiseFloorTracker t;
    float fl = 0;
    std::vector<float> db;
    for (int i = 0; i < 50; ++i) { db = frame(1700, 2100, -60, -110); fl = t.update(db.data(), kBins, kBinHz, kDt); }
    CHECK(std::abs(fl - -60.f) < 4.f);
    CHECK(wholeSlice(db) < -100.f);

    // A strong station filling the passband may lift the floor while it transmits,
    // but stays well clear of it, and the floor is back as soon as the station stops.
    for (int i = 0; i < 100; ++i) {
      db = frame(1700, 2100, -60, -110);
      addSignal(db, 1815, -20);
      fl = t.update(db.data(), kBins, kBinHz, kDt);
    }
    CHECK(fl > -64.f && fl < -20.f - 20.f);
    fl = t.update(frame(1700, 2100, -60, -110).data(), kBins, kBinHz, kDt);
    CHECK(std::abs(fl - -60.f) < 4.f);

    // Opening the filter up again is followed at once.
    fl = t.update(frame(0, 1e9, -60, -60).data(), kBins, kBinHz, kDt);
    CHECK(std::abs(fl - -60.f) < 4.f);
  }

  // A 250 Hz filter, the narrowest in use, with a shallow 30 dB stopband.
  {
    NoiseFloorTracker t;
    float fl = 0;
    for (int i = 0; i < 50; ++i) fl = t.update(frame(1000, 1250, -70, -100).data(), kBins, kBinHz, kDt);
    CHECK(std::abs(fl - -70.f) < 5.f);
  }

  // Open receiver, one station 40 dB over the noise calling CQ for 30 s out of every 35:
  // it must not be taken for the passband, or everything weaker would go dark.
  {
    NoiseFloorTracker t;
    float worst = -200;
    for (int i = 0; i < 2500; ++i) {   // about 3.5 minutes
      auto db = frame(0, 1e9, -60, -60);
      if (i % 412 >= 59) addSignal(db, 1200, -20);
      float fl = t.update(db.data(), kBins, kBinHz, kDt);
      worst = std::max(worst, fl);
    }
    CHECK(worst < -55.f);
  }

  // Issue #43: transmitting mutes the receive audio. Learning that silence as the floor
  // left the waterfall all yellow for minutes after going back to receive (the settled
  // levels follow down at once but up at 0.2 dB/s). Held while keyed, the floor is right
  // on the first frame after.
  {
    const std::vector<float> band = frame(1700, 2100, -60, -110);
    const std::vector<float> muted = frame(0, 1e9, -135, -135);
    auto run = [&](bool hold) {
      NoiseFloorTracker t;
      TransmitHold h;
      double now = 0;
      float fl = 0;
      auto step = [&](const std::vector<float>& db, bool keyed) {
        now += kDt;
        if (!(hold && h.update(keyed, now))) fl = t.update(db.data(), kBins, kBinHz, kDt);
      };
      for (int i = 0; i < 100; ++i) step(band, false);
      for (int i = 0; i < 120; ++i) step(muted, true);    // 10 s over
      step(muted, false);                                 // still muted as the rig unkeys
      for (int i = 0; i < 3; ++i) step(band, false);
      return fl;
    };
    CHECK(run(false) < -90.f);              // the bug: stopband taken for the floor
    CHECK(std::abs(run(true) - -60.f) < 4.f);
  }

  // The hold lasts while keyed and for its tail after, then lets go.
  {
    TransmitHold h;
    CHECK(!h.update(false, 0.0));
    CHECK(h.update(true, 1.0));
    CHECK(h.update(true, 5.0));
    CHECK(h.update(false, 5.1));
    CHECK(h.update(false, 5.0 + TransmitHold::kTailSec - 0.01));
    CHECK(!h.update(false, 5.0 + TransmitHold::kTailSec + 0.2));
  }

  // A different slice size starts over rather than reading stale blocks.
  {
    NoiseFloorTracker t;
    for (int i = 0; i < 10; ++i) t.update(frame(0, 1e9, -60, -60).data(), kBins, kBinHz, kDt);
    std::vector<float> small(40, -45.f);
    CHECK(std::abs(t.update(small.data(), small.size(), kBinHz, kDt) - -45.f) < 0.5f);
    std::vector<float> tiny(3, -45.f);
    CHECK(std::abs(t.update(tiny.data(), tiny.size(), 50.0, kDt) - -45.f) < 0.5f);
  }

  TEST_MAIN_END();
}
