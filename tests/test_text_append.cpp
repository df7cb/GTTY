// Text arriving in a receive pane must leave a selection as it was: a selection that
// reaches the end of the text must not grow, and the primary selection must not be
// claimed again (on Wayland a claim made without user input loses it).
// Needs a display; skipped (exit 77) without one.
#include <gtk/gtk.h>

#include <string>

#include "text_append.h"
#include "check.h"

using namespace gtty;

namespace {
int g_claims = 0;
void onPrimaryChanged(GdkClipboard*, gpointer) { ++g_claims; }
void settle() { for (int i = 0; i < 50; ++i) { while (g_main_context_iteration(nullptr, FALSE)) {} g_usleep(2000); } }

std::string selected(GtkTextBuffer* buf) {
  GtkTextIter a, b;
  if (!gtk_text_buffer_get_selection_bounds(buf, &a, &b)) return "";
  gchar* t = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
  std::string s = t;
  g_free(t);
  return s;
}
}  // namespace

int main() {
  if (!gtk_init_check()) { std::printf("no display, skipped\n"); return 77; }
  GtkWidget* win = gtk_window_new();
  GtkWidget* view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
  gtk_window_set_child(GTK_WINDOW(win), view);
  gtk_window_present(GTK_WINDOW(win));
  GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  GtkTextTag* tag = gtk_text_buffer_create_tag(buf, nullptr, "foreground", "#d03030", nullptr);
  keepSelectionOnAppend(buf);
  g_signal_connect(gtk_widget_get_primary_clipboard(view), "changed", G_CALLBACK(onPrimaryChanged), nullptr);

  // forward: dragged left to right, the insert mark at the end; otherwise the other way.
  for (bool forward : {true, false}) {
    gtk_text_buffer_set_text(buf, "CQ TEST DE DF7CB DF7CB", -1);
    settle();   // giving up the previous selection is over before the next one is made
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_offset(buf, &a, 11);
    gtk_text_buffer_get_end_iter(buf, &b);
    if (forward) gtk_text_buffer_select_range(buf, &b, &a); else gtk_text_buffer_select_range(buf, &a, &b);
    settle();
    CHECK_EQ(selected(buf), std::string("DF7CB DF7CB"));
    const int claims = g_claims;
    for (int i = 0; i < 10; ++i) appendKeepingSelection(buf, "X", 1);
    appendKeepingSelection(buf, " sent\n", -1, tag);
    settle();
    CHECK_EQ(selected(buf), std::string("DF7CB DF7CB"));   // did not grow
    CHECK_EQ(g_claims, claims);                             // and was not claimed again
    CHECK_EQ(gtk_text_buffer_get_char_count(buf), 22 + 10 + 6);
    GtkTextIter ins;
    gtk_text_buffer_get_iter_at_mark(buf, &ins, gtk_text_buffer_get_insert(buf));
    CHECK_EQ(gtk_text_iter_get_offset(&ins), forward ? 22 : 11);   // the same way round as before
  }

  // A selection in the middle of the text, and no selection, are left alone.
  gtk_text_buffer_set_text(buf, "CQ TEST DE DF7CB DF7CB", -1);
  settle();
  GtkTextIter a, b;
  gtk_text_buffer_get_iter_at_offset(buf, &a, 11);
  gtk_text_buffer_get_iter_at_offset(buf, &b, 16);
  gtk_text_buffer_select_range(buf, &b, &a);
  settle();
  CHECK_EQ(selected(buf), std::string("DF7CB"));
  int claims = g_claims;
  appendKeepingSelection(buf, " K", -1);
  settle();
  CHECK_EQ(selected(buf), std::string("DF7CB"));
  CHECK_EQ(g_claims, claims);
  gtk_text_buffer_get_end_iter(buf, &b);
  gtk_text_buffer_place_cursor(buf, &b);
  appendKeepingSelection(buf, " K", -1);
  CHECK_EQ(selected(buf), std::string(""));
  // Selecting by hand afterwards still claims the selection as usual.
  settle();
  claims = g_claims;
  gtk_text_buffer_get_start_iter(buf, &a);
  gtk_text_buffer_get_end_iter(buf, &b);
  gtk_text_buffer_select_range(buf, &b, &a);
  settle();
  CHECK(g_claims > claims);
  TEST_MAIN_END();
}
