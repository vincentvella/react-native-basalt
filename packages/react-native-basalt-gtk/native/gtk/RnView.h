// react-native-basalt — GTK4 view layer
//
// Deliberately free of React Native headers: this file only knows about
// absolute frames and paint properties. The mounting manager translates
// Fabric's ShadowViewMutation stream into calls on this API, so the widget
// layer can be built and exercised without the RN C++ core present.

#pragma once

#include <gtk/gtk.h>
#include <pango/pango.h>

G_BEGIN_DECLS

// ---------------------------------------------------------------------------
// RnLayout — a GtkLayoutManager that does no layout of its own.
//
// Yoga has already computed absolute frames by the time a mutation reaches us,
// so this places each child at the rect the shadow tree assigned it. Measuring
// returns zero: RN is the sole source of truth for size.
// ---------------------------------------------------------------------------

#define RN_TYPE_LAYOUT (rn_layout_get_type())
G_DECLARE_FINAL_TYPE(RnLayout, rn_layout, RN, LAYOUT, GtkLayoutManager)

// ---------------------------------------------------------------------------
// RnView — the GTK peer of a Fabric <View>.
// ---------------------------------------------------------------------------

#define RN_TYPE_VIEW (rn_view_get_type())
G_DECLARE_FINAL_TYPE(RnView, rn_view, RN, VIEW, GtkWidget)

RnView *rn_view_new(int tag);

// As above, but with an accessible role.
//
// GTK4 has no per-instance role setter: a role is a construct-only property, or
// is set once per widget class. RnView is one class for every React Native
// view, so the role has to be chosen when the widget is made. That is possible
// because Fabric delivers a view's props with the Create mutation that makes
// it -- but it does mean accessibilityRole cannot change afterwards.
RnView *rn_view_new_with_role(int tag, GtkAccessibleRole role);

// Fabric tag, for debugging and hit-test attribution.
int rn_view_get_tag(RnView *self);

// Frame in parent coordinates, straight from LayoutMetrics::frame.
void rn_view_set_frame(RnView *self, float x, float y, float width, float height);
void rn_view_get_frame(RnView *self, graphene_rect_t *out);

// BaseViewProps::backgroundColor. Passing has_color = FALSE paints nothing.
void rn_view_set_background_color(RnView *self, gboolean has_color, const GdkRGBA *color);

// BaseViewProps::opacity.
void rn_view_set_opacity(RnView *self, double opacity);

// Corner radii, in the order top-left, top-right, bottom-right, bottom-left.
// Each is a width/height pair, because React Native's radii are elliptical.
// Pass NULL for square corners.
void rn_view_set_border_radii(RnView *self, const graphene_size_t radii[4]);

// Border widths and colours, in the order top, right, bottom, left -- which is
// the order GTK's border node wants and the order CSS names them in.
void rn_view_set_borders(RnView *self, const float widths[4], const GdkRGBA colors[4]);

// BaseViewProps::transform, already resolved by React Native.
//
// Applied in the layout manager rather than at paint time, so that GTK's own
// hit testing follows it: a transformed view is picked where it appears, not
// where its frame says it is. Anchored on the view's centre, which is what
// every other React Native platform does and what transformOrigin is measured
// against. Pass NULL for identity.
void rn_view_set_transform(RnView *self, const graphene_matrix_t *matrix);

// BaseViewProps::zIndex. GTK paints in child order, so this reorders painting
// without touching the child list that mutations index into.
void rn_view_set_z_index(RnView *self, int z_index);

// The text of a <Paragraph>, already laid out.
//
// A Paragraph is a View that also paints text, so it gets no separate widget
// type: same box model, same frame, same children. The mounting manager builds
// the layout from the shadow view's AttributedString -- Pango is part of the
// GTK stack, so taking a PangoLayout here keeps this file free of React Native
// types the way the rest of it is.
//
// Takes its own reference. Pass NULL to clear. `color` is the colour to paint
// glyphs that carry no foreground attribute of their own.
void rn_view_set_text_layout(RnView *self, PangoLayout *layout, const GdkRGBA *color);

// How an image fills its frame. Mirrors React Native's ImageResizeMode, minus
// Repeat, which needs a repeating pattern node rather than one texture draw.
typedef enum {
  RN_IMAGE_FIT_COVER,
  RN_IMAGE_FIT_CONTAIN,
  RN_IMAGE_FIT_STRETCH,
  RN_IMAGE_FIT_CENTER,
} RnImageFit;

