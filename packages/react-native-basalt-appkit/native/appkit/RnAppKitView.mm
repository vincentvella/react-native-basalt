#import "RnAppKitView.h"

#include <cmath>

#import "RnTextLayout.h"

#import <QuartzCore/QuartzCore.h>

// Topmost first: AppKit's subviews array is back to front, and a hit test wants
// the view that is drawn on top of the others.
//
// A child is only entered when the point is inside its frame, which means a
// child that overflows a parent -- React Native's default, since `overflow` is
// `visible` -- is not reachable through that parent. In practice Fabric's view
// flattening hoists most such children out to an ancestor that does contain
// them, so this matters less than it reads; the GTK side has the same limit for
// the same reason.
RnAppKitView *RnAppKitHitTest(RnAppKitView *root, CGFloat x, CGFloat y) {
  if (root == nil || root.hidden) {
    return nil;
  }

  // The point arrives relative to this view's visible top-left; child frames
  // are in its *bounds* space. For an unscrolled view those are the same, and
  // for a scrolled one they differ by exactly the offset -- which is what makes
  // hit testing follow the scroll without anything here knowing about
  // ScrollViews.
  const NSRect bounds = root.bounds;
  const CGFloat bx = x + bounds.origin.x;
  const CGFloat by = y + bounds.origin.y;

  if (!NSPointInRect(NSMakePoint(bx, by), bounds)) {
    return nil;
  }

  for (NSView *child in root.subviews.reverseObjectEnumerator) {
    if (![child isKindOfClass:[RnAppKitView class]]) {
      continue;
    }
    const NSRect frame = child.frame;
    RnAppKitView *hit = RnAppKitHitTest((RnAppKitView *)child,
                                        bx - frame.origin.x,
                                        by - frame.origin.y);
    if (hit != nil) {
      return hit;
    }
  }
  return root;
}

// A rounded rectangle with a different radius pair at each corner.
//
// CGPathAddArcToPoint only draws circular arcs, and React Native's radii are
// elliptical, so the corners are cubic Beziers. kappa is the usual constant
// for approximating a quarter ellipse with one curve -- the error is under a
// thousandth of the radius, which is far below a pixel at any radius an
// interface uses.
static CGPathRef RnAppKitCreateRoundedPath(CGRect rect, const CGFloat radii[8]) {
  static const CGFloat kappa = (CGFloat)0.5522847498307936;
  const CGFloat x = CGRectGetMinX(rect);
  const CGFloat y = CGRectGetMinY(rect);
  const CGFloat w = CGRectGetWidth(rect);
  const CGFloat h = CGRectGetHeight(rect);

  // Clamp so that two radii along one edge can never exceed it. The values
  // arriving from Fabric are already clamped, but this path is also built for
  // the *inner* edge of a border, whose radii are reduced by the border widths
  // and can be anything.
  CGFloat r[8];
  for (int i = 0; i < 8; i++) {
    r[i] = radii[i] > 0 ? radii[i] : 0;
  }
  const CGFloat tlw = r[0], tlh = r[1], trw = r[2], trh = r[3];
  const CGFloat brw = r[4], brh = r[5], blw = r[6], blh = r[7];

  CGMutablePathRef path = CGPathCreateMutable();
  CGPathMoveToPoint(path, NULL, x + tlw, y);
  CGPathAddLineToPoint(path, NULL, x + w - trw, y);
  CGPathAddCurveToPoint(path, NULL,
                        x + w - trw + trw * kappa, y,
                        x + w, y + trh - trh * kappa,
                        x + w, y + trh);
  CGPathAddLineToPoint(path, NULL, x + w, y + h - brh);
  CGPathAddCurveToPoint(path, NULL,
                        x + w, y + h - brh + brh * kappa,
                        x + w - brw + brw * kappa, y + h,
                        x + w - brw, y + h);
  CGPathAddLineToPoint(path, NULL, x + blw, y + h);
  CGPathAddCurveToPoint(path, NULL,
                        x + blw - blw * kappa, y + h,
                        x, y + h - blh + blh * kappa,
                        x, y + h - blh);
  CGPathAddLineToPoint(path, NULL, x, y + tlh);
  CGPathAddCurveToPoint(path, NULL,
                        x, y + tlh - tlh * kappa,
                        x + tlw - tlw * kappa, y,
                        x + tlw, y);
  CGPathCloseSubpath(path);
  return path;
}

