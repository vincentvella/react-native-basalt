// A scoped clip that survives a rotation.
//
// PushAxisAlignedClip is the cheap way to clip and it is wrong wherever a
// transform is in play: it clips to the rectangle's *bounding box* under the
// current transform, so inside a rotated view the clip is a larger upright box
// and content escapes at the corners. A layer with a geometric mask is correct
// under any transform.
//
// Shared because there are two places that clip -- `overflow: hidden` on a view
// and an <Image> that must not paint outside its own frame -- and a platform
// where one of them handles rotation and the other does not is worse than one
// where neither does: the difference only shows up in a picture, on a screen
// nobody is looking at.

#pragma once

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

namespace basalt::win32 {

class ScopedGeometryClip {
 public:
  // A null target means "no clip"; the caller then does not have to choose
  // between an if and a scope.
  ScopedGeometryClip(ID2D1RenderTarget *target, const D2D1_RECT_F &rect, float radius)
      : target_(target) {
    if (target_ == nullptr) {
      return;
    }

    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    target_->GetFactory(factory.GetAddressOf());
    if (!factory) {
      return;
    }

    Microsoft::WRL::ComPtr<ID2D1Geometry> mask;
    if (radius > 0.0f) {
      Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> rounded;
      if (FAILED(factory->CreateRoundedRectangleGeometry(
              D2D1::RoundedRect(rect, radius, radius), rounded.GetAddressOf()))) {
        return;
      }
      mask = rounded;
    } else {
      Microsoft::WRL::ComPtr<ID2D1RectangleGeometry> plain;
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
  Microsoft::WRL::ComPtr<ID2D1Layer> layer_;
  bool pushed_ = false;
};

} // namespace basalt::win32
