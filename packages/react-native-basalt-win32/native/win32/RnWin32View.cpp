#include "RnWin32View.h"

#include "RnWin32Image.h"
#include "RnWin32TextLayout.h"
#include "Win32Clip.h"
#include "Win32Strings.h"

// Before d2d1.h, which wants the base Windows types and does not pull them in
// itself. NOMINMAX and WIN32_LEAN_AND_MEAN come from the package's CMakeLists;
// without the first, windows.h defines `min` and `max` as macros and breaks
// <algorithm> below at a distance.
#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {
namespace {

// A scoped opacity layer, for `opacity` on a view.
//
// A layer rather than multiplying every brush's alpha: React Native's opacity
// composites the subtree as a unit, so two overlapping half-transparent
// children do not show through each other.
class ScopedOpacity {
 public:
  ScopedOpacity(ID2D1RenderTarget *target, float opacity) : target_(target) {
    if (opacity >= 1.0f) {
      return;
    }
    if (FAILED(target_->CreateLayer(nullptr, layer_.GetAddressOf()))) {
      return;
    }
    auto parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::InfiniteRect();
    parameters.opacity = opacity;
    target_->PushLayer(parameters, layer_.Get());
    pushed_ = true;
  }

  ~ScopedOpacity() {
    if (pushed_) {
      target_->PopLayer();
    }
  }

  ScopedOpacity(const ScopedOpacity &) = delete;
  ScopedOpacity &operator=(const ScopedOpacity &) = delete;

 private:
  ID2D1RenderTarget *target_;
  ComPtr<ID2D1Layer> layer_;
  bool pushed_ = false;
};

// Escaped the way g_strescape's output is on the GTK side, so a string with a
// quote or a newline in it stays one line and stays comparable.
void appendEscaped(std::string &out, const std::string &text) {
  for (const char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
        break;
    }
  }
}

template <typename... Args>
void appendFormat(std::string &out, const char *format, Args... args) {
  char buffer[256];
  const int written = std::snprintf(buffer, sizeof(buffer), format, args...);
  if (written > 0) {
    // snprintf returns what it *would* have written, so a truncated field is
    // clamped here rather than read past the buffer.
    const int limit = static_cast<int>(sizeof(buffer)) - 1;
    out.append(buffer, static_cast<size_t>(written < limit ? written : limit));
  }
}

unsigned toByte(float component) {
  return static_cast<unsigned>(component * 255.0f + 0.5f);
}

// Two 2D affine matrices, in Direct2D's Matrix3x2F order and its row-vector
// convention: `compose(a, b)` is "apply a, then b", which is what
// `Matrix3x2F::SetProduct(a, b)` computes. Written out in floats rather than
// built with D2D1::Matrix3x2F so that hit testing -- which wants none of
// Direct2D -- can use the same composition painting does.
void compose(const float a[6], const float b[6], float out[6]) {
  const float r[6] = {
      a[0] * b[0] + a[1] * b[2],
      a[0] * b[1] + a[1] * b[3],
      a[2] * b[0] + a[3] * b[2],
      a[2] * b[1] + a[3] * b[3],
      a[4] * b[0] + a[5] * b[2] + b[4],
      a[4] * b[1] + a[5] * b[3] + b[5],
  };
  for (int i = 0; i < 6; i++) {
    out[i] = r[i];
  }
}

// Maps a point through the inverse of `m`. False when `m` is singular, which is
// a view scaled to nothing: it paints no pixels, so nothing can be over it.
bool invertPoint(const float m[6], float x, float y, float &outX, float &outY) {
  const float determinant = m[0] * m[3] - m[1] * m[2];
  if (std::fabs(determinant) < 1e-6f) {
    return false;
  }
  const float shiftedX = x - m[4];
  const float shiftedY = y - m[5];
  outX = (shiftedX * m[3] - shiftedY * m[2]) / determinant;
  outY = (shiftedY * m[0] - shiftedX * m[1]) / determinant;
  return true;
}

} // namespace

RnWin32View::RnWin32View(int32_t tag) : tag_(tag) {}

RnWin32View::~RnWin32View() {
  // A view owns neither its children nor its parent; the registry owns every
  // view. What a destructor does owe is that nothing is left pointing at it.
  if (parent_ != nullptr) {
    parent_->removeChild(this);
  }
  for (RnWin32View *child : children_) {
    child->parent_ = nullptr;
  }
}

