// react-native-linux — GTK4 view layer
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

// Fabric tag, for debugging and hit-test attribution.
int rn_view_get_tag(RnView *self);

// Frame in parent coordinates, straight from LayoutMetrics::frame.
void rn_view_set_frame(RnView *self, float x, float y, float width, float height);
void rn_view_get_frame(RnView *self, graphene_rect_t *out);

// BaseViewProps::backgroundColor. Passing has_color = FALSE paints nothing.
void rn_view_set_background_color(RnView *self, gboolean has_color, const GdkRGBA *color);

// BaseViewProps::opacity.
void rn_view_set_opacity(RnView *self, double opacity);

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

// Child management. Mirrors Insert/Remove mutations; index is the position
// within the parent's child list, as Fabric numbers it.
void rn_view_insert_child(RnView *self, RnView *child, int index);
void rn_view_remove_child(RnView *self, RnView *child);

G_END_DECLS