// The decoded pixels of an <Image>. Takes its own reference; pass NULL to clear.
//
// Like the text layout above, this is a GTK type rather than a React Native
// one, so the widget layer stays free of RN headers. GtkImageLoader produces
// the texture and GtkMountingManager chooses the fit.
void rn_view_set_texture(RnView *self, GdkTexture *texture, RnImageFit fit);

// Called on the GTK main thread when this widget's own allocation changes.
//
// The host attaches one to a surface root to drive
// ReactHost::setSurfaceConstraints. GTK4 removed GtkWidget::size-allocate, and
// a layout manager's allocate is the supported replacement: it is the one
// place a widget is told the size it actually got.
typedef void (*RnViewResizeFunc)(RnView *self, int width, int height, gpointer user_data);
void rn_view_set_resize_callback(RnView *self, RnViewResizeFunc callback, gpointer user_data);

// Clip children to this view's bounds.
//
// Off by default, because React Native's default is overflow: visible. A
// <ScrollView> needs it, and so does any view with overflow: 'hidden'.
void rn_view_set_clips_children(RnView *self, gboolean clips);

// Shift children by a scroll offset, in points.
//
// Applied in the layout manager rather than by moving frames, so the frames
// Fabric assigned stay untouched and GTK's own hit testing follows the shift
// for free: a child allocated at its scrolled position is picked there.
void rn_view_set_scroll_offset(RnView *self, double offset_x, double offset_y);
void rn_view_get_scroll_offset(RnView *self, double *offset_x, double *offset_y);

// A textual description of the widget tree rooted here, one indented line per
// view.
//
// This is what makes the full stack testable rather than merely watchable.
// Everything below GDK has unit tests, but the path from JavaScript through
// React, Fabric and the mounting manager can only be exercised by running the
// real host -- and until now the only way to check the result was to look at a
// screenshot. A dump can be asserted on.
//
// Returns a newly allocated string; free with g_free.
char *rn_view_describe_tree(RnView *self);

// Turns this view into a text field, or back.
//
// The editable is a real GtkText -- the widget behind GtkEntry -- parented
// inside this one and sized to its whole frame. Using GTK's own editable rather
// than drawing a cursor on a PangoLayout is what brings input methods,
// selection, the clipboard and every keybinding a Linux user expects, none of
// which is worth reimplementing.
//
// Returns the editable, or NULL after clearing.
GtkText *rn_view_set_editable(RnView *self, gboolean editable);

// The border and padding to hold the native peer inside. Yoga has already
// resolved these into React Native's content inset; without them a <TextInput>
// renders its text flush against its own border, ignoring paddingHorizontal.
void rn_view_set_peer_insets(RnView *self, const GtkBorder *insets);
void rn_view_get_peer_insets(RnView *self, GtkBorder *out);
GtkText *rn_view_get_editable(RnView *self);

// Accessibility, as a screen reader sees it.
//
// `label` is the accessible name and `description` the hint; either may be NULL
// or empty to leave it unset. GTK maps these onto AT-SPI, which is what Orca
// reads.
void rn_view_set_accessible_text(RnView *self, const char *label, const char *description);

// Accessible states. Each is a tri-state: unset leaves GTK's default alone,
// which is not the same as setting it false.
typedef enum {
  RN_A11Y_UNSET,
  RN_A11Y_FALSE,
  RN_A11Y_TRUE,
} RnAccessibleFlag;

void rn_view_set_accessible_state(RnView *self,
                                  RnAccessibleFlag disabled,
                                  RnAccessibleFlag checked,
                                  RnAccessibleFlag selected,
                                  RnAccessibleFlag expanded,
                                  RnAccessibleFlag busy);

// Hidden from assistive technology, for accessible={false} and
// accessibilityElementsHidden.
void rn_view_set_accessible_hidden(RnView *self, gboolean hidden);

// Child management. Mirrors Insert/Remove mutations; index is the position
// within the parent's child list, as Fabric numbers it.
void rn_view_insert_child(RnView *self, RnView *child, int index);
void rn_view_remove_child(RnView *self, RnView *child);

G_END_DECLS
