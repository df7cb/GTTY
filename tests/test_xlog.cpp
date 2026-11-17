// Logged QSOs handed to Xlog as "remote data". The server here reads the way
// Xlog 2.0.25's tcp_client_activity_7311() does: one read of at most 1024
// bytes, then the connection is closed; the record is split on '\1' and each
// field on its first ':'.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/net/fllog_client.h"
#include "core/net/socket_compat.h"
#include "core/net/xlog_client.h"
#include "check.h"

using namespace gtty;

namespace {

std::map<std::string, std::string> parse(const std::string& rec) {
  std::map<std::string, std::string> out;
  size_t start = 0;
  while (start < rec.size()) {
    size_t end = rec.find('\1', start);
    if (end == std::string::npos) end = rec.size();
    std::string f = rec.substr(start, end - start);
    size_t colon = f.find(':');
    if (colon != std::string::npos) out[f.substr(0, colon)] = f.substr(colon + 1);
    start = end + 1;
  }
  return out;
}

struct FakeXlog {
  int fd = -1;
  uint16_t port = 0;
  std::thread thread;
  std::atomic<bool> run{true};
  std::mutex mu;
  std::vector<std::string> records;

  bool start() {
    fd = net::socketFd(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    for (uint16_t p = 17311; p < 17331; ++p) {
      sockaddr_in sa{};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(p);
      inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
      if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == 0 && ::listen(fd, 4) == 0) { port = p; break; }
    }
    if (!port) return false;
    thread = std::thread([this] { loop(); });
    return true;
  }
  void stop() {
    run = false;
    if (thread.joinable()) thread.join();
    if (fd >= 0) ::close(fd);
  }
  void loop() {
    while (run) {
      pollfd p{fd, POLLIN, 0};
      if (::poll(&p, 1, 50) <= 0) continue;
      int c = ::accept(fd, nullptr, nullptr);
      if (c < 0) continue;
      pollfd cp{c, POLLIN, 0};
      if (::poll(&cp, 1, 2000) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        char buf[1024];
        ssize_t n = ::recv(c, buf, sizeof buf, 0);
        if (n > 0) {
          std::lock_guard<std::mutex> lk(mu);
          records.emplace_back(buf, static_cast<size_t>(n));
        }
      }
      ::close(c);
    }
  }
};

}  // namespace

int main() {
  Qso q;
  q.timeUtc = "2026-11-02 08:04:59";
  q.call = "DL1ABC";
  q.band = "20m";
  q.freqHz = 14083500;
  q.rstSent = "599";
  q.rstRcvd = "579";
  q.exchSent = "012";
  q.exchRcvd = "034";
  q.name = "Hans";
  q.grid = "JO62";
  q.comment = "first line\nsecond\1line";

  // ------------------------------------------------------------ the record
  std::string rec = xlogRemoteRecord(q, "gtty v 1.0");
  CHECK(rec.compare(0, 19, "program:gtty v 1.0\1") == 0);
  CHECK(rec.back() == '\1');
  auto f = parse(rec);
  CHECK_EQ(f["version"], std::string("1"));
  CHECK_EQ(f["date"], std::string("02 Nov 2026"));
  CHECK_EQ(f["time"], std::string("0804"));
  CHECK_EQ(f["call"], std::string("DL1ABC"));
  CHECK_EQ(f["mhz"], std::string("14.083500"));
  CHECK_EQ(f["mode"], std::string("RTTY"));
  CHECK_EQ(f["tx"], std::string("599"));
  CHECK_EQ(f["rx"], std::string("579"));
  CHECK_EQ(f["name"], std::string("Hans"));
  CHECK_EQ(f["locator"], std::string("JO62"));
  CHECK_EQ(f["free1"], std::string("034"));
  CHECK_EQ(f["free2"], std::string("012"));
  CHECK_EQ(f["notes"], std::string("first line secondline"));   // no newline, no stray separator
  CHECK(f.find("qth") == f.end());                               // empty fields are left out
  CHECK(rec.find("qth:") == std::string::npos);

  // No frequency, an unreadable time: those fields are left out, Xlog fills them in.
  Qso bare;
  bare.call = "K1ABC";
  bare.timeUtc = "garbage";
  auto fb = parse(xlogRemoteRecord(bare, "gtty"));
  CHECK(fb.find("mhz") == fb.end());
  CHECK(fb.find("date") == fb.end());
  CHECK(fb.find("time") == fb.end());
  CHECK_EQ(fb["call"], std::string("K1ABC"));

  // A record that would not fit in Xlog's 1024-byte read is shortened, notes first.
  Qso big = q;
  big.comment = std::string(2000, 'n');
  big.qth = std::string(300, 'q');
  std::string bigRec = xlogRemoteRecord(big, "gtty");
  CHECK(bigRec.size() <= 1000);
  auto fbig = parse(bigRec);
  CHECK_EQ(fbig["call"], std::string("DL1ABC"));
  CHECK_EQ(fbig["qth"].size(), size_t(300));      // kept: shortening the notes was enough
  CHECK(fbig["notes"].size() < 2000);
  CHECK(bigRec.back() == '\1');

  // ------------------------------------------------------------ over the wire
  FakeXlog xlog;
  CHECK(xlog.start());
  if (!xlog.port) TEST_MAIN_END();

  std::string err;
  CHECK(xlogSend("127.0.0.1", xlog.port, rec, err));
  for (int i = 0; i < 200; ++i) {
    { std::lock_guard<std::mutex> lk(xlog.mu); if (!xlog.records.empty()) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  {
    std::lock_guard<std::mutex> lk(xlog.mu);
    CHECK_EQ(xlog.records.size(), size_t(1));
    if (!xlog.records.empty()) CHECK_EQ(xlog.records[0], rec);   // arrives whole in Xlog's single read
  }

  // Nobody listening: an error, promptly.
  uint16_t dead = static_cast<uint16_t>(xlog.port + 100);
  auto t0 = std::chrono::steady_clock::now();
  CHECK(!xlogSend("127.0.0.1", dead, rec, err));
  CHECK(!err.empty());
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));

  // Through the forwarder, as the GUI sends it.
  {
    RecordForwarder fwd{xlogSend};
    std::mutex rm;
    std::vector<std::string> results;
    fwd.onResult = [&](bool ok, const std::string& label, const std::string&) {
      std::lock_guard<std::mutex> lk(rm);
      results.push_back(label + (ok ? ":ok" : ":failed"));
    };
    fwd.setTarget("127.0.0.1", xlog.port);
    fwd.add("K1AAA", "program:gtty\1call:K1AAA\1");
    for (int i = 0; i < 300; ++i) {
      { std::lock_guard<std::mutex> lk(rm); if (!results.empty()) break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::lock_guard<std::mutex> lk(rm);
    CHECK_EQ(results.size(), size_t(1));
    if (!results.empty()) CHECK_EQ(results[0], std::string("K1AAA:ok"));
  }
  for (int i = 0; i < 200; ++i) {
    { std::lock_guard<std::mutex> lk(xlog.mu); if (xlog.records.size() >= 2) break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  {
    std::lock_guard<std::mutex> lk(xlog.mu);
    CHECK_EQ(xlog.records.size(), size_t(2));
    if (xlog.records.size() == 2) CHECK_EQ(parse(xlog.records[1])["call"], std::string("K1AAA"));
  }

  xlog.stop();
  TEST_MAIN_END();
}
