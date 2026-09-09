#include "RnView.h"

// ---------------------------------------------------------------------------
// RnLayout
// ---------------------------------------------------------------------------

struct _RnLayout {
  GtkLayoutManager parent_instance;
};

G_DEFINE_TYPE(RnLayout, rn_layout, GTK_TYPE_LAYOUT_MANAGER)

static void rn_layout_measure(GtkLayoutManager * /*manager*/,
                              GtkWidget * /*widget*/,
                              GtkOrientation /*orientation*/,
                              int /*for_size*/,
                              int *minimum,
                              int *natural,
                              int *minimum_baseline,
                              int *natural_baseline) {
  // RN owns sizing. Reporting zero keeps GTK from ever second-guessing Yoga.
  *minimum = 0;
  *natural = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

// Defined below; lets allocate report the root's size without exposing the
// struct.
static void rn_view_notify_allocation(RnView *self, int width, int height);
static void rn_view_scroll_offset(RnView *self, double *offset_x, double *offset_y);

static void rn_layout_allocate(GtkLayoutManager * /*manager*/,
                               GtkWidget *widget,
                               int width,
                               int height,
                               int /*baseline*/) {
  // A scrolling view moves its children rather than its own frame. Reading the
  // offset here, once, keeps it out of every child's stored frame.
  double scroll_x = 0.0;
  double scroll_y = 0.0;
  if (RN_IS_VIEW(widget)) {
    rn_view_scroll_offset(RN_VIEW(widget), &scroll_x, &scroll_y);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (!gtk_widget_should_layout(child) || !RN_IS_VIEW(child)) {
      continue;
    }

    graphene_rect_t frame;
    rn_view_get_frame(RN_VIEW(child), &frame);

    graphene_point_t origin;
    origin.x = frame.origin.x - static_cast<float>(scroll_x);
    origin.y = frame.origin.y - static_cast<float>(scroll_y);

    GskTransform *transform = gsk_transform_translate(nullptr, &origin);
    gtk_widget_allocate(child,
                        static_cast<int>(frame.size.width),
                        static_cast<int>(frame.size.height),
                        -1,
                        transform);
  }

  // Children are placed from frames RN already decided, so this is not part of
  // laying out; it is how the widget tells the host what size the window gave
  // it. On a surface root that becomes the next layout constraint.
  if (RN_IS_VIEW(widget)) {
    rn_view_notify_allocation(RN_VIEW(widget), width, height);
  }
}

static void rn_layout_class_init(RnLayoutClass *klass) {
  GtkLayoutManagerClass *layout_class = GTK_LAYOUT_MANAGER_CLASS(klass);
  layout_class->measure = rn_layout_measure;
  layout_class->allocate = rn_layout_allocate;
}

static void rn_layout_init(RnLayout * /*self*/) {}

// ---------------------------------------------------------------------------
// RnView
// ---------------------------------------------------------------------------

struct _RnView {
  GtkWidget parent_instance;

  int tag;
  graphene_rect_t frame;

  gboolean has_background_color;
  GdkRGBA background_color;
  double opacity;

  PangoLayout *text_layout;
  GdkRGBA text_color;

  GdkTexture *texture;
  RnImageFit texture_fit;

  gboolean clips_children;
  double scroll_x;
  double scroll_y;

  RnViewResizeFunc resize_callback;
  gpointer resize_data;
  int allocated_width;
  int allocated_height;
};

static void rn_view_notify_allocation(RnView *self, int width, int height) {
  if (self->resize_callback == nullptr) {
    return;
  }
  // allocate runs on every layout pass, most of which change nothing. Only a
  // real size change is worth a Fabric commit.
  if (width == self->allocated_width && height == self->allocated_height) {
    return;
  }
  self->allocated_width = width;
  self->allocated_height = height;
  self->resize_callback(self, width, height, self->resize_data);
}

void rn_view_set_resize_callback(RnView *self, RnViewResizeFunc callback, gpointer user_data) {
  g_return_if_fail(RN_IS_VIEW(self));
  self->resize_callback = callback;
  self->resize_data = user_data;
}

G_DEFINE_TYPE(RnView, rn_view, GTK_TYPE_WIDGET)

static void rn_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RnView *self = RN_VIEW(widget);

  const int width = gtk_widget_get_width(widget);
  const int height = gtk_widget_get_height(widget);

  const gboolean needs_opacity_layer = self->opacity < 1.0;
  if (needs_opacity_layer) {
    gtk_snapshot_push_opacity(snapshot, self->opacity);
  }

  if (self->clips_children) {
    graphene_rect_t bounds;
    bounds.origin.x = 0.0f;
    bounds.origin.y = 0.0f;
    bounds.size.width = static_cast<float>(width);
    bounds.size.height = static_cast<float>(height);
    gtk_snapshot_push_clip(snapshot, &bounds);
  }

  if (self->has_background_color) {
    graphene_rect_t bounds;
    bounds.origin.x = 0.0f;
    bounds.origin.y = 0.0f;
    bounds.size.width = static_cast<float>(width);
    bounds.size.height = static_cast<float>(height);
    gtk_snapshot_append_color(snapshot, &self->background_color, &bounds);
  }

  // TODO(borders): borderRadii/borderWidth/borderColor want a rounded-rect
  // clip here (gtk_snapshot_push_rounded_clip) plus a border node.

  if (self->texture != nullptr && width > 0 && height > 0) {
    const float viewWidth = static_cast<float>(width);
    const float viewHeight = static_cast<float>(height);
    const float imageWidth = static_cast<float>(gdk_texture_get_width(self->texture));
    const float imageHeight = static_cast<float>(gdk_texture_get_height(self->texture));

    graphene_rect_t destination;
    destination.origin.x = 0.0f;
    destination.origin.y = 0.0f;
    destination.size.width = viewWidth;
    destination.size.height = viewHeight;

    if (self->texture_fit != RN_IMAGE_FIT_STRETCH && imageWidth > 0 && imageHeight > 0) {
      float scale = 1.0f;
      switch (self->texture_fit) {
        case RN_IMAGE_FIT_CONTAIN:
          scale = MIN(viewWidth / imageWidth, viewHeight / imageHeight);
          break;
        case RN_IMAGE_FIT_COVER:
          scale = MAX(viewWidth / imageWidth, viewHeight / imageHeight);
          break;
        case RN_IMAGE_FIT_CENTER:
          // Centre at natural size, but never larger than the frame -- which is
          // what React Native's `center` does.
          scale = MIN(1.0f, MIN(viewWidth / imageWidth, viewHeight / imageHeight));
          break;
        case RN_IMAGE_FIT_STRETCH:
          break;
      }
      destination.size.width = imageWidth * scale;
      destination.size.height = imageHeight * scale;
      destination.origin.x = (viewWidth - destination.size.width) / 2.0f;
      destination.origin.y = (viewHeight - destination.size.height) / 2.0f;
    }

    // cover and center can put pixels outside the frame, and an <Image> never
    // paints beyond its own box on iOS or Android.
    const gboolean needs_clip = self->texture_fit == RN_IMAGE_FIT_COVER ||
                                self->texture_fit == RN_IMAGE_FIT_CENTER;
    if (needs_clip) {
      graphene_rect_t clip;
      clip.origin.x = 0.0f;
      clip.origin.y = 0.0f;
      clip.size.width = viewWidth;
      clip.size.height = viewHeight;
      gtk_snapshot_push_clip(snapshot, &clip);
    }
    gtk_snapshot_append_texture(snapshot, self->texture, &destination);
    if (needs_clip) {
      gtk_snapshot_pop(snapshot);
    }
  }

  // Text sits above the background and below any children, which is the order
  // <Text> with nested views expects.
  if (self->text_layout != nullptr) {
    gtk_snapshot_append_layout(snapshot, self->text_layout, &self->text_color);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    gtk_widget_snapshot_child(widget, child, snapshot);
  }

  if (self->clips_children) {
    gtk_snapshot_pop(snapshot);
  }

  if (needs_opacity_layer) {
    gtk_snapshot_pop(snapshot);
  }
}

