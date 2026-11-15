// PSK Reporter (https://pskreporter.info) client: reports the stations gtty
// decodes so they appear on the PSK Reporter map, as WSJT-X and fldigi do.
//
// The protocol is IPFIX (RFC 7011) over UDP, as described at
// https://pskreporter.info/pskdev.html: each datagram carries one receiver
// record (our callsign, locator and software) and a batch of sender records
// (callsign, frequency, mode, time). The templates that describe the two
// record layouts go in the first few datagrams and then about once an hour.
// Datagrams go out no more often than every five minutes, and a station is
// reported once per band in that time.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gtty {

struct PskReceiverInfo {
  std::string callsign;
  std::string locator;    // Maidenhead, 4 or 6 characters
  std::string software;   // e.g. "gtty 1.2.3"
};

struct PskSpot {
  std::string callsign;
  uint32_t freqHz = 0;
  std::string mode;       // "RTTY"
  uint32_t unixTime = 0;
};

namespace pskreporter {

constexpr const char* kHost = "report.pskreporter.info";
constexpr uint16_t kPort = 4739;       // 14739 accepts the same datagrams but does not store them
constexpr size_t kMaxDatagram = 1400;  // stay under a typical path MTU

// The datagrams carrying `spots`, each no larger than kMaxDatagram, every one
// starting with the receiver record. With `templates`, each also carries the
// template sets first. `seq` is the IPFIX sequence number of the first; it is
// advanced by the number of data records sent. No spots: one datagram with
// only the receiver record (and the templates, if asked for).
std::vector<std::vector<uint8_t>> encode(const PskReceiverInfo& rx, const std::vector<PskSpot>& spots,
                                         bool templates, uint32_t observationDomain, uint32_t exportTime,
                                         uint32_t& seq);

}  // namespace pskreporter

class PskReporter {
 public:
  using Sink = std::function<bool(const std::vector<uint8_t>&)>;

  PskReporter() = default;
  ~PskReporter();
  PskReporter(const PskReporter&) = delete;
  PskReporter& operator=(const PskReporter&) = delete;

  // Get ready to report as `rx` to `host`, which is resolved in the background; spots
  // wait in the queue until it is. Callsign and locator are required.
  bool open(const PskReceiverInfo& rx, std::string& err, const std::string& host = pskreporter::kHost,
            uint16_t port = pskreporter::kPort);
  // Like open() but hands datagrams to `sink` instead of the network (tests).
  bool openWithSink(const PskReceiverInfo& rx, Sink sink, std::string& err);
  // Send what is queued, then stop.
  void close(uint32_t nowUnix);
  bool isOpen() const { return open_; }

  // Queue a decoded station unless it was already reported on this band in the
  // last five minutes. Returns whether it was queued.
  bool add(const std::string& callsign, double freqHz, const std::string& mode, uint32_t nowUnix);

  // Call regularly: sends the queue once the reporting interval has passed.
  void tick(uint32_t nowUnix);

  size_t pending() const { return queue_.size(); }
  uint64_t spotsSent() const { return sent_; }

  static constexpr uint32_t kInterval = 300;        // seconds between datagram batches
  static constexpr uint32_t kTemplateEvery = 3600;  // resend templates this often

 private:
  // The socket and its destination, filled in by a resolver thread that may outlive us.
  struct Net {
    std::mutex mu;
    bool busy = false;    // a lookup is running
    int fd = -1;
    std::vector<uint8_t> dest;
    std::string err;      // the last lookup failed
    ~Net();
  };

  bool start(const PskReceiverInfo& rx, std::string& err);
  void resolve(uint32_t nowUnix);
  void flush(uint32_t nowUnix);

  bool open_ = false;
  std::shared_ptr<Net> net_;
  std::string host_;
  uint16_t port_ = 0;
  uint32_t lastResolve_ = 0;
  Sink sink_;
  PskReceiverInfo rx_;
  uint32_t domain_ = 0;
  uint32_t seq_ = 0;
  uint32_t lastSend_ = 0;       // 0: nothing sent yet
  uint32_t lastTemplates_ = 0;
  int templateBatches_ = 0;     // batches sent with templates since open
  std::vector<PskSpot> queue_;
  std::map<std::string, uint32_t> recent_;  // "CALL/band" -> when it was last queued
  uint64_t sent_ = 0;
};

}  // namespace gtty
