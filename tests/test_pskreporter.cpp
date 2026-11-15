// PSK Reporter client: the IPFIX datagram layout from pskreporter.info/pskdev.html,
// splitting into datagrams, and the reporter's de-duplication and pacing.
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <string>
#include <vector>

#include "core/net/psk_reporter.h"
#include "check.h"

using namespace gtty;

namespace {
uint16_t get16(const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>(b[at] << 8 | b[at + 1]); }
uint32_t get32(const std::vector<uint8_t>& b, size_t at) {
  return static_cast<uint32_t>(get16(b, at)) << 16 | get16(b, at + 2);
}
std::string getString(const std::vector<uint8_t>& b, size_t& at) {
  size_t n = b[at++];
  std::string s(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + n));
  at += n;
  return s;
}

// Walk the sets of a datagram: returns {set id -> offset}, checks lengths and padding.
std::vector<std::pair<uint16_t, size_t>> sets(const std::vector<uint8_t>& d) {
  std::vector<std::pair<uint16_t, size_t>> out;
  size_t at = 16;
  while (at + 4 <= d.size()) {
    uint16_t id = get16(d, at), len = get16(d, at + 2);
    CHECK(len >= 4 && len % 4 == 0);
    out.push_back({id, at});
    at += len;
  }
  CHECK_EQ(at, d.size());
  return out;
}
}  // namespace

