#pragma once
#include <cmath>
#include <string>

#include "core/baudot.h"

namespace gtty {

// RTTY signaling parameters shared by modulator, demodulator and FSK keyer.
struct RttyParams {
  double baud = 45.45;         // 45.45, 50, 75, 100 ...
  double shift = 170.0;        // Hz
  double markFreq = 2125.0;    // audio mark tone (Hz)
  // Default audio/AFSK tones: mark 2125 Hz, space 2295 Hz (170 Hz shift, space
  // above mark). Set reverse=true for space below mark (2125/1955).
  bool reverse = false;        // false: space = mark + shift; true: space = mark - shift

  // Set both tones directly; derives shift and polarity.
  void setTones(double mark, double space) {
    markFreq = mark;
    shift = std::fabs(space - mark);
    reverse = space < mark;
  }
  double stopBits = 1.5;       // 1.5 or 2 (TX); RX tolerates either
  BaudotVariant variant = BaudotVariant::ITA2;
  bool unshiftOnSpace = true;

  double spaceFreq() const { return reverse ? markFreq - shift : markFreq + shift; }
  // The same two tones with mark and space exchanged. The default pair (mark below
  // space) is right for a receiver on the lower sideband, the amateur convention; on
  // the upper sideband the audio is mirrored and the signal's mark is the higher tone.
  RttyParams swappedTones() const {
    RttyParams p = *this;
    p.setTones(spaceFreq(), markFreq);
    return p;
  }
  // Reverse the polarity, or put it back: mark and space change places and the pair
  // stays where it is, so a signal tuned in stays tuned in. (Setting `reverse` alone
  // would keep mark and move space to its other side, a shift's width off the signal.)
  void setReversed(bool r) {
    if (r != reverse) *this = swappedTones();
  }
  double centerFreq() const { return (markFreq + spaceFreq()) / 2.0; }
  double bitPeriod() const { return 1.0 / baud; }
  // Start bit + 5 data + stop bits.
  double charPeriod() const { return (1.0 + 5.0 + stopBits) / baud; }
};

// Whether a rig mode (Hamlib name) puts the audio on the upper sideband. RTTYR is left
// out on purpose: the operator chose the rig's reverse, typically for a station that
// sends reversed, and following it would undo that.
inline bool isUpperSidebandMode(const std::string& mode) { return mode == "USB" || mode == "PKTUSB"; }

// The radio frequency of a signal's mark tone, which is where an RTTY station is said to
// be: the frequency a spot or the Reverse Beacon Network gives, and what fldigi puts in
// its log row. `markToneHz` is the audio mark tone in use and `afcHz` how far AFC has
// followed the signal off it. On a sideband the tone is that far from the dial, above it
// or below. In the rig's own RTTY modes (FSK) the dial already reads the mark frequency
// of a signal on the tones, so only the AFC offset counts. The distance from the dial is
// in whole Hz: a logger that takes the difference to the dial and applies it back (TLF,
// to QSY) then lands on the Hz it asked for.
inline double markRadioFrequency(double dialHz, const std::string& rigMode, bool lowerSideband,
                                 double markToneHz, double afcHz) {
  const bool fsk = rigMode == "RTTY" || rigMode == "RTTYR";
  const double audio = std::round((fsk ? 0.0 : markToneHz) + afcHz);
  return lowerSideband ? dialHz - audio : dialHz + audio;
}

}  // namespace gtty