static void rn_view_dispose(GObject *object) {
  RnView *self = RN_VIEW(object);
  GtkWidget *widget = GTK_WIDGET(object);

  // A GtkWidget must unparent its children before it goes away, or GTK warns
  // and leaks. Delete mutations can arrive with children still attached.
  GtkWidget *child = gtk_widget_get_first_child(widget);
  while (child != nullptr) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_widget_unparent(child);
    child = next;
  }

  g_clear_object(&self->text_layout);
  g_clear_object(&self->texture);

  G_OBJECT_CLASS(rn_view_parent_class)->dispose(object);
}

static void rn_view_class_init(RnViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = rn_view_dispose;
  widget_class->snapshot = rn_view_snapshot;

  gtk_widget_class_set_layout_manager_type(widget_class, RN_TYPE_LAYOUT);
}

static void rn_view_init(RnView *self) {
  self->tag = 0;
  graphene_rect_init(&self->frame, 0.0f, 0.0f, 0.0f, 0.0f);
  self->has_background_color = FALSE;
  self->background_color = GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  self->opacity = 1.0;
  self->text_layout = nullptr;
  self->text_color = GdkRGBA{0.0f, 0.0f, 0.0f, 1.0f};
  self->texture = nullptr;
  self->texture_fit = RN_IMAGE_FIT_COVER;
  self->clips_children = FALSE;
  self->scroll_x = 0.0;
  self->scroll_y = 0.0;
  self->resize_callback = nullptr;
  self->resize_data = nullptr;
  // -1, not 0: a first allocation of 0x0 is a real transition worth reporting.
  self->allocated_width = -1;
  self->allocated_height = -1;
}

