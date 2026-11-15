// Thread-safe Hamlib wrapper with a background poller for frequency/mode.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/rig/serial_ptt.h"

struct s_rig;

namespace gtty {

struct RigModelInfo {
  int model;
  std::string mfg, name, version, status;
};

struct RigConfig {
  bool enabled = false;
  int model = 1;                 // Hamlib model number (1 = dummy, 2 = NET rigctl)
  std::string device;            // /dev/ttyUSB0 or host:port for model 2
  int baud = 0;                  // 0 = backend default
  std::string pttType = "RIG";   // RIG, DTR, RTS, NONE
  std::string pttDevice;         // serial device for DTR/RTS PTT (empty = rig device)
  int pollMs = 250;

  // DTR/RTS PTT on a port other than the rig's: gtty keys that line itself (SerialPtt),
  // whatever the rig backend is and whether or not the rig is connected.
  bool pttOnOwnPort() const {
    return (pttType == "DTR" || pttType == "RTS") && !pttDevice.empty() && pttDevice != device;
  }
};

struct RigState {
  bool connected = false;
  double freqHz = 0;
  std::string mode;
  int widthHz = 0;
  // Set in one poll only: the filter width that was asked for and that the rig did not
  // take. widthHz is what the rig has instead.
  int widthRefusedHz = 0;
  bool ptt = false;
  // IF shift as the rig reports it (RIG_LEVEL_IF). ifShiftKnown is false when the backend
  // cannot read it, so a control showing it has nothing to start from.
  int ifShiftHz = 0;
  bool ifShiftKnown = false;
  std::string error;
};

// Checks that a filter width that was set is the one the rig ends up with. Hamlib reports
// success whether or not the radio took the width, and for a while afterwards it (and a
// rigctld in between) answers from its cache with the width that was asked for. So the
// answer is only believed once that cache has expired.
class WidthCheck {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::milliseconds kSettle{1500};

  void asked(int hz, Clock::time_point now) { askedHz_ = hz; due_ = now + kSettle; }
  void cancel() { askedHz_ = 0; }
  // Call with each width read from the rig (0 = not known). Returns the width asked for
  // when the rig has settled on a different one, once; 0 otherwise.
  int polled(int reportedHz, Clock::time_point now) {
    if (askedHz_ <= 0 || now < due_) return 0;
    int asked = askedHz_;
    askedHz_ = 0;
    return reportedHz > 0 && reportedHz != asked ? asked : 0;
  }

 private:
  int askedHz_ = 0;
  Clock::time_point due_{};
};

class RigControl {
 public:
  RigControl();
  ~RigControl();
  RigControl(const RigControl&) = delete;
  RigControl& operator=(const RigControl&) = delete;

  static std::vector<RigModelInfo> listModels();

  bool open(const RigConfig& cfg, std::string& err);
  // Opens the PTT port when cfg.pttOnOwnPort(); otherwise does nothing and returns true.
  // Independent of open(): the line can be keyed with rig control off or failed.
  bool openPttPort(const RigConfig& cfg, std::string& err);
  void close();   // the rig and the PTT port
  bool isOpen() const { return rig_ != nullptr; }
  // True when PTT is this object's to key: a PTT port is configured (setPtt() fails with
  // the reason if it could not be opened), or else the rig is open.
  bool canPtt() const { return ownPttPort_ || rig_ != nullptr; }
  // PTT is a DTR/RTS line on a serial port of its own, keyed here and not by the rig backend.
  bool ownsPttLine() const { return ownPttPort_; }
  const RigConfig& config() const { return cfg_; }

  RigState state() const;
  bool setFreq(double hz);
  // mode: Hamlib mode name (RTTY, RTTYR, USB, LSB, PKTUSB ...). width -1 = no change.
  bool setMode(const std::string& mode, int widthHz = -1);
  bool setPtt(bool on);
  // Receiver filter: passband width in Hz for the current mode (rig_set_mode). True means
  // only that the command went out: a rig that then stays on another width is reported
  // by the poller in RigState::widthRefusedHz.
  bool setWidth(int hz);
  // IF shift in Hz (RIG_LEVEL_IF); hasIfShift() tells whether the backend supports it.
  bool setIfShift(int hz);
  bool hasIfShift() const;
  // Sends text through the rig's own keyer (Hamlib rig_send_morse). In RTTY/FSK
  // mode on Elecraft K3/K4/KX3 and Kenwood TS-890 this keys FSK directly.
  bool sendText(const std::string& text);
  bool stopText();

  std::function<void(const RigState&)> onState;  // called from the poller thread

 private:
  void pollLoop();
  void setError(const std::string& e);
  void readIfShift();   // with mu_ held

  void closeRig();

  s_rig* rig_ = nullptr;
  SerialPtt pttPort_;
  bool ownPttPort_ = false;      // PTT is on pttPort_ (open or not), never on the rig
  std::string pttPortError_;
  RigConfig cfg_;
  mutable std::mutex mu_;      // guards rig_ calls and state_
  RigState state_;
  WidthCheck widthCheck_;
  std::thread poller_;
  std::atomic<bool> run_{false};
};

std::string bandForFrequency(double hz);

}  // namespace gtty
