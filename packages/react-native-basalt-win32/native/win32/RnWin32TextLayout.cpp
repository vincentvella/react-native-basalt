#include "RnWin32TextLayout.h"

#include "Win32Strings.h"

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {
namespace {

// One shared DirectWrite factory for the process.
//
// DWRITE_FACTORY_TYPE_SHARED rather than ISOLATED: the shared factory caches
// font data across everything in the process and is documented thread-safe,
// which is what lets measurement happen on Fabric's layout thread and painting
// on the UI thread with no lock between them. The GTK side cannot do this --
// see the header.
IDWriteFactory *dwriteFactory() {
  static IDWriteFactory *factory = [] {
    IDWriteFactory *created = nullptr;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                        __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown **>(&created));
    return created;
  }();
  return factory;
}

DWRITE_TEXT_ALIGNMENT toDWriteAlignment(RnTextAlign align) {
  switch (align) {
    case RnTextAlign::Center:
      return DWRITE_TEXT_ALIGNMENT_CENTER;
    case RnTextAlign::Right:
      return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case RnTextAlign::Justified:
      return DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
    case RnTextAlign::Left:
      break;
  }
  return DWRITE_TEXT_ALIGNMENT_LEADING;
}

// What "unconstrained" means to DirectWrite. It has no notion of an infinite
// width and will happily produce NaN metrics from one, so this is a width no
// paragraph reaches rather than FLT_MAX.
constexpr float kUnconstrained = 1.0e6f;

} // namespace

std::shared_ptr<RnWin32TextLayout>
RnWin32TextLayout::create(std::string utf8Text, const RnTextStyle &style, int maximumNumberOfLines) {
  IDWriteFactory *factory = dwriteFactory();
  if (factory == nullptr) {
    return nullptr;
  }

  const std::wstring family = widen(style.fontFamily);

  IDWriteTextFormat *format = nullptr;
  const HRESULT hr = factory->CreateTextFormat(
      family.empty() ? L"Segoe UI" : family.c_str(),
      nullptr, // the system font collection
      style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
      style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      // In DIPs at 96 dpi, which is React Native's density-independent pixel
      // exactly. Nothing here multiplies by a scale factor, and nothing should:
      // the render target carries the display scale, the same division of
      // labour `plan/decisions.md` records for Pango's absolute sizes.
      style.fontSize,
      L"",
      &format);
  if (FAILED(hr) || format == nullptr) {
    return nullptr;
  }

  format->SetTextAlignment(toDWriteAlignment(style.align));
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  if (style.lineHeight > 0.0f) {
    // Baseline at 80% of the line box is DirectWrite's own recommendation for
    // uniform spacing, and it is what an unset baseline would have produced.
    format->SetLineSpacing(
        DWRITE_LINE_SPACING_METHOD_UNIFORM, style.lineHeight, style.lineHeight * 0.8f);
  }

  auto layout = std::shared_ptr<RnWin32TextLayout>(new RnWin32TextLayout());
  layout->utf8_ = std::move(utf8Text);
  layout->utf16_ = widen(layout->utf8_);
  layout->style_ = style;
  layout->maximumNumberOfLines_ = maximumNumberOfLines > 0 ? maximumNumberOfLines : 0;
  layout->format_ = format;
  return layout;
}

std::shared_ptr<RnWin32TextLayout>
RnWin32TextLayout::createFromRuns(const std::vector<RnTextRun> &runs, int maximumNumberOfLines) {
  if (runs.empty()) {
    return create(std::string{}, RnTextStyle{}, maximumNumberOfLines);
  }

  // The first run's style is the paragraph's: alignment and line spacing belong
  // to the whole thing rather than to a span, and React Native applies them
  // that way too.
  std::string joined;
  for (const auto &run : runs) {
    joined += run.text;
  }

  auto layout = create(joined, runs.front().style, maximumNumberOfLines);
  if (layout == nullptr) {
    return nullptr;
  }

  // One style needs no ranges, and skipping them keeps the common case -- a
  // plain <Text> -- free of per-range work.
  if (runs.size() == 1) {
    return layout;
  }

  // Ranges are counted in UTF-16 code units, not bytes and not code points, so
  // each run's extent has to be measured after conversion. Getting this wrong
  // shifts every style after the first emoji.
  unsigned start = 0;
  for (const auto &run : runs) {
    const unsigned length = static_cast<unsigned>(widen(run.text).size());
    layout->runs_.push_back(ResolvedRun{start, length, run.style});
    start += length;
  }
  return layout;
}

RnWin32TextLayout::~RnWin32TextLayout() {
  if (format_ != nullptr) {
    format_->Release();
    format_ = nullptr;
  }
}

