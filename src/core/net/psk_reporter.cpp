#include "core/net/psk_reporter.h"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iterator>
#include <random>
#include <thread>

#include "core/net/socket_compat.h"

namespace gtty {

namespace pskreporter {
namespace {

constexpr uint16_t kReceiverTemplate = 0x9992;
constexpr uint16_t kSenderTemplate = 0x9993;

// Options template set (set id 3): receiverCallsign, receiverLocator, decodingSoftware,
// all variable length, PSK Reporter's enterprise number 30351. Padded to 36 bytes.
const uint8_t kReceiverTemplateSet[] = {
    0x00, 0x03, 0x00, 0x24, 0x99, 0x92, 0x00, 0x03, 0x00, 0x00,
    0x80, 0x02, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x04, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x00, 0x00,
};

// Template set (set id 2): senderCallsign (variable), frequency (4 bytes, Hz), mode (variable),
// informationSource (1 byte), flowStartSeconds (IANA element 150, 4 bytes).
const uint8_t kSenderTemplateSet[] = {
    0x00, 0x02, 0x00, 0x2C, 0x99, 0x93, 0x00, 0x05,
    0x80, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x05, 0x00, 0x04, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x0A, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x0B, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
    0x00, 0x96, 0x00, 0x04,
};

constexpr uint8_t kInfoSourceAutomatic = 1;

void put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
  put16(b, static_cast<uint16_t>(v >> 16));
  put16(b, static_cast<uint16_t>(v));
}
void set16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
  b[at] = static_cast<uint8_t>(v >> 8);
  b[at + 1] = static_cast<uint8_t>(v);
}
// IPFIX variable-length field, short form: one length byte (< 255), then the bytes.
void putString(std::vector<uint8_t>& b, const std::string& s) {
  size_t n = std::min<size_t>(s.size(), 254);
  b.push_back(static_cast<uint8_t>(n));
  b.insert(b.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

std::vector<uint8_t> spotRecord(const PskSpot& s) {
  std::vector<uint8_t> r;
  putString(r, s.callsign);
  put32(r, s.freqHz);
  putString(r, s.mode);
  r.push_back(kInfoSourceAutomatic);
  put32(r, s.unixTime);
  return r;
}

// Close the set started at `start`: pad to a multiple of 4 and write its length.
void endSet(std::vector<uint8_t>& b, size_t start) {
  while ((b.size() - start) % 4) b.push_back(0);
  set16(b, start + 2, static_cast<uint16_t>(b.size() - start));
}

}  // namespace

std::vector<std::vector<uint8_t>> encode(const PskReceiverInfo& rx, const std::vector<PskSpot>& spots,
                                         bool templates, uint32_t observationDomain, uint32_t exportTime,
                                         uint32_t& seq) {
  std::vector<std::vector<uint8_t>> out;
  size_t next = 0;
  do {
    std::vector<uint8_t> b;
    put16(b, 0x000A);   // IPFIX version
    put16(b, 0);        // length, filled in below
    put32(b, exportTime);
    put32(b, seq);
    put32(b, observationDomain);
    if (templates) {
      b.insert(b.end(), std::begin(kReceiverTemplateSet), std::end(kReceiverTemplateSet));
      b.insert(b.end(), std::begin(kSenderTemplateSet), std::end(kSenderTemplateSet));
    }
    size_t set = b.size();
    put16(b, kReceiverTemplate);
    put16(b, 0);
    putString(b, rx.callsign);
    putString(b, rx.locator);
    putString(b, rx.software);
    endSet(b, set);
    uint32_t records = 1;

    if (next < spots.size()) {
      set = b.size();
      put16(b, kSenderTemplate);
      put16(b, 0);
      while (next < spots.size()) {
        std::vector<uint8_t> r = spotRecord(spots[next]);
        // Keep room for the set's padding (at most 3 bytes). A datagram takes at least one spot.
        if (b.size() + r.size() + 3 > kMaxDatagram && b.size() > set + 4) break;
        b.insert(b.end(), r.begin(), r.end());
        ++records;
        ++next;
      }
      endSet(b, set);
    }
    set16(b, 2, static_cast<uint16_t>(b.size()));
    seq += records;
    out.push_back(std::move(b));
  } while (next < spots.size());
  return out;
}

}  // namespace pskreporter

PskReporter::Net::~Net() {
  if (fd >= 0) ::close(fd);
}

PskReporter::~PskReporter() = default;

bool PskReporter::start(const PskReceiverInfo& rx, std::string& err) {
  if (rx.callsign.empty()) { err = "set your callsign first"; return false; }
  if (rx.locator.size() < 4) { err = "set your grid locator first"; return false; }
  rx_ = rx;
  std::random_device rd;
  domain_ = rd();
  seq_ = 0;
  lastSend_ = 0;
  lastTemplates_ = 0;
  templateBatches_ = 0;
  queue_.clear();
  recent_.clear();
  open_ = true;
  return true;
}

bool PskReporter::open(const PskReceiverInfo& rx, std::string& err, const std::string& host, uint16_t port) {
  close(0);
  if (!start(rx, err)) return false;
  host_ = host;
  port_ = port;
  net_ = std::make_shared<Net>();
  std::weak_ptr<Net> weak = net_;
  sink_ = [weak](const std::vector<uint8_t>& d) {
    auto net = weak.lock();
    if (!net) return false;
    std::lock_guard<std::mutex> lock(net->mu);
    if (net->fd < 0) return false;
    ssize_t n = ::sendto(net->fd, d.data(), d.size(), 0, reinterpret_cast<const sockaddr*>(net->dest.data()),
                         static_cast<socklen_t>(net->dest.size()));
    return n == static_cast<ssize_t>(d.size());
  };
  resolve(0);
  return true;
}

// Look the host up on a thread of its own, so a slow resolver never holds up the caller.
void PskReporter::resolve(uint32_t nowUnix) {
  lastResolve_ = nowUnix;
  {
    std::lock_guard<std::mutex> lock(net_->mu);
    if (net_->busy) return;
    net_->busy = true;
  }
  std::thread([net = net_, host = host_, port = port_] {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    std::lock_guard<std::mutex> lock(net->mu);
    net->busy = false;
    if (rc != 0 || !res) { net->err = "resolve " + host + ": " + gai_strerror(rc); return; }
    int fd = gtty::net::socketFd(res->ai_family, SOCK_DGRAM, 0, /*nonblock=*/true);
    if (fd < 0) { net->err = strerror(errno); freeaddrinfo(res); return; }
    net->fd = fd;
    net->dest.assign(reinterpret_cast<uint8_t*>(res->ai_addr),
                     reinterpret_cast<uint8_t*>(res->ai_addr) + res->ai_addrlen);
    net->err.clear();
    freeaddrinfo(res);
  }).detach();
}

bool PskReporter::openWithSink(const PskReceiverInfo& rx, Sink sink, std::string& err) {
  close(0);
  if (!start(rx, err)) return false;
  sink_ = std::move(sink);
  return true;
}

void PskReporter::close(uint32_t nowUnix) {
  bool ready = !net_;
  if (net_) { std::lock_guard<std::mutex> lock(net_->mu); ready = net_->fd >= 0; }
  if (open_ && ready && !queue_.empty() && nowUnix) flush(nowUnix);
  open_ = false;
  sink_ = nullptr;
  net_.reset();   // a lookup still running keeps its own reference
  queue_.clear();
}

bool PskReporter::add(const std::string& callsign, double freqHz, const std::string& mode, uint32_t nowUnix) {
  if (!open_ || callsign.empty() || freqHz <= 0 || freqHz >= 4.29e9) return false;
  if (queue_.size() >= 1000) return false;   // offline for a while: do not grow without bound
  // One report per station and band (approximated by the MHz) per interval.
  std::string key = callsign + "/" + std::to_string(static_cast<long>(freqHz / 1e6));
  auto it = recent_.find(key);
  if (it != recent_.end() && nowUnix - it->second < kInterval) return false;
  if (recent_.size() > 2000) {
    for (auto i = recent_.begin(); i != recent_.end();)
      i = (nowUnix - i->second >= kInterval) ? recent_.erase(i) : std::next(i);
  }
  recent_[key] = nowUnix;
  queue_.push_back({callsign, static_cast<uint32_t>(std::lround(freqHz)), mode, nowUnix});
  return true;
}

void PskReporter::tick(uint32_t nowUnix) {
  if (!open_ || queue_.empty()) return;
  if (lastSend_ && nowUnix - lastSend_ < kInterval) return;
  if (net_) {
    bool ready, failed;
    {
      std::lock_guard<std::mutex> lock(net_->mu);
      ready = net_->fd >= 0;
      failed = !net_->busy && !ready;
    }
    if (!ready) {
      if (failed && nowUnix - lastResolve_ >= kInterval) resolve(nowUnix);   // try the lookup again
      return;
    }
  }
  flush(nowUnix);
}

void PskReporter::flush(uint32_t nowUnix) {
  bool templates = templateBatches_ < 3 || nowUnix - lastTemplates_ >= kTemplateEvery;
  for (const auto& d : pskreporter::encode(rx_, queue_, templates, domain_, nowUnix, seq_))
    if (sink_) sink_(d);
  if (templates) { ++templateBatches_; lastTemplates_ = nowUnix; }
  sent_ += queue_.size();
  queue_.clear();
  lastSend_ = nowUnix;
}

}  // namespace gtty
