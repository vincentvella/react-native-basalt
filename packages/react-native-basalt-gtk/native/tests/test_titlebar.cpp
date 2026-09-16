// The title bar's metrics and what it does to the window.
//
// What the two styles look like is in the screenshots that went with phase 58;
// what is assertable is the arithmetic an app lays itself out against, and
// whether the window really took the style rather than the metrics being
// computed from a flag nothing acted on.

#include "TestHarness.h"

#include "GtkTitleBar.h"

#include <sstream>

namespace {

// A window and the controls the host overlays, built the way main_gtk.cpp
// builds them. Never shown: the metrics are a property of the widgets rather
// than of anything on screen.
struct Fixture {
  GtkWindow *window;
  GtkWidget *controls;

  Fixture() {
    window = GTK_WINDOW(gtk_window_new());
    controls = gtk_window_controls_new(GTK_PACK_END);
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), controls);
    gtk_window_set_child(window, overlay);
    basalt::titleBar().attach(window, controls);
  }

  ~Fixture() { gtk_window_destroy(window); }
};

} // namespace

TEST(titlebar_native_style_costs_the_app_nothing) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);

  // GTK's decorations sit above the child rather than over it, so they take
  // nothing from the app. Windows and macOS report the same zeroes, arrived at
  // differently -- there the system's caption is outside the client area.
  const auto metrics = basalt::titleBar().metrics();
  EXPECT(metrics.style == basalt::TitleBarStyle::Native);
  EXPECT_EQ(metrics.height, 0.0);
  EXPECT_EQ(metrics.buttonsWidth, 0.0);
  EXPECT(gtk_window_get_decorated(fixture.window));
}

TEST(titlebar_hidden_style_reports_what_to_leave_clear) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Hidden);

  const auto metrics = basalt::titleBar().metrics();
  EXPECT(metrics.style == basalt::TitleBarStyle::Hidden);
  // Measured from the controls rather than assumed: how big they are is the
  // theme's business, and a hard-coded size would be wrong on every desktop
  // that disagrees.
  EXPECT(metrics.height > 0.0);
  EXPECT(metrics.buttonsWidth > 0.0);

  // The window really did take the style.
  EXPECT(!gtk_window_get_decorated(fixture.window));
  // And the host's stand-in buttons are up, because GTK's own went with the
  // decorations -- which is where this host differs from the other two.
  EXPECT(gtk_widget_get_visible(fixture.controls));

  // Back again, so an unmounted request restores the window.
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);
  EXPECT(gtk_window_get_decorated(fixture.window));
  EXPECT(!gtk_widget_get_visible(fixture.controls));
  EXPECT_EQ(basalt::titleBar().metrics().height, 0.0);
}

TEST(titlebar_takes_a_title) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);
  basalt::titleBar().setTitle(std::string("Inbox"));

  const char *title = gtk_window_get_title(fixture.window);
  EXPECT_EQ(std::string(title != nullptr ? title : ""), std::string("Inbox"));
}
