// Hands a logged QSO to a logbook program the way fldigi hands it to fllog:
// one XML-RPC call, log.add_record, whose only parameter is the QSO as an ADIF
// record. fllog and QLog (both on port 8421) take QSOs this way.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gtty {

// Blocking, with a time-out of a few seconds on each step: call it off the UI
// thread. False with `err` set when the server cannot be reached or refuses.
bool fllogAddRecord(const std::string& host, uint16_t port, const std::string& adifRecord, std::string& err);

namespace net {
// A TCP connection to host:port, made with a time-out of `timeoutMs`. The
// socket comes back non-blocking. -1 with `err` set when it cannot be made.
int tcpConnect(const std::string& host, uint16_t port, int timeoutMs, std::string& err);
}  // namespace net

// Sends records one after the other from a thread of its own, so a logbook
// that is slow or switched off never holds up the caller. `send` does the
// delivery of one record: fllogAddRecord by default, xlogSend for Xlog.
class RecordForwarder {
 public:
  using SendFn = std::function<bool(const std::string& host, uint16_t port, const std::string& record, std::string& err)>;
  explicit RecordForwarder(SendFn send = fllogAddRecord) : send_(std::move(send)) {}
  ~RecordForwarder();
  RecordForwarder(const RecordForwarder&) = delete;
  RecordForwarder& operator=(const RecordForwarder&) = delete;

  void setTarget(const std::string& host, uint16_t port);
  // `label` (the callsign) only names the record in onResult.
  void add(const std::string& label, const std::string& record);
  void stop();   // waits for the record being sent, drops the ones still queued

  // Runs on the forwarder's thread, once per record.
  std::function<void(bool ok, const std::string& label, const std::string& err)> onResult;

 private:
  struct Job { std::string host; uint16_t port; std::string label, record; };
  void loop();
  SendFn send_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Job> jobs_;
  std::thread thread_;
  bool run_ = false;
  std::string host_ = "127.0.0.1";
  uint16_t port_ = 8421;
};

}  // namespace gtty