// Clips to the side of the line through `p` with direction `dir` that contains
// `inside`. Two of these give an edge the sector it owns at a corner.
//
// A half-plane rather than a polygon because a sector has no far end: the
// border band at a rounded corner runs as far out as the radius takes it, and
// any bounded shape has to guess how far that is.
static void RnAppKitClipToHalfPlane(CGContextRef context,
                                    CGPoint p,
                                    CGPoint dir,
                                    CGPoint inside,
                                    CGFloat extent) {
  const CGFloat length = std::hypot(dir.x, dir.y);
  if (length < (CGFloat)1e-6) {
    // Both borders meeting here are zero wide, so there is nothing to divide.
    return;
  }
  const CGPoint along = {dir.x / length, dir.y / length};
  const CGPoint normal = {-along.y, along.x};
  const CGFloat side =
      (inside.x - p.x) * normal.x + (inside.y - p.y) * normal.y;
  const CGFloat sign = side < 0 ? -1 : 1;

  const CGPoint a = {p.x + along.x * extent, p.y + along.y * extent};
  const CGPoint b = {p.x - along.x * extent, p.y - along.y * extent};
  const CGPoint c = {b.x + normal.x * sign * extent, b.y + normal.y * sign * extent};
  const CGPoint d = {a.x + normal.x * sign * extent, a.y + normal.y * sign * extent};

  CGContextBeginPath(context);
  CGContextMoveToPoint(context, a.x, a.y);
  CGContextAddLineToPoint(context, b.x, b.y);
  CGContextAddLineToPoint(context, c.x, c.y);
  CGContextAddLineToPoint(context, d.x, d.y);
  CGContextClosePath(context);
  CGContextClip(context);
}

@implementation RnAppKitView {
  NSString *_roleName;
  RnTextLayout *_textLayout;
  CGImageRef _image;
  RnAppKitImageFit _imageFit;
  BOOL _hasBackgroundColor;
  CGFloat _backgroundComponents[4];
  CGFloat _opacity;
  BOOL _clipsChildren;
  CGFloat _cornerRadius;
  // Eight floats: a horizontal and a vertical radius per corner, in the order
  // top-left, top-right, bottom-right, bottom-left. React Native clamps
  // opposite corners against the frame before these arrive, so nothing here
  // has to.
  CGFloat _borderRadii[8];
  BOOL _hasBorderRadii;
  // top, right, bottom, left -- the order CSS names them.
  CGFloat _borderWidths[4];
  // Four RGBA quadruples, in the same edge order.
  CGFloat _borderColors[16];
  BOOL _hasBorders;
  CATransform3D _transform;
  BOOL _hasTransform;
}

+ (instancetype)viewWithTag:(NSInteger)tag {
  RnAppKitView *view = [[RnAppKitView alloc] initWithFrame:NSZeroRect];
  view->_rnTag = tag;
  return view;
}

- (void)dealloc {
  CGImageRelease(_image);
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    _opacity = 1.0;
    _imageFit = RnAppKitImageFitCover;
    _transform = CATransform3DIdentity;
    // Layer-backed from the start rather than on demand: a view that acquires a
    // layer later loses whatever was set on it before, and the props arrive in
    // whatever order the mutation stream happens to carry them.
    self.wantsLayer = YES;
    // Frames come from Fabric already resolved. Autoresizing would fight them.
    self.autoresizingMask = NSViewNotSizable;
    self.translatesAutoresizingMaskIntoConstraints = YES;
  }
  return self;
}

// See the note in the header: React Native's coordinates are top-left.
- (BOOL)isFlipped {
  return YES;
}

- (void)setRnFrameX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
  self.frame = NSMakeRect(x, y, width, height);
  // A mask layer is built against the bounds, so a resize invalidates it. The
  // uniform case is a plain cornerRadius and needs nothing.
  if (self.layer.mask != nil) {
    [self rnUpdateRadiusMask];
  }
  // Borders are drawn along the bounds too.
  if (_hasBorders) {
    self.needsDisplay = YES;
  }
}

- (void)setRnBackgroundColorRed:(CGFloat)red
                          green:(CGFloat)green
                           blue:(CGFloat)blue
                          alpha:(CGFloat)alpha
                       hasColor:(BOOL)hasColor {
  _hasBackgroundColor = hasColor;
  _backgroundComponents[0] = red;
  _backgroundComponents[1] = green;
  _backgroundComponents[2] = blue;
  _backgroundComponents[3] = alpha;

  if (!hasColor) {
    self.layer.backgroundColor = nil;
    return;
  }
  // sRGB explicitly. React Native's colour components are sRGB, and letting
  // CoreGraphics pick a device space shifts every colour slightly on a
  // wide-gamut display -- visible next to the same app on another platform,
  // which is exactly what this project is trying not to be.
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const CGFloat components[4] = {red, green, blue, alpha};
  CGColorRef color = CGColorCreate(space, components);
  self.layer.backgroundColor = color;
  CGColorRelease(color);
  CGColorSpaceRelease(space);
}

// --- Accessibility ---------------------------------------------------------

