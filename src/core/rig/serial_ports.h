// Serial ports present on this machine, offered as suggestions where a device is typed in.
#pragma once
#include <string>
#include <vector>

namespace gtty {

struct SerialPortInfo {
  std::string path;     // what goes in the device field
  std::string detail;   // a hint for the person choosing, e.g. the node a by-id link points at
};

// Linux: the stable /dev/serial/by-id links first (they keep their name when the USB
// devices are plugged in a different order), then ttyUSB*, ttyACM*, and the ttyS* ports
// that have a UART behind them. macOS: /dev/cu.*. The roots can be moved for tests.
std::vector<SerialPortInfo> listSerialPorts(const std::string& devRoot = "/dev",
                                            const std::string& sysRoot = "/sys");

}  // namespace gtty