// --- Geometry --------------------------------------------------------------

void RnWin32View::setFrame(float x, float y, float width, float height) {
  frame_ = RnRect{x, y, width, height};
}

void RnWin32View::setScrollOffset(float x, float y) {
  scrollX_ = x;
  scrollY_ = y;
}

// --- Appearance ------------------------------------------------------------

void RnWin32View::setBackgroundColor(float red,
                                     float green,
                                     float blue,
                                     float alpha,
                                     bool hasColor) {
  hasBackgroundColor_ = hasColor;
  backgroundColor_[0] = red;
  backgroundColor_[1] = green;
  backgroundColor_[2] = blue;
  backgroundColor_[3] = alpha;
}

void RnWin32View::setOpacity(float opacity) {
  opacity_ = opacity;
}

void RnWin32View::setTransform(const float *matrix16) {
  if (matrix16 == nullptr) {
    hasTransform_ = false;
    transform_[0] = 1.0f;
    transform_[1] = 0.0f;
    transform_[2] = 0.0f;
    transform_[3] = 1.0f;
    transform_[4] = 0.0f;
    transform_[5] = 0.0f;
    return;
  }

  // CSS matrix3d order, which is column-major: m11 m12 m13 m14 m21 ... So the
  // 2D affine part is elements 0, 1, 4, 5, 12 and 13, and those are the same
  // six the GTK side pulls out of its graphene matrix and the macOS side out of
  // its CATransform3D.
  hasTransform_ = true;
  transform_[0] = matrix16[0];
  transform_[1] = matrix16[1];
  transform_[2] = matrix16[4];
  transform_[3] = matrix16[5];
  transform_[4] = matrix16[12];
  transform_[5] = matrix16[13];
}

void RnWin32View::setClipsChildren(bool clips) {
  clipsChildren_ = clips;
}

void RnWin32View::setCornerRadius(float radius) {
  cornerRadius_ = radius;
}

void RnWin32View::setZIndex(int zIndex) {
  zIndex_ = zIndex;
}

void RnWin32View::setHidden(bool hidden) {
  hidden_ = hidden;
}

// --- Content ----------------------------------------------------------------

void RnWin32View::setTextLayout(std::shared_ptr<RnWin32TextLayout> layout) {
  textLayout_ = std::move(layout);
}

void RnWin32View::setImage(std::shared_ptr<RnWin32Image> image, RnImageFit fit) {
  image_ = std::move(image);
  imageFit_ = fit;
}

// --- Accessibility ----------------------------------------------------------

void RnWin32View::setAccessibleInfo(const RnAccessibleInfo &info) {
  accessible_ = info;
}

IRawElementProviderSimple *RnWin32View::createAccessibleProvider() const {
  return basalt::win32::createAccessibleProvider(accessible_);
}

// --- Geometry, resolved -----------------------------------------------------

void RnWin32View::localToParent(float out[6]) const {
  const float translation[6] = {1.0f, 0.0f, 0.0f, 1.0f, frame_.x, frame_.y};
  if (!hasTransform_) {
    for (int i = 0; i < 6; i++) {
      out[i] = translation[i];
    }
    return;
  }

  // Anchored at the view's centre by moving the centre to the origin and back
  // around the transform, which is what React Native means by an untouched
  // `transformOrigin` and what every other platform here does. The frame's
  // translation comes last because this composes "apply a, then b".
  const float centreX = frame_.width / 2.0f;
  const float centreY = frame_.height / 2.0f;
  const float toOrigin[6] = {1.0f, 0.0f, 0.0f, 1.0f, -centreX, -centreY};
  const float fromOrigin[6] = {1.0f, 0.0f, 0.0f, 1.0f, centreX, centreY};

  float composed[6];
  compose(toOrigin, transform_, composed);
  compose(composed, fromOrigin, composed);
  compose(composed, translation, out);
}

std::vector<RnWin32View *> RnWin32View::childrenInPaintOrder() const {
  std::vector<RnWin32View *> ordered = children_;
  const bool needsSorting =
      std::any_of(ordered.begin(), ordered.end(), [](const RnWin32View *child) {
        return child->zIndex() != 0;
      });
  if (needsSorting) {
    // Stable, so equal zIndex keeps document order -- which is what CSS and
    // React Native both promise.
    std::stable_sort(
        ordered.begin(), ordered.end(), [](const RnWin32View *a, const RnWin32View *b) {
          return a->zIndex() < b->zIndex();
        });
  }
  return ordered;
}

