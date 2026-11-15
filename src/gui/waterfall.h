// Scrolling waterfall + spectrum with mark/space cursors. GtkDrawingArea based.
#pragma once
#include <gtk/gtk.h>

#include "core/dsp/noise_floor.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gtty {

class Waterfall {
 public:
  Waterfall();
  ~Waterfall();
  GtkWidget* widget() const { return area_; }

  void setRange(double loHz, double hiHz);
  void setTones(double markHz, double spaceHz);  // cursor positions (AFC-tracked)
  void setNominalTones(double markHz, double spaceHz);
  // Draw high audio frequencies on the left, so the display matches a rig's
  // RF scope in a lower-sideband mode (RTTY-L, LSB).
  void setReversed(bool reversed);
  bool reversed() const { return reversed_; }
  void addSpectrum(const std::vector<float>& db, double binHz);
  // Transmitting: keep scrolling, but hold the level instead of learning the muted
  // receiver (or our own signal) as the band (issue #43).
  void setTransmitting(bool on) { transmitting_ = on; }

  // Left click or drag: desired center of the tone pair (Hz). Right click: same, for rig retune.
  // Scroll wheel: nudge by deltaHz.
  std::function<void(double hz)> onClickTune;
  std::function<void(double hz)> onRightClickTune;
  std::function<void(double deltaHz)> onNudge;
  std::function<void(double hz)> onMiddleClick;   // toggle a notch
  void setNotches(const std::vector<double>& hz) { notches_ = hz; gtk_widget_queue_draw(area_); }

  // Callsign labels, as WSJT-X Improved and FT8CN draw them: placed at the top of the
  // waterfall at a station's mark tone when it is identified, they scroll down with
  // its trace. Clicking one calls onLabelClick instead of tuning to the click.
  enum class LabelKind { Normal, Cq, ToMe, Dupe };
  void addCallLabel(double markHz, const std::string& call, LabelKind kind);
  void clearCallLabels() { labels_.clear(); gtk_widget_queue_draw(area_); }
  std::function<void(double markHz, const std::string& call)> onLabelClick;

 private:
  static void draw(GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer self);
  static void pressed(GtkGestureClick* g, int n, double x, double y, gpointer self);
  static void middlePressed(GtkGestureClick* g, int n, double x, double y, gpointer self);
  static void dragBegin(GtkGestureDrag* g, double x, double y, gpointer self);
  static void dragUpdate(GtkGestureDrag* g, double dx, double dy, gpointer self);
  static void dragEnd(GtkGestureDrag* g, double dx, double dy, gpointer self);
  static gboolean scrolled(GtkEventControllerScroll* c, double dx, double dy, gpointer self);
  static void motion(GtkEventControllerMotion*, double x, double y, gpointer self);
  static void resized(GtkDrawingArea*, int w, int h, gpointer self);
  void ensureSurface(int w, int h);
  double xToHz(double x, int w) const {
    double t = x / w;
    return lo_ + (reversed_ ? 1.0 - t : t) * (hi_ - lo_);
  }
  double hzToX(double hz, int w) const {
    double t = (hz - lo_) / (hi_ - lo_);
    return (reversed_ ? 1.0 - t : t) * w;
  }
  void blankHistory();
  struct CallLabel {
    double hz;
    std::string text;
    LabelKind kind;
    int64_t row;    // rows_ when the label was at the top of the waterfall
    double width;   // text width in px, for hit testing
  };
  // Box of a label in widget coordinates: x, y (top), width, height.
  void labelBox(const CallLabel& l, int w, double& x, double& y, double& bw, double& bh) const;
  const CallLabel* labelAt(double x, double y, int w) const;
  static void colormap(float v, unsigned char* bgra);

  GtkWidget* area_;
  cairo_surface_t* surf_ = nullptr;
  int sw_ = 0, sh_ = 0;
  double lo_ = 300, hi_ = 3000;
  bool reversed_ = false;
  double mark_ = 2125, space_ = 2295;
  double nomMark_ = 2125, nomSpace_ = 2295;
  std::vector<float> lastDb_;
  double binHz_ = 1;
  float floorDb_ = -80, rangeDb_ = 60;  // auto-tracked
  NoiseFloorTracker noise_;
  TransmitHold txHold_;
  bool transmitting_ = false;
  gint64 lastSpectrumUs_ = 0;
  double hoverHz_ = -1;
  std::vector<double> notches_;
  std::vector<CallLabel> labels_;
  int64_t rows_ = 0;   // rows scrolled since start
  // drag state
  double dragStartX_ = 0, dragStartY_ = 0;
  double dragStartCenter_ = 0;
  bool dragInsideWindow_ = false;
  bool dragging_ = false;
  bool dragMoved_ = false;
  static constexpr int kSpectrumHeight = 70;
  static constexpr double kLabelFont = 11, kLabelHeight = 15, kLabelPad = 3;
};

}  // namespace gtty
