#include "check.h"
#include "core/line_breaks.h"

using namespace gtty;

static std::string run(const std::string& in) {
  LineBreaks lb;
  std::string out;
  for (char c : in)
    if (char o = lb.feed(c)) out.push_back(o);
  return out;
}

int main() {
  // Every way a station ends a line gives exactly one break.
  CHECK_EQ(run("QTC 1/10\r\nNEXT"), std::string("QTC 1/10\nNEXT"));
  CHECK_EQ(run("QTC 1/10\rNEXT"), std::string("QTC 1/10\nNEXT"));       // CR only: used to print as one long line
  CHECK_EQ(run("QTC 1/10\nNEXT"), std::string("QTC 1/10\nNEXT"));
  CHECK_EQ(run("QTC 1/10\r\r\nNEXT"), std::string("QTC 1/10\nNEXT"));   // the teleprinter habit: CR CR LF
  CHECK_EQ(run("QTC 1/10\n\rNEXT"), std::string("QTC 1/10\nNEXT"));
  CHECK_EQ(run("A\r\r\rB"), std::string("A\nB"));

  // A blank line still takes a second LF.
  CHECK_EQ(run("A\n\nB"), std::string("A\n\nB"));
  CHECK_EQ(run("A\r\n\r\nB"), std::string("A\n\nB"));
  CHECK_EQ(run("A\r\n\nB"), std::string("A\n\nB"));

  // Each line of a CR-only transmission breaks, and the state carries across feeds.
  CHECK_EQ(run("1234 DL1ABC 001\r1235 DL2DEF 002\r1236 DL3GHI 003\r"),
           std::string("1234 DL1ABC 001\n1235 DL2DEF 002\n1236 DL3GHI 003\n"));
  LineBreaks lb;
  CHECK_EQ(lb.feed('\r'), '\n');
  CHECK_EQ(lb.feed('\n'), '\0');
  CHECK_EQ(lb.feed('A'), 'A');
  lb.feed('\r');
  lb.reset();
  CHECK_EQ(lb.feed('\n'), '\n');

  TEST_MAIN_END();
}
