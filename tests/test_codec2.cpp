// libcodec2 FSK engine loopback (skipped when not built with libcodec2).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/modem/codec2_demod.h"
#include "core/modem/matched_filter_demod.h"
#include "check.h"
#include "rtty_sim.h"

using namespace gtty;

int main() {
  if (!codec2Available()) { std::printf("codec2 not built in; skipping\n"); return 0; }
  const std::string msg = "CQ CQ DE W1AW W1AW 599 001 TEST K";
  RttyParams p;
  DemodConfig c; c.params = p; c.sampleRate = Receiver::kInternalRate;
  {
    Codec2Demod d(c);
    auto got = demodulateWith(d, modulate("RYRYRYRY " + msg, p, 48000), 48000);
    std::printf("codec2 clean: '%s' (AFC %.1f Hz, %llu chars, %llu framing errors)\n", trim(got).c_str(),
                d.status().afcOffsetHz, (unsigned long long)d.status().chars, (unsigned long long)d.status().framingErrors);
    CHECK(got.find("599 001 TEST K") != std::string::npos);
    size_t at = got.find("CQ");
    CHECK(at != std::string::npos && errors(msg, got.substr(at)) <= 2);
  }
  {
    auto y = modulate("RYRYRYRY " + msg, p, 48000);
    addNoise(y, 6.0, 48000, 3);
    Codec2Demod d(c);
    auto got = demodulateWith(d, y, 48000);
    size_t at = got.find("CQ");
    std::string body = at == std::string::npos ? got : got.substr(at);
    std::printf("codec2 6 dB SNR: '%s' (%d errors)\n", trim(body).c_str(), errors(msg, body));
    CHECK(errors(msg, body) <= 8);
  }
  // The vote groups the engines' characters by when they ended, within half a character
  // (83 ms at 45.45 baud), so codec2 has to keep time with the others. It counted its
  // clock in truncated steps (losing 0.26 ms a second, out of the window after five
  // minutes) and lost up to a frame (~260 ms) at every reconfigure (a tone change, a
  // Skimmer click), and its characters then interleaved with the others' in the vote.
  {
    const double fs = Receiver::kInternalRate;
    std::string text = "RYRYRYRY";
    while (text.size() < 2200) text += " CQ TEST AA3B AA3B CQ 599 123 123 TU";   // about six minutes
    auto y = modulate(text, p, fs);
    auto run = [&](bool reconfigure) {
      MatchedFilterDemod a(c, "A");
      Codec2Demod d(c);
      std::vector<CodeEvent> ea, ed;
      a.onCode = [&](const CodeEvent& e) { if (e.valid) ea.push_back(e); };
      d.onCode = [&](const CodeEvent& e) { if (e.valid) ed.push_back(e); };
      const size_t block = 1000;
      for (size_t i = 0, k = 0; i < y.size(); i += block, ++k) {
        size_t n = std::min(block, y.size() - i);
        a.process(y.data() + i, n);
        d.process(y.data() + i, n);
        if (reconfigure && k % 900 == 450) d.configure(c);   // every 75 s
      }
      // Median distance from each of A's characters to codec2's same character, over the last minute.
      std::vector<double> off;
      for (const auto& e : ea) {
        if (static_cast<double>(e.sample) < static_cast<double>(y.size()) - 60 * fs) continue;
        double best = 1e9;
        for (const auto& o : ed)
          if (o.code == e.code) {
            double ms = (static_cast<double>(o.sample) - static_cast<double>(e.sample)) * 1000.0 / fs;
            if (std::fabs(ms) < std::fabs(best)) best = ms;
          }
        if (std::fabs(best) < 250) off.push_back(best);
      }
      if (off.size() < 100) return 1e9;
      std::nth_element(off.begin(), off.begin() + off.size() / 2, off.end());
      return off[off.size() / 2];
    };
    double steady = run(false), retuned = run(true);
    std::printf("codec2 timing vs A after six minutes: %+.0f ms, reconfigured five times: %+.0f ms\n", steady, retuned);
    CHECK(std::fabs(steady) < 20);
    CHECK(std::fabs(retuned) < 20);
  }
  TEST_MAIN_END();
}
