// The Windows view layer: a view Fabric's mounting can drive.
//
// Deliberately the same shape as `gtk/RnView.h` and `appkit/RnAppKitView.h` --
// a view owns a tag and an absolute frame, does no layout of its own, and
// places each child at the rect the shadow tree already resolved. Yoga has done
// the layout by the time a mutation arrives, and a second layout system
// underneath it is the thing to avoid, not to add.
//
// Three decisions carry the weight here, and the first is the one that makes
// this platform different from the other two.
//
// **A view is not a window.** On GTK a view is a GtkWidget and on macOS an
// NSView; the obvious Windows translation is a child HWND per view, and it is
// wrong. An HWND is a kernel object with a message queue association, USER32
// caps a process at ten thousand of them by default, and a list of a few
// hundred rows would spend the budget. Worse, none of what the analogy promises
// arrives: an HWND clips rectangularly and cannot be rotated, so `transform`
// and rounded `overflow: hidden` would both have to be reimplemented anyway.
// So the host owns one HWND for the surface, and a view is a plain C++ object
// painted by the recursive walk in `paint`. That is closer to GTK's snapshot
// than to AppKit's layer tree, and it means this file owes the platform
// nothing but Direct2D. The one place a real window is still the right answer
// is `<TextInput>`, where an EDIT peer brings input methods and every Windows
// key binding with it -- the same bargain GTK's GtkText and AppKit's
// NSTextField make.
//
// **No flip.** Win32 and Direct2D both put the origin at the top left, which is
// where React Native puts it. The AppKit side needs `isFlipped` and a test that
// states the coordinate in numbers; here the coordinate systems already agree,
// and the only reason to mention it is that a reader coming from that file will
// go looking for the flip.
//
// **Painting is immediate, not retained.** DirectComposition would give a
// visual per view, which is the closer analogue of CALayer, and it would put
// transform and opacity in the compositor. It would also make the offscreen
// snapshot -- the thing that proves this layer paints correctly at all -- a
// second, separate path. One Direct2D walk renders identically to a window and
// to a WIC bitmap, so the picture the tests compare is made by the same code
// that draws the app.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Direct2D's interfaces are structs, so the paint entry point can be declared
// without dragging <d2d1.h> -- and windows.h behind it -- into every
// translation unit that only wants to build a tree. The tests do exactly that.
struct ID2D1RenderTarget;

namespace basalt::win32 {

struct RnRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// How an image fills its frame. Mirrors React Native's ImageResizeMode, minus
// Repeat, which needs a tiled draw rather than one image draw. Declared now
// because `describeTree`'s field order is a cross-platform contract and adding
// a field later in the wrong place would read as every line differing.
enum class RnImageFit {
  Cover,
  Contain,
  Stretch,
  Center,
};

class RnWin32View {
 public:
  explicit RnWin32View(int32_t tag);
  ~RnWin32View();

  RnWin32View(const RnWin32View &) = delete;
  RnWin32View &operator=(const RnWin32View &) = delete;

  // Fabric's tag for this view. Set once, at creation.
  int32_t tag() const { return tag_; }

  // --- Geometry ------------------------------------------------------------

  // The frame the shadow tree resolved, in the parent's coordinates, top-left
  // origin. Applied directly: nothing here recomputes it.
  void setFrame(float x, float y, float width, float height);
  const RnRect &frame() const { return frame_; }

  // Shifts this view's children by (-x, -y), which is how a ScrollView scrolls.
  //
  // An offset applied while painting rather than by moving every child: the
  // frames the mounting manager wrote stay exactly the ones Yoga produced, and
  // hit testing subtracts the same offset, so the two cannot disagree. Both
  // other platforms arrive at the same arrangement by their own route.
  void setScrollOffset(float x, float y);
  float scrollX() const { return scrollX_; }
  float scrollY() const { return scrollY_; }

  // --- Appearance ----------------------------------------------------------

  // Components are premultiplied-free 0..1, as React Native's colour components
  // arrive. Passing hasColor=false means "no background", which is not the same
  // as transparent black: a view with no background does not paint at all.
  void setBackgroundColor(float red, float green, float blue, float alpha, bool hasColor);
  bool hasBackgroundColor() const { return hasBackgroundColor_; }

  void setOpacity(float opacity);
  float opacity() const { return opacity_; }

  // React Native's transform, as sixteen floats in CSS `matrix3d` order. Null
  // clears it.
  //
  // Only the 2D affine part is drawn -- Direct2D has no perspective and neither
  // does the GTK side today -- and the anchor is the view's centre, which is
  // what React Native means by an untouched `transformOrigin`.
  void setTransform(const float *matrix16);
  bool hasTransform() const { return hasTransform_; }

  // `overflow: hidden`. The background is clipped to the corner radius whether
  // or not this is set; this is only about the children.
  void setClipsChildren(bool clips);
  bool clipsChildren() const { return clipsChildren_; }

  void setCornerRadius(float radius);
  float cornerRadius() const { return cornerRadius_; }

  // zIndex reorders painting and never the child list: Fabric's Insert and
  // Remove carry an index into that list, so it has to stay in mutation order.
  void setZIndex(int zIndex);
  int zIndex() const { return zIndex_; }

  // --- Tree ----------------------------------------------------------------
  //
  // A view does not own its children. The mounting registry owns every view,
  // which is what makes Fabric's "a Remove detaches but must not destroy" rule
  // structural here rather than a reference count that has to be got right: a
  // detached view is simply a view with no parent, still in the registry, ready
  // for the Insert that reparents it. Callers with no registry -- the demo, the
  // tests -- hold their views in a vector of unique_ptr.

  void insertChild(RnWin32View *child, int index);
  void removeChild(RnWin32View *child);
  const std::vector<RnWin32View *> &children() const { return children_; }
  RnWin32View *parent() const { return parent_; }

  // --- Painting ------------------------------------------------------------

  // Paints this view and its subtree into `target`, which must be between
  // BeginDraw and EndDraw. The target's transform on entry is the one this
  // view's frame is relative to, and it is restored before returning.
  void paint(ID2D1RenderTarget *target) const;

  // --- Reporting -----------------------------------------------------------

  // One line per view, two spaces of indent per level.
  //
  // The format is a cross-platform contract, not a debug convenience:
  // `scripts/compare_hosts.sh` diffs two hosts' dumps line by line, so the
  // fields and their order match `rn_view_describe_into` in gtk/RnView.cpp and
  // `describeInto:depth:` in appkit/RnAppKitView.mm exactly. A field added in
  // the wrong place makes every line differ.
  std::string describeTree() const;

 private:
  // Paints the children into whatever space the target's transform is already
  // in, which is this view's own. Separate from `paint` so the clip and the
  // scroll offset have an obvious scope, and so no Direct2D type appears above.
  void paintChildren(ID2D1RenderTarget *target) const;
  void describeInto(std::string &out, int depth) const;

  int32_t tag_;
  RnRect frame_;
  RnWin32View *parent_ = nullptr;
  std::vector<RnWin32View *> children_;

  bool hasBackgroundColor_ = false;
  float backgroundColor_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float opacity_ = 1.0f;
  bool clipsChildren_ = false;
  float cornerRadius_ = 0.0f;
  int zIndex_ = 0;
  float scrollX_ = 0.0f;
  float scrollY_ = 0.0f;

  bool hasTransform_ = false;
  // The 2D affine part, in the order Direct2D's Matrix3x2F stores it:
  // _11, _12, _21, _22, _31, _32 -- which is also the order CSS writes a
  // matrix() and the six numbers describeTree prints.
  float transform_[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
};

} // namespace basalt::win32
