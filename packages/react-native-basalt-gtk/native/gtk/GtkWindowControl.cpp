// The Linux half of core/WindowControl.h.
//
// GTK4 is the most opinionated of the three desktops about this, and two of the
// four functions are shaped by what it will not do.
//
// **A window cannot be positioned.** `gtk_window_move` is gone in GTK4 and
// nothing replaced it: on Wayland a client has no idea where its window is and
// no way to say, and GTK removed the X11 call rather than ship an API that
// works on one display server. So `setWindowPosition` and the `x`/`y` in
// `windowBounds` are honest zeroes, and `centerWindow` is the window manager's
// job -- which, on both display servers, it does by default for a window that
// has not been placed.
//
// **A window's size is a request.** `gtk_window_set_default_size` is what a
// client has; the compositor may ignore it. That is also true of the other two
// desktops in a tiling window manager, and the answer is the same: ask, and
// report what actually happened through the bounds listener.

#include "WindowControl.h"

#include "GtkTitleBar.h"

#include <gtk/gtk.h>

namespace basalt {

namespace {


// The window this process owns. The same question PlatformServicesGtk.cpp asks
// for a popup's parent, and asked the same way: GTK has a list of toplevels
// rather than an idea of "the app's window".
GtkWindow *window() {
  GListModel *toplevels = gtk_window_get_toplevels();
  if (toplevels == nullptr) {
    return nullptr;
  }
  GtkWindow *fallback = nullptr;
  const guint count = g_list_model_get_n_items(toplevels);
  for (guint i = 0; i < count; i++) {
    auto *candidate = static_cast<GtkWindow *>(g_list_model_get_item(toplevels, i));
    if (candidate == nullptr) {
      continue;
    }
    const bool visible = gtk_widget_get_visible(GTK_WIDGET(candidate)) != FALSE;
    if (visible && gtk_window_is_active(candidate)) {
      g_object_unref(candidate);
      return candidate;
    }
    if (visible && fallback == nullptr) {
      fallback = candidate;
    }
    g_object_unref(candidate);
  }
  return fallback;
}

} // namespace

WindowBounds windowBounds() {
  WindowBounds bounds;
  GtkWindow *target = window();
  if (target == nullptr) {
    return bounds;
  }
  bounds.width = gtk_widget_get_width(GTK_WIDGET(target));
  bounds.height = gtk_widget_get_height(GTK_WIDGET(target));
  bounds.fullScreen = gtk_window_is_fullscreen(target) != FALSE;
  bounds.maximized = gtk_window_is_maximized(target) != FALSE;
  // x and y stay zero. See the header of this file: GTK4 has no way to ask.
  return bounds;
}

void setWindowSize(double width, double height) {
  GtkWindow *target = window();
  if (target == nullptr || width <= 0.0 || height <= 0.0) {
    return;
  }
  // `default_size` rather than a resize call, because that is the only one GTK4
  // has: it is the size the window takes when it is not being managed into
  // something else, and setting it on a mapped window resizes it.
  gtk_window_set_default_size(target, static_cast<int>(width), static_cast<int>(height));
}

void setWindowPosition(double /*x*/, double /*y*/) {
  // Deliberately nothing. See the header: GTK4 removed `gtk_window_move` and
  // Wayland has no equivalent, so an app asking for a position is asking for
  // something no Linux desktop will do. Failing silently is the same answer the
  // compositor gives and is better than a warning an app cannot act on.
}

void centerWindow() {
  // Also nothing, and for the same reason -- centring is positioning. Both
  // display servers centre an unplaced window by default, so an app that never
  // moves its window already gets what this would have asked for.
}

void setWindowFullScreen(bool fullScreen) {
  GtkWindow *target = window();
  if (target == nullptr) {
    return;
  }
  if (fullScreen) {
    gtk_window_fullscreen(target);
  } else {
    gtk_window_unfullscreen(target);
  }
}


} // namespace basalt