// React Native's role vocabulary is mostly ARIA's; AppKit's is its own, older
// and smaller. Several entries below are the closest thing rather than an
// equivalent, and each says so -- an approximate role is better than none, and
// much better than a wrong one, which makes a control announce itself as
// something it is not.
static NSAccessibilityRole RnAccessibilityRoleFor(NSString *name) {
  static NSDictionary<NSString *, NSAccessibilityRole> *roles = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    roles = @{
      @"button" : NSAccessibilityButtonRole,
      @"imagebutton" : NSAccessibilityButtonRole,
      @"link" : NSAccessibilityLinkRole,
      // A search field is a text field with a subrole on macOS, not a role of
      // its own. Reporting the role loses the "this searches" part, which is
      // what NSAccessibilitySearchFieldSubrole would carry -- a subrole needs a
      // second property, and this does not set one yet.
      @"search" : NSAccessibilityTextFieldRole,
      @"image" : NSAccessibilityImageRole,
      @"text" : NSAccessibilityStaticTextRole,
      @"adjustable" : NSAccessibilitySliderRole,
      @"checkbox" : NSAccessibilityCheckBoxRole,
      @"combobox" : NSAccessibilityComboBoxRole,
      @"menu" : NSAccessibilityMenuRole,
      @"menubar" : NSAccessibilityMenuBarRole,
      @"menuitem" : NSAccessibilityMenuItemRole,
      @"progressbar" : NSAccessibilityProgressIndicatorRole,
      @"radio" : NSAccessibilityRadioButtonRole,
      @"radiogroup" : NSAccessibilityRadioGroupRole,
      @"scrollbar" : NSAccessibilityScrollBarRole,
      @"spinbutton" : NSAccessibilityIncrementorRole,
      @"list" : NSAccessibilityListRole,
      @"grid" : NSAccessibilityTableRole,
      @"toolbar" : NSAccessibilityToolbarRole,
      @"tooltip" : NSAccessibilityHelpTagRole,
      // macOS has no toggle-button or switch role: VoiceOver announces both as
      // checkboxes, which is also what AppKit's own NSSwitch reports.
      @"togglebutton" : NSAccessibilityCheckBoxRole,
      @"switch" : NSAccessibilityCheckBoxRole,
      // A tab in an AX tab group is a radio button, which reads oddly and is
      // what every Mac application does.
      @"tab" : NSAccessibilityRadioButtonRole,
      @"tablist" : NSAccessibilityTabGroupRole,
      // No standalone header or alert role for a view. A group at least says
      // "these belong together", which a static text would not.
      @"header" : NSAccessibilityGroupRole,
      @"alert" : NSAccessibilityGroupRole,
      // Explicitly nothing: `none` and `presentation` mean "do not announce
      // this", which is handled by isAccessibilityElement below.
      @"none" : NSAccessibilityUnknownRole,
      @"presentation" : NSAccessibilityUnknownRole,
    };
  });
  NSAccessibilityRole role = roles[name];
  // Anything unrecognised is a group rather than a guess: a wrong role is worse
  // for a screen reader than a vague one, because it makes the view announce
  // itself as something it is not.
  return role != nil ? role : NSAccessibilityGroupRole;
}

- (void)setRnAccessibleRole:(NSString *)role {
  _roleName = role.length > 0 ? [role copy] : nil;

  if (_roleName == nil) {
    self.accessibilityRole = NSAccessibilityGroupRole;
    // A plain <View> is scenery. Leaving every one of them in the tree would
    // bury the handful that mean something under hundreds that do not.
    self.accessibilityElement = NO;
    return;
  }

  const NSAccessibilityRole mapped = RnAccessibilityRoleFor(_roleName);
  self.accessibilityRole = mapped;
  self.accessibilityElement = mapped != NSAccessibilityUnknownRole;
}

- (void)setRnAccessibleLabel:(NSString *)label hint:(NSString *)hint {
  self.accessibilityLabel = label.length > 0 ? label : nil;
  // `accessibilityHelp`, not `accessibilityValue`: React Native's hint is the
  // supplementary description, which on a Mac is what a help tag carries.
  self.accessibilityHelp = hint.length > 0 ? hint : nil;

  // A label makes a view worth announcing even when it has no role -- which is
  // the common case for an icon-only <Pressable> with an accessibilityLabel.
  if (label.length > 0) {
    self.accessibilityElement = YES;
  }
}

- (void)setRnAccessibleStateDisabled:(RnAppKitAccessibleFlag)disabled
                             checked:(RnAppKitAccessibleFlag)checked
                            selected:(RnAppKitAccessibleFlag)selected
                            expanded:(RnAppKitAccessibleFlag)expanded
                                busy:(RnAppKitAccessibleFlag)busy {
  if (disabled != RnAppKitAccessibleUnset) {
    self.accessibilityEnabled = disabled != RnAppKitAccessibleTrue;
  }
  if (checked != RnAppKitAccessibleUnset) {
    // AppKit expresses checked as the element's value, the way a checkbox does,
    // rather than as a state of its own.
    self.accessibilityValue = @(checked == RnAppKitAccessibleTrue);
  }
  if (selected != RnAppKitAccessibleUnset) {
    self.accessibilitySelected = selected == RnAppKitAccessibleTrue;
  }
  if (expanded != RnAppKitAccessibleUnset) {
    self.accessibilityExpanded = expanded == RnAppKitAccessibleTrue;
  }
  // `busy` has no AppKit equivalent. React Native's meaning is "this is
  // loading", which VoiceOver has no way to be told about a plain view; the
  // nearest thing is an NSAccessibilityProgressIndicator, which would be a
  // different role rather than a state. Left unreported rather than mapped onto
  // something that means something else.
  (void)busy;
}

