// Appending received text to a GtkTextBuffer without disturbing a selection.
//
// The buffer's insert and selection-bound marks follow text inserted where they stand,
// so a selection that reaches the end of the text would grow with every character that
// arrives, and a middle-click would paste more than was selected. The mark is put back
// instead. That has to happen without GTK claiming the primary selection again, which it
// does whenever one of those marks is set: on Wayland a claim made when the user is not
// clicking or typing in this window is turned down, and the selection is lost altogether.
#pragma once
#include <gtk/gtk.h>

namespace gtty {

namespace text_append_detail {
inline const char* const kQuiet = "gtty-quiet-mark-set";
inline void onMarkSet(GtkTextBuffer* buf, GtkTextIter*, GtkTextMark*, gpointer) {
  // Runs before GtkTextBuffer's own handler, the one that claims the selection.
  if (g_object_get_data(G_OBJECT(buf), kQuiet)) g_signal_stop_emission_by_name(buf, "mark-set");
}
}  // namespace text_append_detail

// Call once for each buffer that appendKeepingSelection() is used on.
inline void keepSelectionOnAppend(GtkTextBuffer* buf) {
  g_signal_connect(buf, "mark-set", G_CALLBACK(text_append_detail::onMarkSet), nullptr);
}

// Inserts text at the end of the buffer; a selection keeps the text it had.
inline void appendKeepingSelection(GtkTextBuffer* buf, const char* text, int len, GtkTextTag* tag = nullptr) {
  GtkTextIter a, b, end;
  const bool atEnd = gtk_text_buffer_get_selection_bounds(buf, &a, &b) && gtk_text_iter_is_end(&b);
  const int selEnd = atEnd ? gtk_text_iter_get_offset(&b) : 0;
  gtk_text_buffer_get_end_iter(buf, &end);
  if (tag) gtk_text_buffer_insert_with_tags(buf, &end, text, len, tag, nullptr);
  else gtk_text_buffer_insert(buf, &end, text, len);
  if (!atEnd) return;
  // Whichever of the two marks was at the end went along with the new text.
  GtkTextMark* mark = gtk_text_buffer_get_insert(buf);
  GtkTextIter it;
  gtk_text_buffer_get_iter_at_mark(buf, &it, mark);
  if (gtk_text_iter_get_offset(&it) <= selEnd) mark = gtk_text_buffer_get_selection_bound(buf);
  gtk_text_buffer_get_iter_at_offset(buf, &it, selEnd);
  g_object_set_data(G_OBJECT(buf), text_append_detail::kQuiet, GINT_TO_POINTER(1));
  gtk_text_buffer_move_mark(buf, mark, &it);
  g_object_set_data(G_OBJECT(buf), text_append_detail::kQuiet, nullptr);
}

}  // namespace gtty
