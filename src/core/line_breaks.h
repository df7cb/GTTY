// Line endings as RTTY stations send them. The convention is CR LF (often CR CR LF),
// but some stations and contest programs send a bare CR, a bare LF, or LF CR. Printing
// only on LF runs the lines of a CR-only station together; printing on both gives blank
// lines for everyone else. This filter turns each of those endings into one '\n'.
#pragma once

namespace gtty {

class LineBreaks {
 public:
  // Returns what to print for c: '\n' for a line break, 0 for a CR or LF that belongs
  // to a break already given, c itself for anything else.
  char feed(char c) {
    if (c == '\r') {
      if (state_ != State::Text) return 0;
      state_ = State::AfterCr;
      return '\n';
    }
    if (c == '\n') {
      bool partOfCrLf = state_ == State::AfterCr;
      state_ = State::AfterLf;
      return partOfCrLf ? 0 : '\n';
    }
    state_ = State::Text;
    return c;
  }
  void reset() { state_ = State::Text; }

 private:
  // AfterCr: a CR (or several) gave the break, an LF that follows completes it.
  // AfterLf: an LF has been seen, a further LF is a blank line, a CR is not.
  enum class State { Text, AfterCr, AfterLf };
  State state_ = State::Text;
};

}  // namespace gtty
