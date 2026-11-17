#include "core/net/xlog_client.h"

#include "core/net/fllog_client.h"
#include "core/net/socket_compat.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace gtty {

namespace {

constexpr int kTimeoutMs = 4000;
// Xlog reads one block of at most 1024 bytes and terminates it in place, so a
// record of 1024 bytes or more is cut short (and overruns its buffer).
constexpr size_t kMaxRecord = 1000;

// Xlog splits the record on '\1' and turns newlines and tabs into spaces.
std::string clean(const std::string& v) {
  std::string out;
  out.reserve(v.size());
  for (char c : v) {
    if (c == '\1') continue;
    out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  }
  return out;
}

// "2026-11-12 18:04:59" -> "12 Nov 2026" and "1804", the forms Xlog asks for.
bool splitTime(const std::string& t, std::string& date, std::string& hhmm) {
  int y, mo, d, h, mi;
  if (std::sscanf(t.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5 || mo < 1 || mo > 12) return false;
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[32];
  std::snprintf(buf, sizeof buf, "%02d %s %04d", d, kMonths[mo - 1], y);
  date = buf;
  std::snprintf(buf, sizeof buf, "%02d%02d", h, mi);
  hhmm = buf;
  return true;
}

}  // namespace

std::string xlogRemoteRecord(const Qso& q, const std::string& program) {
  std::vector<std::pair<std::string, std::string>> f;
  f.emplace_back("program", program);
  f.emplace_back("version", "1");
  std::string date, hhmm;
  if (splitTime(q.timeUtc, date, hhmm)) {
    f.emplace_back("date", date);
    f.emplace_back("time", hhmm);
  }
  f.emplace_back("call", q.call);
  if (q.freqHz > 0) {
    char mhz[32];
    std::snprintf(mhz, sizeof mhz, "%.6f", q.freqHz / 1e6);
    f.emplace_back("mhz", mhz);
  }
  f.emplace_back("mode", q.mode);
  f.emplace_back("tx", q.rstSent);
  f.emplace_back("rx", q.rstRcvd);
  f.emplace_back("name", q.name);
  f.emplace_back("qth", q.qth);
  f.emplace_back("locator", q.grid);
  // The contest exchange: what came back in Xlog's first free field, what went
  // out in the second. Xlog appends them to the remarks when the fields are hidden.
  f.emplace_back("free1", q.exchRcvd);
  f.emplace_back("free2", q.exchSent);
  f.emplace_back("notes", q.comment);

  for (auto& [name, value] : f) value = clean(value);
  auto build = [&] {
    std::string r;
    for (auto& [name, value] : f) {
      if (value.empty()) continue;   // an empty value would add a stray ", " to Xlog's remarks
      r += name + ":" + value + '\1';
    }
    return r;
  };
  std::string r = build();
  // Too long: give up the free-text fields first, longest-lived last.
  for (const char* shorten : {"notes", "qth", "name"}) {
    if (r.size() <= kMaxRecord) break;
    for (auto& [name, value] : f) {
      if (name != shorten) continue;
      size_t excess = r.size() - kMaxRecord;
      value.resize(value.size() > excess ? value.size() - excess : 0);
    }
    r = build();
  }
  if (r.size() > kMaxRecord) r.resize(kMaxRecord);
  return r;
}

bool xlogSend(const std::string& host, uint16_t port, const std::string& record, std::string& err) {
  int fd = net::tcpConnect(host, port, kTimeoutMs, err);
  if (fd < 0) {
    if (err.empty()) err = "cannot connect";
    return false;
  }
  // One write: Xlog reads once when the socket becomes readable, then closes it.
  bool ok = true;
  size_t off = 0;
  while (off < record.size()) {
    ssize_t n = ::send(fd, record.data() + off, record.size() - off, net::sendFlags());
    if (n > 0) { off += static_cast<size_t>(n); continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd p{fd, POLLOUT, 0};
      if (::poll(&p, 1, kTimeoutMs) > 0) continue;
    }
    err = n < 0 ? std::strerror(errno) : "connection closed";
    ok = false;
    break;
  }
  ::close(fd);
  return ok;
}

}  // namespace gtty
