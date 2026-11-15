#include <adwaita.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "core/version.h"
#include "main_window.h"

namespace {
std::unique_ptr<gtty::MainWindow> g_window;

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
  g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
  int rc = g_application_run(G_APPLICATION(app), argc, argv);
  g_window.reset();
  g_object_unref(app);
  return rc;
}
