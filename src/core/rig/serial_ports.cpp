#include "core/rig/serial_ports.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace gtty {

namespace fs = std::filesystem;

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// ttyUSB2 before ttyUSB10.
bool naturalLess(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
      while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
      std::string na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
      na.erase(0, std::min(na.find_first_not_of('0'), na.size()));
      nb.erase(0, std::min(nb.find_first_not_of('0'), nb.size()));
      if (na.size() != nb.size()) return na.size() < nb.size();
      if (na != nb) return na < nb;
      i = i2; j = j2;
    } else {
      if (a[i] != b[j]) return a[i] < b[j];
      ++i; ++j;
    }
  }
  return a.size() - i < b.size() - j;
}

std::vector<std::string> namesIn(const fs::path& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    out.push_back(it->path().filename().string());
  std::sort(out.begin(), out.end(), naturalLess);
  return out;
}

// The kernel makes ttyS0..ttyS31 whether or not a UART is there; type 0 means none.
bool uartPresent(const fs::path& sysRoot, const std::string& name) {
  std::ifstream f(sysRoot / "class" / "tty" / name / "type");
  int type = 0;
  return f && (f >> type) && type != 0;
}

}  // namespace

std::vector<SerialPortInfo> listSerialPorts(const std::string& devRootStr, const std::string& sysRootStr) {
  const fs::path devRoot(devRootStr), sysRoot(sysRootStr);
  std::vector<SerialPortInfo> out;
  std::error_code ec;

  for (const auto& n : namesIn(devRoot / "serial" / "by-id")) {
    fs::path link = devRoot / "serial" / "by-id" / n;
    fs::path target = fs::read_symlink(link, ec);
    std::string detail = ec ? std::string() : target.filename().string();
    out.push_back({link.string(), detail});
  }
  std::vector<std::string> devs = namesIn(devRoot);
  for (const char* prefix : {"ttyUSB", "ttyACM"})
    for (const auto& n : devs)
      if (startsWith(n, prefix)) out.push_back({(devRoot / n).string(), ""});
  for (const auto& n : devs)
    if (startsWith(n, "ttyS") && uartPresent(sysRoot, n)) out.push_back({(devRoot / n).string(), "built-in UART"});
  for (const auto& n : devs)
    if (startsWith(n, "cu.") && n != "cu.Bluetooth-Incoming-Port") out.push_back({(devRoot / n).string(), ""});
  return out;
}

}  // namespace gtty
