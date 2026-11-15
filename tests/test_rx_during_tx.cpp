// Received text around a transmission (issue #47).
//
// The receiver is muted while transmitting, but characters decoded before keying were
// still on their way out (the vote waits for its slowest engine, doubtful and trailing
// characters are held), and what the decoders made of the muted audio came out too. So
// received characters landed inside the transmitted text: "[TX] DL <RX> M <TX> 1ASA".
#include <string>
#include <vector>

#include "check.h"
#include "core/events.h"
#include "core/receiver.h"
#include "rtty_sim.h"

using namespace gtty;

namespace {
const double kFs = 48000;

// All received text in the queue, per slot (the engines, then the vote).
std::vector<std::string> drain(EventQueue& q, size_t slots) {
  std::vector<std::string> out(slots);
  Event e;
  while (q.pop(e))
    if (auto* t = std::get_if<RxTextEvent>(&e))
      if (t->slot >= 0 && static_cast<size_t>(t->slot) < slots) out[t->slot] += t->text;
  return out;
}

void feed(Receiver& rx, const std::vector<float>& y) {
  for (size_t i = 0; i < y.size(); i += 4096) rx.processSamples(y.data() + i, std::min<size_t>(4096, y.size() - i), kFs);
}
}  // namespace

int main() {
  RttyParams p;
  DemodConfig c;
  c.params = p;
  EventQueue q;
  Receiver rx(nullptr, &q);
  rx.configure(c);
  const size_t slots = rx.bank().size() + 1;
  const int vote = rx.voteSlot();

  // A station answers; we key up the moment it stops: no silence after its last character
  // (modulate() pads 0.3 s, and one LTRS follows the text).
  auto call = modulate("RYRYRYRY CQ CQ DE DL1ASA DL1ASA", p, kFs);
  call.resize(call.size() - static_cast<size_t>(kFs * (0.3 + p.charPeriod())));
  feed(rx, call);
  auto before = drain(q, slots);
  rx.setMuted(true);
  std::vector<float> block(4096, 0.f);
  rx.processSamples(block.data(), block.size(), kFs);   // the first block after keying
  auto atKey = drain(q, slots);
  std::string heard = before[vote] + atKey[vote];
  std::printf("before keying: '%s'\n", trim(heard).c_str());
  // Everything decoded before keying is out by the first block after it: the call is whole.
  CHECK(heard.find("DL1ASA DL1ASA") != std::string::npos);

  // While transmitting, nothing is received, in any pane, even with a signal on the input
  // (the rig's monitor, or the muting being late).
  feed(rx, modulate("RYRYRYRY TU DF7C CQ TEST", p, kFs));
  auto during = drain(q, slots);
  for (size_t s = 0; s < slots; ++s) {
    if (!during[s].empty()) std::printf("slot %zu while keyed: '%s'\n", s, during[s].c_str());
    CHECK(during[s].empty());
  }

  // Back on receive, the next station decodes as usual.
  rx.setMuted(false);
  feed(rx, modulate("RYRYRYRY CQ CQ DE K5XOM K5XOM", p, kFs));
  std::vector<float> tail(static_cast<size_t>(kFs), 0.f);
  feed(rx, tail);
  rx.flushEvents();
  auto after = drain(q, slots);
  std::printf("after: '%s'\n", trim(after[vote]).c_str());
  CHECK(after[vote].find("K5XOM K5XOM") != std::string::npos);

  TEST_MAIN_END();
}