// --- Tree ------------------------------------------------------------------

void RnWin32View::insertChild(RnWin32View *child, int index) {
  if (child == nullptr || child == this) {
    return;
  }
  if (child->parent_ != nullptr) {
    child->parent_->removeChild(child);
  }

  // Fabric's index counts positions in the parent's *final* child list, so a
  // later mutation lands between two existing children. Clamped rather than
  // trusted: a transaction racing a surface teardown can name a position that
  // no longer exists, and that has to be survivable.
  const int count = static_cast<int>(children_.size());
  const int at = std::clamp(index, 0, count);
  children_.insert(children_.begin() + at, child);
  child->parent_ = this;
}

void RnWin32View::removeChild(RnWin32View *child) {
  const auto it = std::find(children_.begin(), children_.end(), child);
  if (it == children_.end()) {
    return;
  }
  (*it)->parent_ = nullptr;
  children_.erase(it);
}

// --- Painting --------------------------------------------------------------

void RnWin32View::paint(ID2D1RenderTarget *target) const {
  if (target == nullptr || hidden_) {
    return;
  }

  D2D1::Matrix3x2F parentTransform;
  target->GetTransform(&parentTransform);

  // From localToParent rather than composed here, so that hit testing -- which
  // inverts the same six numbers -- cannot end up with a different idea of
  // where this view is.
  float localValues[6];
  localToParent(localValues);
  const D2D1::Matrix3x2F local(localValues[0],
                               localValues[1],
                               localValues[2],
                               localValues[3],
                               localValues[4],
                               localValues[5]);
  target->SetTransform(local * parentTransform);

  const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);

  {
    const ScopedOpacity fade(target, opacity_);

    // The background is always clipped to the rounded box, even when children
    // are not: `overflow: visible` lets a child escape the corner, but the
    // view's own fill still has to respect its border radius.
    if (hasBackgroundColor_) {
      ComPtr<ID2D1SolidColorBrush> brush;
      // An explicit sRGB colour. Letting the target pick a device space shifts
      // every colour slightly on a wide-gamut display -- not wrong exactly, but
      // different from the same app on Linux, which is the one thing this
      // project is trying not to be.
      const D2D1_COLOR_F colour = D2D1::ColorF(
          backgroundColor_[0], backgroundColor_[1], backgroundColor_[2], backgroundColor_[3]);
      if (SUCCEEDED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
        if (cornerRadius_ > 0.0f) {
          target->FillRoundedRectangle(D2D1::RoundedRect(bounds, cornerRadius_, cornerRadius_),
                                       brush.Get());
        } else {
          target->FillRectangle(bounds, brush.Get());
        }
      }
    }

    // Then the image, then the text, then the children -- the order
    // `rn_view_snapshot` uses on GTK. Nothing in React Native puts two of these
    // on one view, but the order still has to be decided somewhere, and it is
    // cheaper to match than to argue about later.
    if (image_ != nullptr) {
      image_->draw(target, frame_.width, frame_.height, imageFit_);
    }

    // Text sits above the background and below any children, which is the
    // order `<Text>` with nested views expects. The paragraph draws itself at
    // the view's own origin, in the box Yoga gave the view -- the same box it
    // was measured against, because both go through RnWin32TextLayout.
    if (textLayout_ != nullptr) {
      textLayout_->draw(target, frame_.width, frame_.height);
    }

    paintChildren(target);
  }

  target->SetTransform(parentTransform);
}

// Reads the current transform rather than being handed one, which is what keeps
// D2D1_MATRIX_3X2_F -- and windows.h behind it -- out of the header.
void RnWin32View::paintChildren(ID2D1RenderTarget *target) const {
  if (children_.empty()) {
    return;
  }

  D2D1::Matrix3x2F worldTransform;
  target->GetTransform(&worldTransform);

  // `overflow: hidden`, and only that: the background above is clipped whether
  // or not this is set.
  const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);
  ScopedGeometryClip clip(clipsChildren_ ? target : nullptr, bounds, cornerRadius_);

  // A ScrollView's offset moves its children and nothing else, so it belongs
  // between this view's transform and theirs.
  if (scrollX_ != 0.0f || scrollY_ != 0.0f) {
    target->SetTransform(D2D1::Matrix3x2F::Translation(-scrollX_, -scrollY_) * worldTransform);
  }

  // Forwards, so the last child painted is on top. Hit testing walks the same
  // list backwards.
  for (const RnWin32View *child : childrenInPaintOrder()) {
    child->paint(target);
  }
}