RnView *rn_view_new(int tag) {
  RnView *self = RN_VIEW(g_object_new(RN_TYPE_VIEW, nullptr));
  self->tag = tag;
  return self;
}

int rn_view_get_tag(RnView *self) {
  return self->tag;
}

void rn_view_set_frame(RnView *self, float x, float y, float width, float height) {
  graphene_rect_init(&self->frame, x, y, width, height);

  // The frame lives in the parent's coordinate space, so it is the parent's
  // allocation that has to be redone.
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(self));
  gtk_widget_queue_allocate(parent != nullptr ? parent : GTK_WIDGET(self));
}

void rn_view_get_frame(RnView *self, graphene_rect_t *out) {
  *out = self->frame;
}

void rn_view_set_background_color(RnView *self, gboolean has_color, const GdkRGBA *color) {
  self->has_background_color = has_color;
  if (has_color && color != nullptr) {
    self->background_color = *color;
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_opacity(RnView *self, double opacity) {
  self->opacity = CLAMP(opacity, 0.0, 1.0);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_text_layout(RnView *self, PangoLayout *layout, const GdkRGBA *color) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (layout != nullptr) {
    g_object_ref(layout);
  }
  g_clear_object(&self->text_layout);
  self->text_layout = layout;

  if (color != nullptr) {
    self->text_color = *color;
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_texture(RnView *self, GdkTexture *texture, RnImageFit fit) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (texture != nullptr) {
    g_object_ref(texture);
  }
  g_clear_object(&self->texture);
  self->texture = texture;
  self->texture_fit = fit;

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void rn_view_scroll_offset(RnView *self, double *offset_x, double *offset_y) {
  *offset_x = self->scroll_x;
  *offset_y = self->scroll_y;
}

void rn_view_set_clips_children(RnView *self, gboolean clips) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->clips_children == clips) {
    return;
  }
  self->clips_children = clips;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_scroll_offset(RnView *self, double offset_x, double offset_y) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->scroll_x == offset_x && self->scroll_y == offset_y) {
    return;
  }
  self->scroll_x = offset_x;
  self->scroll_y = offset_y;
  // Children move, so the layout has to run again; a redraw alone would leave
  // them where they were.
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

void rn_view_get_scroll_offset(RnView *self, double *offset_x, double *offset_y) {
  g_return_if_fail(RN_IS_VIEW(self));
  rn_view_scroll_offset(self, offset_x, offset_y);
}

static void rn_view_describe_into(RnView *self, GString *out, int depth) {
  for (int i = 0; i < depth; i++) {
    g_string_append(out, "  ");
  }

  g_string_append_printf(out,
                         "view tag=%d frame=(%g,%g %gx%g)",
                         self->tag,
                         static_cast<double>(self->frame.origin.x),
                         static_cast<double>(self->frame.origin.y),
                         static_cast<double>(self->frame.size.width),
                         static_cast<double>(self->frame.size.height));

  if (self->has_background_color) {
    g_string_append_printf(out,
                           " bg=#%02x%02x%02x%02x",
                           static_cast<unsigned>(self->background_color.red * 255.0 + 0.5),
                           static_cast<unsigned>(self->background_color.green * 255.0 + 0.5),
                           static_cast<unsigned>(self->background_color.blue * 255.0 + 0.5),
                           static_cast<unsigned>(self->background_color.alpha * 255.0 + 0.5));
  }
  if (self->opacity < 1.0) {
    g_string_append_printf(out, " opacity=%g", self->opacity);
  }
  if (self->clips_children) {
    g_string_append(out, " clip");
  }
  if (self->scroll_x != 0.0 || self->scroll_y != 0.0) {
    g_string_append_printf(out, " scroll=(%g,%g)", self->scroll_x, self->scroll_y);
  }
  if (self->texture != nullptr) {
    g_string_append_printf(out,
                           " texture=%dx%d",
                           gdk_texture_get_width(self->texture),
                           gdk_texture_get_height(self->texture));
  }
  if (self->text_layout != nullptr) {
    const char *text = pango_layout_get_text(self->text_layout);
    if (text != nullptr && *text != '\0') {
      char *escaped = g_strescape(text, nullptr);
      g_string_append_printf(out, " text=\"%s\"", escaped);
      g_free(escaped);
    }
  }
  g_string_append_c(out, '\n');

  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(self)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (RN_IS_VIEW(child)) {
      rn_view_describe_into(RN_VIEW(child), out, depth + 1);
    }
  }
}

char *rn_view_describe_tree(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  GString *out = g_string_new(nullptr);
  rn_view_describe_into(self, out, 0);
  return g_string_free(out, FALSE);
}

void rn_view_insert_child(RnView *self, RnView *child, int index) {
  GtkWidget *child_widget = GTK_WIDGET(child);
  GtkWidget *parent_widget = GTK_WIDGET(self);

  // Fabric indexes into the parent's current child list. Walk to the sibling
  // currently at `index` and insert before it; a past-the-end index appends.
  GtkWidget *sibling = gtk_widget_get_first_child(parent_widget);
  for (int i = 0; i < index && sibling != nullptr; i++) {
    sibling = gtk_widget_get_next_sibling(sibling);
  }

  if (sibling != nullptr) {
    gtk_widget_insert_before(child_widget, parent_widget, sibling);
  } else {
    gtk_widget_set_parent(child_widget, parent_widget);
  }
}

void rn_view_remove_child(RnView * /*self*/, RnView *child) {
  // Remove detaches but must not destroy: a Delete mutation for the same tag
  // follows separately, and until then the view may be re-Inserted elsewhere.
  gtk_widget_unparent(GTK_WIDGET(child));
}
