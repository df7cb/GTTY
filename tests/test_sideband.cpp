// Following the rig's sideband: on the upper sideband the audio is mirrored, so the
// same tone pair is used with mark and space exchanged.
#include <cmath>

#include "check.h"
#include "rtty_sim.h"

using namespace gtty;

int main() {
  // Which rig modes count. The rig's own reverse (RTTYR) is the operator's choice and is not followed.
  CHECK(isUpperSidebandMode("USB"));
  CHECK(isUpperSidebandMode("PKTUSB"));
  for (const char* m : {"LSB", "PKTLSB", "RTTY", "RTTYR", "CW", "AM", "FM", ""}) CHECK(!isUpperSidebandMode(m));

  // The swap keeps the tone pair and the shift and exchanges the roles.
  RttyParams lsb;                       // 2125 mark, 2295 space
  RttyParams usb = lsb.swappedTones();
  CHECK_EQ(usb.markFreq, 2295.0);
  CHECK_EQ(usb.spaceFreq(), 2125.0);
  CHECK_EQ(usb.shift, 170.0);
  CHECK(usb.reverse);
  CHECK_EQ(usb.centerFreq(), lsb.centerFreq());
  // With Rev ticked the swap gives the plain pair again, and twice is no change.
  RttyParams rev = lsb;
  rev.reverse = true;                   // 2125 mark, 1955 space
  CHECK_EQ(rev.swappedTones().markFreq, 1955.0);
  CHECK_EQ(rev.swappedTones().spaceFreq(), 2125.0);
  CHECK(!rev.swappedTones().reverse);
  CHECK_EQ(lsb.swappedTones().swappedTones().markFreq, lsb.markFreq);
  CHECK(!lsb.swappedTones().swappedTones().reverse);
  // The Rev box: mark and space change places, the pair stays put (issue #21: it used to
  // keep mark and move space to the other side, 170 Hz off the signal).
  RttyParams box = lsb;                 // 2125 mark, 2295 space
  box.setReversed(true);
  CHECK(box.reverse);
  CHECK_EQ(box.markFreq, 2295.0);
  CHECK_EQ(box.spaceFreq(), 2125.0);
  CHECK_EQ(box.centerFreq(), lsb.centerFreq());
  box.setReversed(true);                // already reversed: nothing moves
  CHECK_EQ(box.markFreq, 2295.0);
  box.setReversed(false);
  CHECK(!box.reverse);
  CHECK_EQ(box.markFreq, 2125.0);
  CHECK_EQ(box.spaceFreq(), 2295.0);
  // Baud, stop bits and the rest are untouched.
  RttyParams wide;
  wide.baud = 50;
  wide.setTones(2125, 2575);
  CHECK_EQ(wide.swappedTones().baud, 50.0);
  CHECK_EQ(wide.swappedTones().shift, 450.0);

  // A station as it sounds on the upper sideband (its mark is the higher tone) prints
  // with the swapped tones and not with the configured ones; the lower sideband the other way round.
  const double fs = 48000;
  const std::string sent = "CQ CQ DE DL1ABC DL1ABC K";
  auto onUsb = modulate(sent, usb, fs);
  addNoise(onUsb, 15, fs, 3);
  CHECK(demodulate(onUsb, fs, usb, true).find(sent) != std::string::npos);
  CHECK(demodulate(onUsb, fs, lsb, true).find("DL1ABC") == std::string::npos);
  auto onLsb = modulate(sent, lsb, fs);
  addNoise(onLsb, 15, fs, 4);
  CHECK(demodulate(onLsb, fs, lsb, true).find(sent) != std::string::npos);
  CHECK(demodulate(onLsb, fs, usb, true).find("DL1ABC") == std::string::npos);

  // ----------------------------------------------- where a station is: its mark frequency
  // Issue #31: dial 7039.2 kHz on USB, the 2125/2295 pair, AFC at +6 Hz. On the upper
  // sideband the mark is the higher tone, and the Reverse Beacon Network has the station
  // on 7041.5 kHz.
  {
    RttyParams onUsb = RttyParams{}.swappedTones();
    CHECK_EQ(std::lround(markRadioFrequency(7039200.0, "USB", false, onUsb.markFreq, 6.0)), 7041501L);
    CHECK_EQ(std::lround(markRadioFrequency(7039200.0, "PKTUSB", false, onUsb.markFreq, 6.0)), 7041501L);
    // The same station on the lower sideband: the dial is above it, the mark is the lower tone.
    RttyParams onLsb;
    CHECK_EQ(std::lround(markRadioFrequency(7043626.0, "LSB", true, onLsb.markFreq, 0.0)), 7041501L);
    CHECK_EQ(std::lround(markRadioFrequency(7043626.0, "LSB", true, onLsb.markFreq, 6.0)), 7041495L);   // audio up is RF down
    // No rig: the dial typed in is taken as a lower sideband one.
    CHECK_EQ(std::lround(markRadioFrequency(7043626.0, "", true, onLsb.markFreq, 0.0)), 7041501L);
    // The rig's RTTY modes read the mark frequency on the dial; AFC is all that is added.
    CHECK_EQ(std::lround(markRadioFrequency(7041501.0, "RTTY", true, onLsb.markFreq, 0.0)), 7041501L);
    CHECK_EQ(std::lround(markRadioFrequency(7041501.0, "RTTY", true, onLsb.markFreq, 6.0)), 7041495L);
    CHECK_EQ(std::lround(markRadioFrequency(7041501.0, "RTTYR", false, onLsb.markFreq, 6.0)), 7041507L);
    // AFC is rarely on a whole Hz; the distance from the dial is.
    CHECK_EQ(markRadioFrequency(7039200.0, "USB", false, onUsb.markFreq, 6.4), 7041501.0);
    CHECK_EQ(markRadioFrequency(7041501.0, "RTTY", true, onLsb.markFreq, -0.3), 7041501.0);
  }
  TEST_MAIN_END();
}
