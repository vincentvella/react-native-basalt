#import "RnAppKitView.h"

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
  if (_textLayout == nil && _image == nullptr) {
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
  _cornerRadius = radius;
  self.layer.cornerRadius = radius;
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