// --- Hit testing ------------------------------------------------------------

namespace {

// The spelling React Native uses for the prop, which is also CSS's, so the
// three hosts' dumps say the same words.
const char *pointerEventsName(RnWin32View::PointerEvents mode) {
  switch (mode) {
    case RnWin32View::PointerEvents::None:
      return "none";
    case RnWin32View::PointerEvents::BoxNone:
      return "box-none";
    case RnWin32View::PointerEvents::BoxOnly:
      return "box-only";
    case RnWin32View::PointerEvents::Auto:
      break;
  }
  return "auto";
}

} // namespace

RnWin32View *hitTest(RnWin32View *root, float x, float y) {
  // `pointerEvents: none` takes the view and everything inside it out of hit
  // testing entirely, so the caller's loop carries on to whatever is behind.
  if (root == nullptr || root->hidden() ||
      root->pointerEvents() == RnWin32View::PointerEvents::None) {
    return nullptr;
  }

  const RnRect &frame = root->frame();
  if (x < 0.0f || y < 0.0f || x >= frame.width || y >= frame.height) {
    return nullptr;
  }

  // Children are placed in this view's content space, which a scroll offset
  // shifts. Adding it back here is what makes hit testing follow a scroll with
  // nothing in this function knowing what a ScrollView is -- the same offset
  // `paintChildren` subtracts, from the same two fields.
  const float contentX = x + root->scrollX();
  const float contentY = y + root->scrollY();

  // Backwards: the last child painted is the topmost, and the topmost is what a
  // press should land on.
  //
  // `box-only` is the one mode that skips this: the box is the target and
  // nothing inside it is, which is what makes an overlay swallow a press meant
  // for a button drawn on top of it.
  if (root->pointerEvents() != RnWin32View::PointerEvents::BoxOnly) {
    const std::vector<RnWin32View *> ordered = root->childrenInPaintOrder();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
      RnWin32View *child = *it;
      float local[6];
      child->localToParent(local);

      float childX = 0.0f;
      float childY = 0.0f;
      if (!invertPoint(local, contentX, contentY, childX, childY)) {
        continue;
      }
      if (RnWin32View *hit = hitTest(child, childX, childY)) {
        return hit;
      }
    }
  }

  // `box-none` is transparent to a press that misses everything inside it.
  // Returning null rather than the parent is the whole of it: the caller is
  // partway through its own list of children, so the press carries on to the
  // sibling *behind* this view -- which is what the absolutely-positioned
  // overlay this mode exists for is asking for.
  if (root->pointerEvents() == RnWin32View::PointerEvents::BoxNone) {
    return nullptr;
  }

  // A point inside this view but over none of its children is this view. React
  // Native's responder system needs a target for every press inside the
  // surface, and the root is the honest answer for one that missed everything.
  return root;
}

// --- Reporting -------------------------------------------------------------

std::string RnWin32View::describeTree() const {
  std::string out;
  describeInto(out, 0);
  return out;
}

