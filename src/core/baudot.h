// Baudot / ITA2 5-level code: encoder (ASCII -> 5-bit codes with LTRS/FIGS
// shifts) and decoder (5-bit codes -> ASCII, honoring shift state and
// optional unshift-on-space).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gtty {

enum class BaudotVariant { ITA2, USTTY };

namespace baudot {
constexpr uint8_t BLANK = 0x00;
constexpr uint8_t LF    = 0x02;
constexpr uint8_t SPACE = 0x04;
constexpr uint8_t CR    = 0x08;
constexpr uint8_t FIGS  = 0x1B;
constexpr uint8_t LTRS  = 0x1F;

// Letters table (shared by both variants) and figures tables.
const char* lettersTable();                     // 32 entries
const char* figuresTable(BaudotVariant v);      // 32 entries

// What a received word would have read had the decoder been in the other shift. A noise
// hit on a FIGS or LTRS code leaves the rest of the word in the wrong case: a serial
// number 126 prints as "QWY" (the digits sit on the same codes as the top row of
// letters), a call DF7CB as "$!7:?". Returns the other reading when the word looks like
// such a casualty, and an empty string when it does not:
//   - only the letters Q W E R T Y U I O P, two or more: the digits ("WPO" -> "209")
//   - punctuation in it: each punctuation mark becomes its letter; letters and digits
//     stay, since digits are sent in figures anyway ("DF7:?" -> "DF7CB")
// toFigures says which of the two it was. Bracketed words such as the "[07:22:39]"
// timestamps are left alone.
std::string otherShiftReading(const std::string& word, BaudotVariant v, bool* toFigures = nullptr);
}  // namespace baudot

class BaudotEncoder {
 public:
  explicit BaudotEncoder(BaudotVariant v = BaudotVariant::ITA2) : variant_(v) {}
  void setVariant(BaudotVariant v) { variant_ = v; }
  // Mirror the receiver's unshift-on-space so a FIGS is re-sent after a space.
  void setUnshiftOnSpace(bool on) { uos_ = on; }

  // Codes to transmit for one ASCII character. Lower-case letters are folded
  // to upper case, '\n' becomes CR LF. Unsupported characters yield nothing.
  std::vector<uint8_t> encode(char c);
  std::vector<uint8_t> encode(const std::string& s);

  // Forget the current shift state so the next letter/figure is preceded by
  // an explicit shift code (use at the start of a transmission).
  void reset() { shift_ = Shift::Unknown; }

 private:
  enum class Shift { Unknown, Letters, Figures };
  BaudotVariant variant_;
  bool uos_ = true;
  Shift shift_ = Shift::Unknown;
};

class BaudotDecoder {
 public:
  explicit BaudotDecoder(BaudotVariant v = BaudotVariant::ITA2) : variant_(v) {}
  void setVariant(BaudotVariant v) { variant_ = v; }
  void setUnshiftOnSpace(bool on) { uos_ = on; }

  // Returns the decoded character: printable ASCII, '\n' or '\r'. nullopt for the
  // codes that print nothing: the shifts, blank, BELL and WRU.
  std::optional<char> decode(uint8_t code);
  void reset() { figures_ = false; }
  bool inFigures() const { return figures_; }

 private:
  BaudotVariant variant_;
  bool uos_ = true;
  bool figures_ = false;
};

}  // namespace gtty
