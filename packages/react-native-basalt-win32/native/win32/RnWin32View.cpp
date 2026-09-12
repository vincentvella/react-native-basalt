#include "RnWin32View.h"

// Before d2d1.h, which wants the base Windows types and does not pull them in
// itself. NOMINMAX and WIN32_LEAN_AND_MEAN come from the package's CMakeLists;
// without the first, windows.h defines `min` and `max` as macros and breaks
// <algorithm> below at a distance.
#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {
namespace {

// A scoped rounded-rect or rectangle clip.
//
// PushAxisAlignedClip is the cheap way to clip and it is wrong here: it clips
// the rectangle's *bounding box* under the current transform, so a rotated
// ScrollView would clip to a larger upright box and let its content escape at
// the corners. A layer with a geometric mask is correct under any transform,
// and `transform` on a scrolling view is not exotic enough to leave broken.
class ScopedGeometryClip {
 public:
  // A null target means "no clip"; the caller then does not have to choose
  // between an if and a scope.
  ScopedGeometryClip(ID2D1RenderTarget *target, const D2D1_RECT_F &rect, float radius)
      : target_(target) {
    if (target_ == nullptr) {
      return;
    }

    ComPtr<ID2D1Factory> factory;
    target_->GetFactory(factory.GetAddressOf());
    if (!factory) {
      return;
    }

    ComPtr<ID2D1Geometry> mask;
    if (radius > 0.0f) {
      ComPtr<ID2D1RoundedRectangleGeometry> rounded;
      if (FAILED(factory->CreateRoundedRectangleGeometry(
              D2D1::RoundedRect(rect, radius, radius), rounded.GetAddressOf()))) {
        return;
      }
      mask = rounded;
    } else {
      ComPtr<ID2D1RectangleGeometry> plain;
      if (FAILED(factory->CreateRectangleGeometry(rect, plain.GetAddressOf()))) {
        return;
      }
      mask = plain;
    }

    // One layer object per push. Direct2D pools the backing surfaces itself, so
    // this costs an allocation rather than a render target; caching one per
    // view is the optimisation to make if a profile ever asks for it.
    if (FAILED(target_->CreateLayer(nullptr, layer_.GetAddressOf()))) {
      return;
    }

    auto parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::InfiniteRect();
    parameters.geometricMask = mask.Get();
    target_->PushLayer(parameters, layer_.Get());
    pushed_ = true;
  }

  ~ScopedGeometryClip() {
    if (pushed_) {
      target_->PopLayer();
    }
  }

  ScopedGeometryClip(const ScopedGeometryClip &) = delete;
  ScopedGeometryClip &operator=(const ScopedGeometryClip &) = delete;

 private:
  ID2D1RenderTarget *target_;
  ComPtr<ID2D1Layer> layer_;
  bool pushed_ = false;
};

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
  if (target == nullptr) {
    return;
  }

  D2D1::Matrix3x2F parentTransform;
  target->GetTransform(&parentTransform);

  // Direct2D composes row-vector style: `a * b` means apply a, then b. So the
  // frame's translation comes last, and the view's own transform is anchored at
  // its centre by moving the centre to the origin and back around it -- which
  // is what React Native means by an untouched `transformOrigin`, and what
  // every other platform here does.
  D2D1::Matrix3x2F local = D2D1::Matrix3x2F::Translation(frame_.x, frame_.y);
  if (hasTransform_) {
    const float centreX = frame_.width / 2.0f;
    const float centreY = frame_.height / 2.0f;
    const D2D1::Matrix3x2F matrix(
        transform_[0], transform_[1], transform_[2], transform_[3], transform_[4], transform_[5]);
    local = D2D1::Matrix3x2F::Translation(-centreX, -centreY) * matrix *
            D2D1::Matrix3x2F::Translation(centreX, centreY) * local;
  }
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

  // zIndex only reorders painting. The child list itself stays in mutation
  // order, because Fabric's Insert and Remove index into it -- so this sorts a
  // copy, and only when it has to, which is almost never.
  const bool needsSorting =
      std::any_of(children_.begin(), children_.end(), [](const RnWin32View *child) {
        return child->zIndex() != 0;
      });

  if (!needsSorting) {
    for (const RnWin32View *child : children_) {
      child->paint(target);
    }
    return;
  }

  // Stable, so equal zIndex keeps document order -- which is what CSS and React
  // Native both promise.
  std::vector<RnWin32View *> ordered = children_;
  std::stable_sort(ordered.begin(), ordered.end(), [](const RnWin32View *a, const RnWin32View *b) {
    return a->zIndex() < b->zIndex();
  });
  for (const RnWin32View *child : ordered) {
    child->paint(target);
  }
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

  // texture=, text=, editable=, focused and role= belong here, in that order,
  // and arrive with the phases that give this platform an <Image>, a <Text>, a
  // <TextInput> and a UI Automation provider. Named rather than left blank so
  // the next person adds them in the place the other two hosts print them --
  // and note that the three string-valued ones need the same escaping
  // `rn_escape_for_dump` does on GTK, or a newline in a label breaks the
  // one-line-per-view format the comparison depends on.

  out += "\n";

  for (const RnWin32View *child : children_) {
    child->describeInto(out, depth + 1);
  }
}

} // namespace basalt::win32