void RnWin32View::describeInto(std::string &out, int depth) const {
  for (int i = 0; i < depth; i++) {
    out += "  ";
  }

  appendFormat(out,
               "view tag=%d frame=(%g,%g %gx%g)",
               tag_,
               static_cast<double>(frame_.x),
               static_cast<double>(frame_.y),
               static_cast<double>(frame_.width),
               static_cast<double>(frame_.height));

  if (hasBackgroundColor_) {
    appendFormat(out,
                 " bg=#%02x%02x%02x%02x",
                 toByte(backgroundColor_[0]),
                 toByte(backgroundColor_[1]),
                 toByte(backgroundColor_[2]),
                 toByte(backgroundColor_[3]));
  }
  // Field order matches the GTK and AppKit sides exactly -- bg, opacity, clip,
  // transform, scroll, then the content fields -- because
  // scripts/compare_hosts.sh diffs the dumps line by line and a reordering
  // would read as every line differing.
  if (opacity_ < 1.0f) {
    appendFormat(out, " opacity=%g", static_cast<double>(opacity_));
  }
  if (clipsChildren_) {
    out += " clip";
  }
  // Per-corner radii and per-edge borders, in the fields and the order the GTK
  // and AppKit sides print them. This host has one circular radius rather than
  // four elliptical ones, so it prints that radius eight times -- which is the
  // truth about what it paints, and makes a view rounded here and there
  // compare equal while an elliptical or per-corner one does not.
  //
  // There is no borderw=/borderc= here because this host does not draw borders
  // at all yet; see setCornerRadius's note. That is a real divergence from
  // Linux, and printing nothing is what lets scripts/compare_hosts.sh say so
  // rather than hiding it behind a dump that cannot express it.
  if (cornerRadius_ > 0.0f) {
    const double r = static_cast<double>(cornerRadius_);
    appendFormat(out, " radii=(%g,%g,%g,%g,%g,%g,%g,%g)", r, r, r, r, r, r, r, r);
  }
  if (hasTransform_) {
    // The 2D affine part, in the order CSS writes a matrix(): a, b, c, d, tx,
    // ty. The other two platforms print the same six from their own matrix
    // types, so a transform is comparable across all three -- without which a
    // view rotated on one desktop and not on another looks identical here.
    appendFormat(out,
                 " transform=(%g,%g,%g,%g,%g,%g)",
                 static_cast<double>(transform_[0]),
                 static_cast<double>(transform_[1]),
                 static_cast<double>(transform_[2]),
                 static_cast<double>(transform_[3]),
                 static_cast<double>(transform_[4]),
                 static_cast<double>(transform_[5]));
  }
  if (scrollX_ != 0.0f || scrollY_ != 0.0f) {
    appendFormat(out,
                 " scroll=(%g,%g)",
                 static_cast<double>(scrollX_),
                 static_cast<double>(scrollY_));
  }
  // Printed only when it is not the default, like every other field here.
  // Worth printing at all because it is invisible: a view with
  // `pointerEvents: none` is drawn exactly like one without, and the only way
  // the cross-host diff can say the prop arrived on all three is if each one
  // reports it.
  if (pointerEvents_ != PointerEvents::Auto) {
    appendFormat(out, " pe=%s", pointerEventsName(pointerEvents_));
  }

  if (image_ != nullptr) {
    // The same `texture=WxH fit=<name>` the other two hosts emit. The fit is
    // here because it is the only thing about a drawn image that a frame cannot
    // show: two views the same size holding the same picture are identical in
    // every other field of this dump and different on screen.
    appendFormat(out, " texture=%ux%u", image_->width(), image_->height());
    appendFormat(out, " fit=%s", imageFitName(imageFit_));
  }

  if (textLayout_ != nullptr) {
    const std::string &text = textLayout_->text();
    if (!text.empty()) {
      out += " text=\"";
      appendEscaped(out, text);
      out += "\"";
    }
  }

  // A text field's content lives in its EDIT peer, not in a layout, so it would
  // otherwise be invisible to every test that reads this tree -- and the dump
  // would differ from the other two hosts' for a field that was working.
  if (editablePeer_ != nullptr) {
    const int length = GetWindowTextLength(editablePeer_);
    std::wstring wide(static_cast<size_t>(length) + 1, L'\0');
    GetWindowText(editablePeer_, wide.data(), length + 1);
    wide.resize(static_cast<size_t>(length));

    out += " editable=\"";
    appendEscaped(out, narrow(wide));
    out += "\"";

    // GetFocus is per-thread rather than global, which is the right question
    // here: this runs on the thread that owns the window, and what is being
    // asked is whether the field has the keyboard within it.
    if (GetFocus() == editablePeer_) {
      out += " focused";
    }
  }

  // React Native's role name, not UIA's. This dump is compared line by line
  // across three platforms, and each reporting its own toolkit's vocabulary
  // would make every accessible view look like a difference. That the *UIA*
  // control type was really applied is asserted in
  // tests/test_win32_accessibility.cpp, which is where a platform question
  // belongs.
  if (!accessible_.role.empty()) {
    appendFormat(out, " role=%s", accessible_.role.c_str());
  }

  out += "\n";

  for (const RnWin32View *child : children_) {
    child->describeInto(out, depth + 1);
  }
}

} // namespace basalt::win32
