// The serial ports offered in Preferences, listed from a fake /dev and /sys.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/rig/serial_ports.h"
#include "check.h"

using namespace gtty;
namespace fs = std::filesystem;

static void touch(const fs::path& p) { std::ofstream(p).put('\n'); }
static void write(const fs::path& p, const char* s) { fs::create_directories(p.parent_path()); std::ofstream(p) << s; }

int main() {
  fs::path root = fs::temp_directory_path() / ("gtty_serial_ports_" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::path dev = root / "dev", sys = root / "sys";
  fs::create_directories(dev / "serial" / "by-id");
  for (const char* n : {"ttyUSB10", "ttyUSB2", "ttyACM0", "ttyS0", "ttyS1", "tty0", "null", "cu.usbserial-1"}) touch(dev / n);
  fs::create_symlink("../../ttyUSB2", dev / "serial" / "by-id" / "usb-Silicon_Labs_CP2105-if00-port0");
  write(sys / "class" / "tty" / "ttyS0" / "type", "4\n");   // a 16550A
  write(sys / "class" / "tty" / "ttyS1" / "type", "0\n");   // no UART behind it

  auto ports = listSerialPorts(dev.string(), sys.string());
  CHECK_EQ(ports.size(), size_t(6));
  if (ports.size() == 6) {
    CHECK_EQ(ports[0].path, (dev / "serial" / "by-id" / "usb-Silicon_Labs_CP2105-if00-port0").string());
    CHECK_EQ(ports[0].detail, std::string("ttyUSB2"));
    CHECK_EQ(ports[1].path, (dev / "ttyUSB2").string());    // 2 before 10
    CHECK_EQ(ports[2].path, (dev / "ttyUSB10").string());
    CHECK_EQ(ports[3].path, (dev / "ttyACM0").string());
    CHECK_EQ(ports[4].path, (dev / "ttyS0").string());
    CHECK_EQ(ports[5].path, (dev / "cu.usbserial-1").string());
  }
  CHECK(listSerialPorts((root / "missing").string(), sys.string()).empty());
  fs::remove_all(root);
  TEST_MAIN_END();
}