int main() {
  const PskReceiverInfo rx{"WA8Q", "EN81", "gtty 1.0.0"};

  // ---------------------------------------------------------- one datagram
  {
    uint32_t seq = 7;
    std::vector<PskSpot> spots{{"DL1ABC", 14083500, "RTTY", 1700000000}, {"JA1XYZ/P", 7043200, "RTTY", 1700000060}};
    auto ds = pskreporter::encode(rx, spots, true, 0xDEADBEEF, 1700000100, seq);
    CHECK_EQ(ds.size(), size_t(1));
    const auto& d = ds[0];
    CHECK_EQ(get16(d, 0), uint16_t(0x000A));
    CHECK_EQ(get16(d, 2), static_cast<uint16_t>(d.size()));
    CHECK_EQ(get32(d, 4), uint32_t(1700000100));
    CHECK_EQ(get32(d, 8), uint32_t(7));
    CHECK_EQ(get32(d, 12), uint32_t(0xDEADBEEF));
    CHECK_EQ(seq, uint32_t(10));   // the receiver record and two spots

    auto ss = sets(d);
    CHECK_EQ(ss.size(), size_t(4));
    // The receiver options template, byte for byte as pskdev.html gives it.
    const uint8_t rxTemplate[] = {0x00, 0x03, 0x00, 0x24, 0x99, 0x92, 0x00, 0x03, 0x00, 0x00, 0x80, 0x02,
                                  0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F, 0x80, 0x04, 0xFF, 0xFF, 0x00, 0x00,
                                  0x76, 0x8F, 0x80, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F, 0x00, 0x00};
    CHECK(std::vector<uint8_t>(d.begin() + 16, d.begin() + 16 + 36) ==
          std::vector<uint8_t>(std::begin(rxTemplate), std::end(rxTemplate)));
    CHECK_EQ(ss[1].first, uint16_t(2));
    CHECK_EQ(get16(d, ss[1].second + 4), uint16_t(0x9993));   // template id
    CHECK_EQ(get16(d, ss[1].second + 6), uint16_t(5));        // field count

    // Receiver record.
    CHECK_EQ(ss[2].first, uint16_t(0x9992));
    size_t at = ss[2].second + 4;
    CHECK_EQ(getString(d, at), std::string("WA8Q"));
    CHECK_EQ(getString(d, at), std::string("EN81"));
    CHECK_EQ(getString(d, at), std::string("gtty 1.0.0"));

    // Sender records.
    CHECK_EQ(ss[3].first, uint16_t(0x9993));
    at = ss[3].second + 4;
    CHECK_EQ(getString(d, at), std::string("DL1ABC"));
    CHECK_EQ(get32(d, at), uint32_t(14083500)); at += 4;
    CHECK_EQ(getString(d, at), std::string("RTTY"));
    CHECK_EQ(d[at], uint8_t(1)); at += 1;     // informationSource: automatic
    CHECK_EQ(get32(d, at), uint32_t(1700000000)); at += 4;
    CHECK_EQ(getString(d, at), std::string("JA1XYZ/P"));
    CHECK_EQ(get32(d, at), uint32_t(7043200)); at += 4;
    CHECK_EQ(getString(d, at), std::string("RTTY"));
    at += 1;
    CHECK_EQ(get32(d, at), uint32_t(1700000060)); at += 4;
    CHECK(get16(d, ss[3].second + 2) - (at - ss[3].second) < 4);   // only padding left
  }

  // ------------------------------------------- without templates, and split
  {
    uint32_t seq = 0;
    auto ds = pskreporter::encode(rx, {}, false, 1, 2, seq);
    CHECK_EQ(ds.size(), size_t(1));
    CHECK_EQ(sets(ds[0]).size(), size_t(1));   // the receiver record only
    CHECK_EQ(seq, uint32_t(1));

    std::vector<PskSpot> many;
    for (int i = 0; i < 300; ++i) many.push_back({"K" + std::to_string(i) + "AB", 14080000u + i, "RTTY", 5});
    seq = 0;
    ds = pskreporter::encode(rx, many, true, 1, 2, seq);
    CHECK(ds.size() > 1);
    size_t spotsSeen = 0;
    for (const auto& d : ds) {
      CHECK(d.size() <= pskreporter::kMaxDatagram);
      auto ss = sets(d);
      CHECK_EQ(ss.size(), size_t(4));
      size_t at = ss[3].second + 4, end = ss[3].second + get16(d, ss[3].second + 2);
      while (at + 4 < end && d[at] != 0) { getString(d, at); at += 4; getString(d, at); at += 5; ++spotsSeen; }
    }
    CHECK_EQ(spotsSeen, size_t(300));
    CHECK_EQ(seq, static_cast<uint32_t>(300 + ds.size()));
  }

  // ----------------------------------------------------- reporter behaviour
  {
    std::vector<std::vector<uint8_t>> sent;
    PskReporter r;
    std::string err;
    CHECK(!r.openWithSink({"WA8Q", "", "gtty"}, nullptr, err));   // a locator is required
    CHECK(!r.isOpen());
    CHECK(r.openWithSink(rx, [&](const std::vector<uint8_t>& d) { sent.push_back(d); return true; }, err));

    const uint32_t t0 = 1700000000;
    CHECK(r.add("DL1ABC", 14083500, "RTTY", t0));
    CHECK(!r.add("DL1ABC", 14085000, "RTTY", t0 + 10));   // same band within five minutes
    CHECK(r.add("DL1ABC", 7043000, "RTTY", t0 + 10));     // another band
    CHECK(!r.add("", 14083500, "RTTY", t0));
    CHECK(!r.add("K1ABC", 0, "RTTY", t0));                // no frequency known
    CHECK_EQ(r.pending(), size_t(2));

    r.tick(t0 + 20);                                       // first batch goes at once
    CHECK_EQ(sent.size(), size_t(1));
    CHECK_EQ(r.pending(), size_t(0));
    CHECK_EQ(r.spotsSent(), uint64_t(2));

    CHECK(r.add("K1ABC", 14080000, "RTTY", t0 + 30));
    r.tick(t0 + 100);                                      // too soon after the last batch
    CHECK_EQ(sent.size(), size_t(1));
    r.tick(t0 + 20 + PskReporter::kInterval);
    CHECK_EQ(sent.size(), size_t(2));
    r.tick(t0 + 2000);                                     // nothing queued: nothing sent
    CHECK_EQ(sent.size(), size_t(2));

    CHECK(r.add("DL1ABC", 14083500, "RTTY", t0 + 400));    // five minutes on: again
    r.tick(t0 + 2000);
    CHECK(r.add("W1AW", 14081000, "RTTY", t0 + 2100));
    r.close(t0 + 2100);                                    // close sends what is queued
    CHECK_EQ(sent.size(), size_t(4));
    // Templates go in the first three batches and then hourly.
    CHECK_EQ(sets(sent[2]).size(), size_t(4));
    CHECK_EQ(sets(sent[3]).size(), size_t(2));
    CHECK(!r.isOpen());
  }

  // ------------------------------------------ over UDP, resolved in the background
  {
    int srv = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
    socklen_t len = sizeof a;
    ::getsockname(srv, reinterpret_cast<sockaddr*>(&a), &len);
    timeval tv{2, 0};
    ::setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    PskReporter r;
    std::string err;
    CHECK(r.open(rx, err, "127.0.0.1", ntohs(a.sin_port)));
    CHECK(r.add("DL1ABC", 14083500, "RTTY", 1700000000));
    for (int i = 0; i < 200 && r.pending(); ++i) {   // queued until the lookup is done
      r.tick(1700000001);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_EQ(r.pending(), size_t(0));
    std::vector<uint8_t> buf(2048);
    ssize_t n = ::recv(srv, buf.data(), buf.size(), 0);
    CHECK(n > 16);
    if (n > 16) {
      buf.resize(static_cast<size_t>(n));
      CHECK_EQ(get16(buf, 0), uint16_t(0x000A));
      CHECK_EQ(sets(buf).size(), size_t(4));
    }
    r.close(1700000002);
    ::close(srv);
  }

  TEST_MAIN_END();
}
