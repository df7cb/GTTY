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

  // Tuning to a signal found in the tones in use (Skimmer row, waterfall label): the mark
  // in use must land on it, not a shift away, with or without the swap and with Rev.
  for (bool reversed : {false, true}) {
    RttyParams stored;
    stored.reverse = reversed;
    for (bool swapped : {false, true}) {
      RttyParams t = stored.withMarkInUse(1500, swapped);
      RttyParams inUse = swapped ? t.swappedTones() : t;
      CHECK_EQ(inUse.markFreq, 1500.0);
      CHECK_EQ(inUse.shift, stored.shift);
      CHECK_EQ(inUse.reverse, (swapped ? stored.swappedTones() : stored).reverse);
      CHECK_EQ(t.reverse, stored.reverse);   // the stored polarity is kept
    }
  }
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
    // Issue #46: the Skimmer keeps a station's radio frequency and works out where it is
    // heard now, after a QSY, or where to put the dial to hear it on the tones in use.
    struct Case { double dial; const char* mode; bool lsb; double tone; };
    for (const Case& k : {Case{7039200.0, "USB", false, onUsb.markFreq}, Case{7039200.0, "PKTUSB", false, onUsb.markFreq},
                          Case{7043626.0, "LSB", true, onLsb.markFreq}, Case{7043626.0, "", true, onLsb.markFreq},
                          Case{7041501.0, "RTTY", true, onLsb.markFreq}, Case{7041501.0, "RTTYR", false, onLsb.markFreq}}) {
      for (double audio : {k.tone, k.tone + 37.0, 845.0, 2710.0}) {
        double rf = markRadioFrequency(k.dial, k.mode, k.lsb, k.tone, audio - k.tone);
        CHECK(std::fabs(audioMarkForRadioFrequency(rf, k.dial, k.mode, k.lsb, k.tone) - audio) < 0.51);
      }
      double dial = dialForMarkRadioFrequency(7041501.0, k.mode, k.lsb, k.tone);
      CHECK(std::fabs(audioMarkForRadioFrequency(7041501.0, dial, k.mode, k.lsb, k.tone) - k.tone) < 1e-6);
    }
    // 10 kHz down on USB (the report): the station is heard 10 kHz higher, off the audio.
    CHECK_EQ(audioMarkForRadioFrequency(7041501.0, 7029200.0, "USB", false, onUsb.markFreq), 12301.0);
    CHECK_EQ(dialForMarkRadioFrequency(7041501.0, "USB", false, onUsb.markFreq), 7041501.0 - onUsb.markFreq);
    CHECK_EQ(dialForMarkRadioFrequency(7041501.0, "LSB", true, onLsb.markFreq), 7041501.0 + onLsb.markFreq);
    CHECK_EQ(dialForMarkRadioFrequency(7041501.0, "RTTY", true, onLsb.markFreq), 7041501.0);

    // AFC is rarely on a whole Hz; the distance from the dial is.
    CHECK_EQ(markRadioFrequency(7039200.0, "USB", false, onUsb.markFreq, 6.4), 7041501.0);
    CHECK_EQ(markRadioFrequency(7041501.0, "RTTY", true, onLsb.markFreq, -0.3), 7041501.0);
    // A skimmer channel is its audio mark's distance from the tones in use (issue #31,
    // the skimmer 2 kHz off in RTTY mode). Dial 14090.980 in RTTY, a station 125 Hz above
    // the configured mark in the audio is 125 Hz below the dial; on the tones, it is the dial.
    CHECK_EQ(std::lround(markRadioFrequency(14090980.0, "RTTY", true, onLsb.markFreq, 2125.0 - onLsb.markFreq)), 14090980L);
    CHECK_EQ(std::lround(markRadioFrequency(14090980.0, "RTTY", true, onLsb.markFreq, 2250.0 - onLsb.markFreq)), 14090855L);
    CHECK_EQ(std::lround(markRadioFrequency(14090980.0, "RTTYR", false, onLsb.markFreq, 2250.0 - onLsb.markFreq)), 14091105L);
    // On a sideband it is the same as dial -/+ the channel's audio mark.
    CHECK_EQ(std::lround(markRadioFrequency(14093000.0, "LSB", true, onLsb.markFreq, 1800.0 - onLsb.markFreq)), 14091200L);
    CHECK_EQ(std::lround(markRadioFrequency(14089000.0, "USB", false, onUsb.markFreq, 1800.0 - onUsb.markFreq)), 14090800L);
  }
  TEST_MAIN_END();
}
