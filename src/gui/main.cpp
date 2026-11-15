#include <adwaita.h>

#include <clocale>
#include <cstdio>
#include <cstring>
#include <memory>

#include "core/version.h"
#include "main_window.h"

namespace {
std::unique_ptr<gtty::MainWindow> g_window;

// GTK's startup calls setlocale(LC_ALL, ""), which in e.g. a German locale
// makes printf/strtod use ',' as the decimal separator. That breaks every
// machine-readable number we emit or parse (skimmer "DX de" spots, ADIF,
// XML-RPC doubles, cluster spots) and is inconsistent with the rig display.
// Keep translations/collation from the user's locale, but force C numerics.
void startup(AdwApplication*, gpointer) { std::setlocale(LC_NUMERIC, "C"); }

void activate(AdwApplication* app, gpointer) {
  if (!g_window) g_window = std::make_unique<gtty::MainWindow>(app);
  g_window->present();
}
}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--version")) { std::printf("gtty %s\n", gtty::version()); return 0; }
  }
  g_set_prgname("gtty");
  AdwApplication* app = adw_application_new("io.github.gtty", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "startup", G_CALLBACK(startup), nullptr);
  g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
  int rc = g_application_run(G_APPLICATION(app), argc, argv);
  g_window.reset();
  g_object_unref(app);
  return rc;
}