- (void)setRnAccessibleHidden:(BOOL)hidden {
  if (hidden) {
    self.accessibilityElement = NO;
  }
  // Not `else { YES }`: whether an unhidden view is an element is decided by
  // its role and label above, and overriding that here would put every plain
  // <View> back into the tree.
}

- (void)setRnScrollOffsetX:(CGFloat)x y:(CGFloat)y {
  const NSRect bounds = self.bounds;
  if (bounds.origin.x == x && bounds.origin.y == y) {
    return;
  }
  [self setBoundsOrigin:NSMakePoint(x, y)];
  // setBoundsOrigin: moves subviews but does not repaint what this view draws
  // itself, which matters the moment a ScrollView has a background or text.
  self.needsDisplay = YES;
}

- (NSPoint)rnScrollOffset {
  return self.bounds.origin;
}

static const char *RnAppKitImageFitName(RnAppKitImageFit fit) {
  switch (fit) {
    case RnAppKitImageFitContain:
      return "contain";
    case RnAppKitImageFitStretch:
      return "stretch";
    case RnAppKitImageFitCenter:
      return "center";
    case RnAppKitImageFitCover:
      break;
  }
  return "cover";
}

- (void)setRnImage:(CGImageRef)image fit:(RnAppKitImageFit)fit {
  if (_image == image && _imageFit == fit) {
    return;
  }
  // Retained, not borrowed: the loader's cache owns one reference and this owns
  // another, so a view outliving an eviction still has pixels to draw.
  CGImageRef previous = _image;
  _image = image != nullptr ? CGImageRetain(image) : nullptr;
  CGImageRelease(previous);
  _imageFit = fit;
  self.needsDisplay = YES;
}

// Where the image lands inside the frame.
//
// The same arithmetic as the GTK side's, deliberately -- two platforms
// disagreeing about what `resizeMode: 'contain'` means would be the sort of
// difference nobody thinks to check. Duplicated rather than shared because the
// view layers link no common code at all, which is what keeps them testable
// without React Native.
- (NSRect)rnImageRectForSize:(NSSize)size {
  const CGFloat imageWidth = (CGFloat)CGImageGetWidth(_image);
  const CGFloat imageHeight = (CGFloat)CGImageGetHeight(_image);
  if (_imageFit == RnAppKitImageFitStretch || imageWidth <= 0 || imageHeight <= 0) {
    return NSMakeRect(0, 0, size.width, size.height);
  }

  CGFloat scale = 1.0;
  switch (_imageFit) {
    case RnAppKitImageFitContain:
      scale = MIN(size.width / imageWidth, size.height / imageHeight);
      break;
    case RnAppKitImageFitCover:
      scale = MAX(size.width / imageWidth, size.height / imageHeight);
      break;
    case RnAppKitImageFitCenter:
      // Centre at natural size, but never larger than the frame -- which is
      // what React Native's `center` does.
      scale = MIN(1.0, MIN(size.width / imageWidth, size.height / imageHeight));
      break;
    case RnAppKitImageFitStretch:
      break;
  }

  const CGFloat width = imageWidth * scale;
  const CGFloat height = imageHeight * scale;
  return NSMakeRect((size.width - width) / 2.0, (size.height - height) / 2.0, width, height);
}

- (void)setRnTextLayout:(id)layout {
  _textLayout = (RnTextLayout *)layout;
  // A layer-backed view with a drawRect: gets its contents from that draw, and
  // only redraws when told. Without this the first paragraph appears and no
  // later one ever does.
  self.needsDisplay = YES;
}

// Only views with text draw anything; the rest are pure CALayer properties and
// this is never called for them.
//
// The background is *not* drawn here. It is a layer property, which means Core
// Animation paints it under this and a view can have both without either
// knowing about the other.
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  if (_textLayout == nil && _image == nullptr && !_hasBorders) {
    return;
  }
  CGContextRef context = [NSGraphicsContext currentContext].CGContext;
  const NSSize size = self.bounds.size;

  if (_image != nullptr) {
    const NSRect destination = [self rnImageRectForSize:size];

    CGContextSaveGState(context);
    // cover and center can put pixels outside the frame, and an <Image> never
    // paints beyond its own box on iOS or Android.
    if (_imageFit == RnAppKitImageFitCover || _imageFit == RnAppKitImageFitCenter) {
      CGContextClipToRect(context, NSMakeRect(0, 0, size.width, size.height));
    }
    // CGImage draws bottom-up and this view is flipped, so without this every
    // photograph comes out upside down -- which reads as a broken decoder
    // rather than a coordinate system.
    CGContextTranslateCTM(context, 0, size.height);
    CGContextScaleCTM(context, 1, -1);
    CGContextDrawImage(
        context,
        NSMakeRect(destination.origin.x,
                   size.height - destination.origin.y - destination.size.height,
                   destination.size.width,
                   destination.size.height),
        _image);
    CGContextRestoreGState(context);
  }

  // Text sits above the image and below any children, which is the order the
  // GTK side paints in too.
  if (_textLayout != nil) {
    [_textLayout drawInContext:context size:size];
  }

  // Borders paint over the content, as they do on every other platform.
  if (_hasBorders) {
    [self rnDrawBordersInContext:context size:size];
  }
}

