#include "core/baudot.h"
#include "check.h"

using namespace gtty;

int main() {
  BaudotEncoder enc;
  BaudotDecoder dec;
  std::string msg = "CQ CQ DE W1AW W1AW 599 001 TEST\nABC";
  std::string out;
  for (uint8_t c : enc.encode(msg)) {
    auto ch = dec.decode(c);
    if (ch) out.push_back(*ch);
  }
  CHECK_EQ(out, std::string("CQ CQ DE W1AW W1AW 599 001 TEST\r\nABC"));

  // First character must be preceded by an explicit shift.
  BaudotEncoder e2;
  auto codes = e2.encode('A');
  CHECK_EQ(codes.size(), size_t(2));
  CHECK_EQ(codes[0], baudot::LTRS);
  CHECK_EQ(codes[1], uint8_t(0x03));
  // No redundant shift for a second letter.
  CHECK_EQ(e2.encode('B').size(), size_t(1));
  // Figures shift inserted once.
  auto f = e2.encode("12");
  CHECK_EQ(f.size(), size_t(3));
  CHECK_EQ(f[0], baudot::FIGS);

  // Unshift on space.
  BaudotDecoder d2;
  d2.setUnshiftOnSpace(true);
  d2.decode(baudot::FIGS);
  CHECK_EQ(*d2.decode(0x17), '1');
  d2.decode(baudot::SPACE);
  CHECK_EQ(*d2.decode(0x17), 'Q');
  BaudotDecoder d3;
  d3.setUnshiftOnSpace(false);
  d3.decode(baudot::FIGS);
  d3.decode(baudot::SPACE);
  CHECK_EQ(*d3.decode(0x17), '1');

  // US-TTY vs ITA2 figures.
  BaudotDecoder us(BaudotVariant::USTTY), ita(BaudotVariant::ITA2);
  us.decode(baudot::FIGS); ita.decode(baudot::FIGS);
  CHECK_EQ(*us.decode(0x09), '$');
  CHECK_EQ(*ita.decode(0x11), '+');
  CHECK_EQ(*us.decode(0x11), '"');

  // Whatever arrives, noise included, only printable characters and line ends come out:
  // every code, in both shifts, in both variants.
  for (BaudotVariant v : {BaudotVariant::ITA2, BaudotVariant::USTTY}) {
    for (uint8_t shift : {baudot::LTRS, baudot::FIGS}) {
      for (int code = 0; code < 32; ++code) {
        BaudotDecoder d(v);
        d.decode(shift);
        auto ch = d.decode(static_cast<uint8_t>(code));
        if (!ch) continue;
        CHECK((*ch >= 0x20 && *ch < 0x7f) || *ch == '\n' || *ch == '\r');
      }
    }
  }
  // BELL (FIGS J in ITA2, FIGS S in US-TTY) and WRU (FIGS D in ITA2) print nothing
  // and leave the shift as it was.
  BaudotDecoder bell(BaudotVariant::ITA2);
  bell.decode(baudot::FIGS);
  CHECK(!bell.decode(0x0b));
  CHECK(!bell.decode(0x09));
  CHECK_EQ(*bell.decode(0x17), '1');
  BaudotDecoder bellUs(BaudotVariant::USTTY);
  bellUs.decode(baudot::FIGS);
  CHECK(!bellUs.decode(0x05));
  CHECK_EQ(*bellUs.decode(0x0b), '\'');

  // The other shift's reading of a word (issue #26: "126" received as "QWY").
  {
    using baudot::otherShiftReading;
    const auto ita = BaudotVariant::ITA2;
    bool figs = false;
    CHECK_EQ(otherShiftReading("QWY", ita, &figs), std::string("126"));
    CHECK(figs);
    CHECK_EQ(otherShiftReading("WPO", ita), std::string("209"));
    CHECK_EQ(otherShiftReading("qwertyuiop", ita), std::string("1234567890"));
    CHECK_EQ(otherShiftReading("TOO", ita), std::string("599"));
    // Letters outside the top row: an ordinary word, no hint.
    CHECK_EQ(otherShiftReading("TEST", ita), std::string());
    CHECK_EQ(otherShiftReading("DF7CB", ita), std::string());
    CHECK_EQ(otherShiftReading("599", ita), std::string());
    CHECK_EQ(otherShiftReading("Q", ita), std::string());
    // Letters printed as figures: punctuation back to letters, digits kept.
    CHECK_EQ(otherShiftReading("DF7:?", ita, &figs), std::string("DF7CB"));
    CHECK(!figs);
    CHECK_EQ(otherShiftReading(":1", ita), std::string("C1"));
    CHECK_EQ(otherShiftReading("-.", ita), std::string("AM"));
    // The two alphabets differ: ITA2 has no '$', US-TTY prints D's figure as '$'.
    CHECK_EQ(otherShiftReading("$!7:?", ita), std::string());
    CHECK_EQ(otherShiftReading("$!7:?", BaudotVariant::USTTY), std::string("DF7CB"));
    // Timestamps and other bracketed words are not hints.
    CHECK_EQ(otherShiftReading("[07:22:39]", ita), std::string());
  }
  TEST_MAIN_END();
}
