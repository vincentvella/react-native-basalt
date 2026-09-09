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

static void rn_layout_allocate(GtkLayoutManager * /*manager*/,
                               GtkWidget *widget,
                               int /*width*/,
                               int /*height*/,
                               int /*baseline*/) {
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (!gtk_widget_should_layout(child) || !RN_IS_VIEW(child)) {
      continue;
    }

    graphene_rect_t frame;
    rn_view_get_frame(RN_VIEW(child), &frame);

    graphene_point_t origin;
    origin.x = frame.origin.x;
    origin.y = frame.origin.y;

    GskTransform *transform = gsk_transform_translate(nullptr, &origin);
    gtk_widget_allocate(child,
                        static_cast<int>(frame.size.width),
                        static_cast<int>(frame.size.height),
                        -1,
                        transform);
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
};

G_DEFINE_TYPE(RnView, rn_view, GTK_TYPE_WIDGET)

static void rn_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RnView *self = RN_VIEW(widget);

  const int width = gtk_widget_get_width(widget);
  const int height = gtk_widget_get_height(widget);

  const gboolean needs_opacity_layer = self->opacity < 1.0;
  if (needs_opacity_layer) {
    gtk_snapshot_push_opacity(snapshot, self->opacity);
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

  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    gtk_widget_snapshot_child(widget, child, snapshot);
  }

  if (needs_opacity_layer) {
    gtk_snapshot_pop(snapshot);
  }
}

static void rn_view_dispose(GObject *object) {
  GtkWidget *widget = GTK_WIDGET(object);

  // A GtkWidget must unparent its children before it goes away, or GTK warns
  // and leaks. Delete mutations can arrive with children still attached.
  GtkWidget *child = gtk_widget_get_first_child(widget);
  while (child != nullptr) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_widget_unparent(child);
    child = next;
  }

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