// The four edges, each its own width and colour.
//
// This is CSS's own algorithm rather than a stroke: an edge is the region
// between the outer and inner rounded rectangles, clipped to a wedge whose
// sides are the diagonals from the outer corner to the inner one. That is what
// mitres two edges of different colours against each other, and a stroke
// cannot do it -- which matters as soon as one edge differs, and is invisible
// until then.
- (void)rnDrawBordersInContext:(CGContextRef)context size:(NSSize)size {
  const CGFloat w = size.width;
  const CGFloat h = size.height;
  const CGFloat top = _borderWidths[0];
  const CGFloat right = _borderWidths[1];
  const CGFloat bottom = _borderWidths[2];
  const CGFloat left = _borderWidths[3];

  CGPathRef outer = RnAppKitCreateRoundedPath(CGRectMake(0, 0, w, h), _borderRadii);

  // The inner radii shrink by the width of the edges that meet at the corner.
  // A corner whose radius is smaller than its border is square on the inside,
  // which the clamp in the path builder takes care of.
  const CGFloat innerRadii[8] = {
      _borderRadii[0] - left, _borderRadii[1] - top,
      _borderRadii[2] - right, _borderRadii[3] - top,
      _borderRadii[4] - right, _borderRadii[5] - bottom,
      _borderRadii[6] - left, _borderRadii[7] - bottom,
  };
  const CGFloat innerWidth = w - left - right;
  const CGFloat innerHeight = h - top - bottom;
  CGPathRef inner = nullptr;
  if (innerWidth > 0 && innerHeight > 0) {
    inner = RnAppKitCreateRoundedPath(
        CGRectMake(left, top, innerWidth, innerHeight), innerRadii);
  }

  // The diagonal at each corner, as a point and a direction, and a point known
  // to be inside each edge's own band.
  //
  // Deliberately not four quadrilaterals stopping at the inner rectangle. That
  // is right only while the corners are square: a radius pushes the border band
  // outside those quads, and the part of the arc beyond them is then inside no
  // wedge at all and never painted. What that looks like is not a missing
  // sliver -- it is a corner whose colour stops early, which reads as a chamfer
  // rather than as a bug.
  //
  // Two half-plane clips per edge instead. Successive clips intersect, so the
  // pair is the sector the edge owns, and a sector is unbounded: it covers the
  // whole arc however large the radius. It also stays correct when the two
  // diagonals cross inside the view, which a quadrilateral cannot -- that
  // happens whenever the border is thick relative to the frame, and it turns a
  // quad into a bowtie.
  const CGPoint corners[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
  const CGPoint directions[4] = {
      {left, top}, {-right, top}, {-right, -bottom}, {left, -bottom}};
  // Edge e is bounded by the diagonals at corner e and corner e+1.
  const CGPoint insides[4] = {
      {w / 2, top / 2},
      {w - right / 2, h / 2},
      {w / 2, h - bottom / 2},
      {left / 2, h / 2},
  };
  const CGFloat extent = (w + h) * 4;

  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  for (int edge = 0; edge < 4; edge++) {
    if (_borderWidths[edge] <= 0 || _borderColors[edge * 4 + 3] <= 0) {
      continue;
    }
    CGContextSaveGState(context);

    RnAppKitClipToHalfPlane(context, corners[edge], directions[edge], insides[edge], extent);
    RnAppKitClipToHalfPlane(
        context, corners[(edge + 1) % 4], directions[(edge + 1) % 4], insides[edge], extent);

    // Outer minus inner, as an even-odd fill of the two subpaths.
    CGContextBeginPath(context);
    CGContextAddPath(context, outer);
    if (inner != nullptr) {
      CGContextAddPath(context, inner);
    }
    const CGFloat components[4] = {
        _borderColors[edge * 4 + 0], _borderColors[edge * 4 + 1],
        _borderColors[edge * 4 + 2], _borderColors[edge * 4 + 3]};
    CGColorRef color = CGColorCreate(space, components);
    CGContextSetFillColorWithColor(context, color);
    CGContextEOFillPath(context);
    CGColorRelease(color);

    CGContextRestoreGState(context);
  }
  CGColorSpaceRelease(space);
  CGPathRelease(outer);
  if (inner != nullptr) {
    CGPathRelease(inner);
  }
}

- (void)setRnOpacity:(CGFloat)opacity {
  _opacity = opacity;
  self.layer.opacity = (float)opacity;
}

- (void)setRnTransform:(nullable const float *)matrix {
  if (matrix == nullptr) {
    _hasTransform = NO;
    _transform = CATransform3DIdentity;
    self.layer.transform = CATransform3DIdentity;
    return;
  }

  // Field by field rather than a memcpy: CATransform3D's members are CGFloat,
  // which is not guaranteed to be double, and the layout is only documented as
  // "m11 through m44 in this order".
  CATransform3D transform;
  CGFloat *fields = (CGFloat *)&transform;
  for (int index = 0; index < 16; index++) {
    fields[index] = (CGFloat)matrix[index];
  }

  _transform = transform;
  _hasTransform = !CATransform3DIsIdentity(transform);
  self.layer.transform = transform;
}

- (void)setRnClipsChildren:(BOOL)clips {
  _clipsChildren = clips;
  self.layer.masksToBounds = clips;
}

- (void)setRnCornerRadius:(CGFloat)radius {
  const CGFloat radii[8] = {radius, radius, radius, radius,
                            radius, radius, radius, radius};
  [self setRnBorderRadii:radii];
}

- (void)setRnBorderRadii:(nullable const CGFloat *)radii {
  BOOL any = NO;
  for (int i = 0; i < 8; i++) {
    _borderRadii[i] = radii != nullptr && radii[i] > 0 ? radii[i] : 0;
    if (_borderRadii[i] > 0) {
      any = YES;
    }
  }
  _hasBorderRadii = any;

  // A single circular radius is what CALayer can express directly, and is the
  // overwhelmingly common case -- one `borderRadius` in a stylesheet. Keeping
  // it on cornerRadius rather than on a mask means the usual view stays a
  // plain layer, and means `overflow: 'visible'` still lets children escape.
  BOOL uniform = YES;
  for (int i = 1; i < 8; i++) {
    if (_borderRadii[i] != _borderRadii[0]) {
      uniform = NO;
      break;
    }
  }

  if (uniform) {
    _cornerRadius = _borderRadii[0];
    self.layer.cornerRadius = _borderRadii[0];
    self.layer.mask = nil;
  } else {
    // Anything else needs a mask layer, which is what UIKit's own RCTView does
    // for the same reason. The cost is that a mask clips children whatever
    // `overflow` says -- CALayer offers no way to round a background without
    // clipping what sits on it.
    _cornerRadius = 0;
    self.layer.cornerRadius = 0;
    [self rnUpdateRadiusMask];
  }
  // The border is drawn along these radii.
  self.needsDisplay = YES;
}

- (void)rnUpdateRadiusMask {
  if (!_hasBorderRadii) {
    self.layer.mask = nil;
    return;
  }
  CAShapeLayer *mask = [CAShapeLayer layer];
  mask.frame = self.bounds;
  CGPathRef path = RnAppKitCreateRoundedPath(self.bounds, _borderRadii);
  mask.path = path;
  CGPathRelease(path);
  self.layer.mask = mask;
}

- (void)setRnBorderWidths:(nullable const CGFloat *)widths
                   colors:(nullable const CGFloat *)colors {
  BOOL any = NO;
  for (int edge = 0; edge < 4; edge++) {
    _borderWidths[edge] = widths != nullptr && widths[edge] > 0 ? widths[edge] : 0;
    for (int c = 0; c < 4; c++) {
      _borderColors[edge * 4 + c] = colors != nullptr ? colors[edge * 4 + c] : 0;
    }
    // A width with a transparent colour paints nothing, and the GTK side makes
    // the same judgement, so the two agree on whether a view has a border at
    // all rather than only on what it looks like.
    if (_borderWidths[edge] > 0 && _borderColors[edge * 4 + 3] > 0) {
      any = YES;
    }
  }
  _hasBorders = any;
  self.needsDisplay = YES;
}

- (void)insertRnChild:(RnAppKitView *)child atIndex:(NSInteger)index {
  NSArray<NSView *> *children = self.subviews;
  if (index >= (NSInteger)children.count) {
    [self addSubview:child];
    return;
  }
  [self addSubview:child positioned:NSWindowBelow relativeTo:children[(NSUInteger)index]];
}

- (void)removeRnChild:(RnAppKitView *)child {
  // Removed, not destroyed: a Delete mutation for the same tag follows
  // separately, and until then the view may be inserted somewhere else.
  if (child.superview == self) {
    [child removeFromSuperview];
  }
}

- (void)describeInto:(NSMutableString *)out depth:(NSInteger)depth {
  for (NSInteger i = 0; i < depth; i++) {
    [out appendString:@"  "];
  }
  const NSRect frame = self.frame;
  [out appendFormat:@"view tag=%ld frame=(%g,%g %gx%g)",
                    (long)self.rnTag,
                    frame.origin.x,
                    frame.origin.y,
                    frame.size.width,
                    frame.size.height];

  if (_hasBackgroundColor) {
    [out appendFormat:@" bg=#%02x%02x%02x%02x",
                      (unsigned)(_backgroundComponents[0] * 255.0 + 0.5),
                      (unsigned)(_backgroundComponents[1] * 255.0 + 0.5),
                      (unsigned)(_backgroundComponents[2] * 255.0 + 0.5),
                      (unsigned)(_backgroundComponents[3] * 255.0 + 0.5)];
  }
  // Field order matches the GTK side exactly -- bg, opacity, clip, then text --
  // because scripts/compare_hosts.sh diffs the two dumps line by line and a
  // reordering would read as every line differing.
  if (_opacity < 1.0) {
    [out appendFormat:@" opacity=%g", _opacity];
  }
  if (_clipsChildren) {
    [out appendString:@" clip"];
  }
  // Per-corner radii and per-edge borders, in the same fields and the same
  // order the GTK and Win32 sides print. They are here for the same reason
  // `transform=` is, and the comment there names the precedent: a frame cannot
  // show a border, so a bordered view and a bare one read as identical in this
  // dump -- which is how this host went until phase 47 applying neither.
  if (_hasBorderRadii) {
    [out appendFormat:@" radii=(%g,%g,%g,%g,%g,%g,%g,%g)",
                      (double)_borderRadii[0], (double)_borderRadii[1],
                      (double)_borderRadii[2], (double)_borderRadii[3],
                      (double)_borderRadii[4], (double)_borderRadii[5],
                      (double)_borderRadii[6], (double)_borderRadii[7]];
  }
  if (_hasBorders) {
    [out appendFormat:@" borderw=(%g,%g,%g,%g)",
                      (double)_borderWidths[0], (double)_borderWidths[1],
                      (double)_borderWidths[2], (double)_borderWidths[3]];
    [out appendString:@" borderc=("];
    for (int edge = 0; edge < 4; edge++) {
      [out appendFormat:@"%s#%02x%02x%02x%02x",
                        edge == 0 ? "" : ",",
                        (unsigned)(_borderColors[edge * 4 + 0] * 255.0 + 0.5),
                        (unsigned)(_borderColors[edge * 4 + 1] * 255.0 + 0.5),
                        (unsigned)(_borderColors[edge * 4 + 2] * 255.0 + 0.5),
                        (unsigned)(_borderColors[edge * 4 + 3] * 255.0 + 0.5)];
    }
    [out appendString:@")"];
  }
  if (_hasTransform) {
    // The 2D affine part, which is all either platform draws, in the order
    // CSS writes a matrix(): a, b, c, d, tx, ty. The GTK side prints the same
    // six from its own matrix, so a transform is comparable across the two --
    // without which a view that is rotated on one desktop and not on the other
    // looks identical in this dump. Which is exactly how the macOS host went
    // this long without applying transforms at all.
    [out appendFormat:@" transform=(%g,%g,%g,%g,%g,%g)",
                      (double)_transform.m11,
                      (double)_transform.m12,
                      (double)_transform.m21,
                      (double)_transform.m22,
                      (double)_transform.m41,
                      (double)_transform.m42];
  }
  const NSPoint scroll = self.bounds.origin;
  if (scroll.x != 0 || scroll.y != 0) {
    [out appendFormat:@" scroll=(%g,%g)", scroll.x, scroll.y];
  }
  if (_image != nullptr) {
    // The same `texture=WxH fit=<name>` the GTK side emits, so an <Image> shows
    // up in the cross-platform diff and the end-to-end suite can assert on it.
    // The fit is here because it is the only thing about a drawn image that a
    // frame cannot show: two views the same size holding the same picture are
    // identical in every other line of this dump and different on screen.
    [out appendFormat:@" texture=%zux%zu", CGImageGetWidth(_image), CGImageGetHeight(_image)];
    [out appendFormat:@" fit=%s", RnAppKitImageFitName(_imageFit)];
  }
  if (_textLayout != nil) {
    NSString *text = _textLayout.attributedString.string;
    if (text.length > 0) {
      // Escaped like g_strescape's output on the other side, so a string with a
      // quote or a newline in it stays one line and stays comparable.
      NSMutableString *escaped = [text mutableCopy];
      [escaped replaceOccurrencesOfString:@"\\" withString:@"\\\\"
                                  options:0 range:NSMakeRange(0, escaped.length)];
      [escaped replaceOccurrencesOfString:@"\"" withString:@"\\\""
                                  options:0 range:NSMakeRange(0, escaped.length)];
      [escaped replaceOccurrencesOfString:@"\n" withString:@"\\n"
                                  options:0 range:NSMakeRange(0, escaped.length)];
      [out appendFormat:@" text=\"%@\"", escaped];
    }
  }

  // A text field's content lives in its NSTextField peer, not in anything this
  // view draws, so it would otherwise be invisible to every test that reads
  // this tree. Same spelling as the GTK side's.
  if (self.rnEditable != nil) {
    NSString *value = self.rnEditable.stringValue ?: @"";
    NSMutableString *escaped = [value mutableCopy];
    [escaped replaceOccurrencesOfString:@"\\" withString:@"\\\\"
                                options:0 range:NSMakeRange(0, escaped.length)];
    [escaped replaceOccurrencesOfString:@"\"" withString:@"\\\""
                                options:0 range:NSMakeRange(0, escaped.length)];
    [out appendFormat:@" editable=\"%@\"", escaped];

    // First responder is the field's *field editor* while it is being edited,
    // not the field itself, so asking the window whether it is editing this one
    // is the question that actually has the right answer.
    NSWindow *window = self.window;
    const BOOL focused = window != nil &&
        (window.firstResponder == self.rnEditable ||
         ([window.firstResponder isKindOfClass:[NSTextView class]] &&
          ((NSTextView *)window.firstResponder).delegate == (id)self.rnEditable));
    if (focused) {
      [out appendString:@" focused"];
    }
  }

  // React Native's role name, not AppKit's. The GTK side reports the same
  // string for the same reason: this dump is compared line by line across two
  // platforms, and each reporting its own toolkit's vocabulary would make every
  // accessible view look like a difference. That the *platform* role was really
  // applied is asserted in each platform's unit tests instead.
  if (_roleName != nil) {
    [out appendFormat:@" role=%@", _roleName];
  }
  [out appendString:@"\n"];

  for (NSView *child in self.subviews) {
    if ([child isKindOfClass:[RnAppKitView class]]) {
      [(RnAppKitView *)child describeInto:out depth:depth + 1];
    }
  }
}

// --- Input ----------------------------------------------------------------
//
// AppKit delivers a mouse event to the view its own hit testing picked, which
// is some view in this tree; React Native wants it against the surface root,
// hit-tested React Native's way. So every view forwards, and the root is where
// the forwarding stops.
//
// Not overriding hitTest: to make the root swallow everything instead: that
// would also swallow the cursor rectangles, tooltips and tracking areas any
// later component needs, and AppKit's own hit testing is doing no harm here.

- (nullable id<RnAppKitInputHandler>)rnHandler:(NSView **)outRoot {
  NSView *view = self;
  while (view != nil) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      id<RnAppKitInputHandler> handler = ((RnAppKitView *)view).rnInputHandler;
      if (handler != nil) {
        *outRoot = view;
        return handler;
      }
    }
    view = view.superview;
  }
  return nil;
}