IDWriteTextLayout *RnWin32TextLayout::buildLayout(float maxWidth, float maxHeight) const {
  IDWriteFactory *factory = dwriteFactory();
  if (factory == nullptr || format_ == nullptr) {
    return nullptr;
  }

  const float width = maxWidth < 0.0f ? kUnconstrained : maxWidth;
  const float height = maxHeight < 0.0f ? kUnconstrained : maxHeight;

  IDWriteTextLayout *layout = nullptr;
  const HRESULT hr = factory->CreateTextLayout(
      utf16_.c_str(), static_cast<UINT32>(utf16_.size()), format_, width, height, &layout);
  if (FAILED(hr)) {
    return nullptr;
  }

  // Per-span styling, applied here rather than at creation so that measurement
  // and painting see exactly the same runs -- which is the whole reason this
  // function exists.
  for (const auto &run : runs_) {
    const DWRITE_TEXT_RANGE range{run.start, run.length};
    const std::wstring family = widen(run.style.fontFamily);
    if (!family.empty()) {
      layout->SetFontFamilyName(family.c_str(), range);
    }
    layout->SetFontSize(run.style.fontSize, range);
    layout->SetFontWeight(
        run.style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, range);
    layout->SetFontStyle(
        run.style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, range);
  }
  return layout;
}

float RnWin32TextLayout::applyLineLimit(IDWriteTextLayout *layout) const {
  if (layout == nullptr || maximumNumberOfLines_ <= 0) {
    return -1.0f;
  }

  UINT32 lineCount = 0;
  // The documented two-call form: the first is expected to fail with
  // E_NOT_SUFFICIENT_BUFFER and to fill in the count.
  layout->GetLineMetrics(nullptr, 0, &lineCount);
  if (lineCount == 0) {
    return -1.0f;
  }

  std::vector<DWRITE_LINE_METRICS> lines(lineCount);
  if (FAILED(layout->GetLineMetrics(lines.data(), lineCount, &lineCount))) {
    return -1.0f;
  }

  const UINT32 limit = static_cast<UINT32>(maximumNumberOfLines_);
  if (lineCount <= limit) {
    // A limit the paragraph already fits inside is not a truncation, and must
    // not become one. This is the shape of the trap `plan/decisions.md` records
    // Pango springing: there, setting the *default* ellipsize mode with no
    // height collapsed every wrapping paragraph to a single line. DirectWrite
    // will not trim without a height, so declining to set one here is what
    // makes the same default safe.
    return -1.0f;
  }

  float limitHeight = 0.0f;
  for (UINT32 i = 0; i < limit; i++) {
    limitHeight += lines[i].height;
  }

  layout->SetMaxHeight(limitHeight);

  // An ellipsis on the last line that fits, which is React Native's default
  // `ellipsizeMode: 'tail'`.
  ComPtr<IDWriteInlineObject> ellipsis;
  if (SUCCEEDED(dwriteFactory()->CreateEllipsisTrimmingSign(layout, ellipsis.GetAddressOf()))) {
    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    layout->SetTrimming(&trimming, ellipsis.Get());
  }
  return limitHeight;
}

RnTextSize RnWin32TextLayout::measure(float maxWidth) const {
  ComPtr<IDWriteTextLayout> layout;
  layout.Attach(buildLayout(maxWidth, -1.0f));
  if (!layout) {
    return {};
  }

  const float limitHeight = applyLineLimit(layout.Get());

  DWRITE_TEXT_METRICS metrics{};
  if (FAILED(layout->GetMetrics(&metrics))) {
    return {};
  }

  // `width` rather than `widthIncludingTrailingWhitespace`, so a trailing space
  // does not widen the box -- which is what every other platform reports and
  // what makes empty text measure to zero.
  RnTextSize size{metrics.width, metrics.height};
  if (limitHeight >= 0.0f) {
    size.height = std::min(size.height, limitHeight);
  }
  return size;
}

void RnWin32TextLayout::draw(ID2D1RenderTarget *target, float width, float height) const {
  if (target == nullptr) {
    return;
  }

  ComPtr<IDWriteTextLayout> layout;
  layout.Attach(buildLayout(width, height));
  if (!layout) {
    return;
  }
  applyLineLimit(layout.Get());

  ComPtr<ID2D1SolidColorBrush> brush;
  const D2D1_COLOR_F colour =
      D2D1::ColorF(style_.color[0], style_.color[1], style_.color[2], style_.color[3]);
  if (FAILED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
    return;
  }

  target->DrawTextLayout(
      D2D1::Point2F(0.0f, 0.0f), layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

} // namespace basalt::win32
