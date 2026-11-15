// Setting the rig's filter width: Hamlib reports success whether or not the radio took
// the width, so gtty reads it back once Hamlib's cache has expired and reports a rig that
// is on another width.
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "core/rig/rig_control.h"
#include "check.h"

using namespace gtty;
using std::chrono::milliseconds;

int main() {
  // ----------------------------------------------- the check itself
  const WidthCheck::Clock::time_point t0{};
  const auto settle = WidthCheck::kSettle;
  {
    WidthCheck c;
    CHECK_EQ(c.polled(500, t0), 0);                       // nothing was asked for
    c.asked(400, t0);
    // Too early to tell: until the cache expires the answer is the width asked for,
    // and a rig that has not switched yet is not a refusal either.
    CHECK_EQ(c.polled(400, t0 + milliseconds(250)), 0);
    CHECK_EQ(c.polled(500, t0 + settle - milliseconds(1)), 0);
    CHECK_EQ(c.polled(500, t0 + settle), 400);            // the rig stayed on 500 Hz
    CHECK_EQ(c.polled(500, t0 + settle + milliseconds(250)), 0);   // said once
  }
  {
    WidthCheck c;
    c.asked(400, t0);
    CHECK_EQ(c.polled(400, t0 + settle), 0);              // taken
    // A later change, on the front panel or by another program, is not a refusal.
    CHECK_EQ(c.polled(500, t0 + settle + milliseconds(250)), 0);
  }
  {
    WidthCheck c;
    c.asked(400, t0);
    CHECK_EQ(c.polled(0, t0 + settle), 0);                // the rig does not report a width
    CHECK_EQ(c.polled(500, t0 + settle + milliseconds(250)), 0);
  }
  {
    WidthCheck c;
    c.asked(400, t0);
    c.asked(250, t0 + milliseconds(600));                 // a second choice replaces the first
    CHECK_EQ(c.polled(300, t0 + settle), 0);              // and is given its own time
    CHECK_EQ(c.polled(300, t0 + milliseconds(600) + settle), 250);
  }
  {
    WidthCheck c;
    c.asked(400, t0);
    c.cancel();
    CHECK_EQ(c.polled(500, t0 + settle), 0);
  }

  // ----------------------------------------------- through Hamlib, on its dummy rig
  // The dummy rig takes any width, so nothing is reported and the width is the one set.
  {
    RigControl rig;
    std::mutex mu;
    std::vector<RigState> seen;
    rig.onState = [&](const RigState& s) { std::lock_guard<std::mutex> lk(mu); seen.push_back(s); };
    RigConfig cfg;
    cfg.enabled = true;
    cfg.model = 1;
    cfg.pollMs = 50;
    std::string err;
    CHECK(rig.open(cfg, err));
    CHECK(!rig.setWidth(0));
    CHECK(rig.setMode("RTTY"));
    CHECK(rig.setWidth(400));
    std::this_thread::sleep_for(settle + milliseconds(400));
    rig.close();
    CHECK(!seen.empty());
    bool refused = false;
    for (const auto& s : seen) refused = refused || s.widthRefusedHz != 0;
    CHECK(!refused);
    if (!seen.empty()) CHECK_EQ(seen.back().widthHz, 400);
  }
  TEST_MAIN_END();
}