- (void)forwardMouse:(NSEvent *)event kind:(char)kind {
  NSView *root = nil;
  id<RnAppKitInputHandler> handler = [self rnHandler:&root];
  if (handler == nil) {
    return;
  }
  const NSPoint point = [root convertPoint:event.locationInWindow fromView:nil];
  switch (kind) {
    case 'd': [handler rnMouseDownAt:point]; break;
    case 'm': [handler rnMouseDraggedTo:point]; break;
    case 'u': [handler rnMouseUpAt:point]; break;
    default: break;
  }
}

// Wheel and touchpad. Unhandled scrolls go to super, which walks the responder
// chain -- so a wheel over a plain view inside a list reaches the list.
- (void)scrollWheel:(NSEvent *)event {
  id<RnAppKitScrollHandler> handler = self.rnScrollHandler;
  if (handler != nil) {
    const NSPoint delta = NSMakePoint(event.scrollingDeltaX, event.scrollingDeltaY);
    // `hasPreciseScrollingDeltas` is the only thing that can tell a touchpad's
    // pixels from a wheel's line counts -- both arrive as small numbers, so the
    // deltas alone cannot. Passed on rather than resolved here: how many pixels
    // a line is worth is a cross-platform decision, and it is made next to the
    // GTK side's copy of it.
    if ([handler rnScrollView:self
                           by:delta
                      precise:event.hasPreciseScrollingDeltas
                        phase:event.phase]) {
      return;
    }
  }
  [super scrollWheel:event];
}

- (void)mouseDown:(NSEvent *)event {
  [self forwardMouse:event kind:'d'];
}

- (void)mouseDragged:(NSEvent *)event {
  [self forwardMouse:event kind:'m'];
}

- (void)mouseUp:(NSEvent *)event {
  [self forwardMouse:event kind:'u'];
}

// Every button, because React Native's touch model has no concept of which one
// -- that belongs to pointer events. The same choice the GTK side makes by
// setting its click gesture to button 0.
- (void)rightMouseDown:(NSEvent *)event {
  [self forwardMouse:event kind:'d'];
}

- (void)rightMouseDragged:(NSEvent *)event {
  [self forwardMouse:event kind:'m'];
}

- (void)rightMouseUp:(NSEvent *)event {
  [self forwardMouse:event kind:'u'];
}

- (NSString *)describeTree {
  NSMutableString *out = [NSMutableString string];
  [self describeInto:out depth:0];
  return out;
}

@end
