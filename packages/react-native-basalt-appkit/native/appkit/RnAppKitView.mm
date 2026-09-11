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
  if (!NSPointInRect(NSMakePoint(x, y), root.bounds)) {
    return nil;
  }

  for (NSView *child in root.subviews.reverseObjectEnumerator) {
    if (![child isKindOfClass:[RnAppKitView class]]) {
      continue;
    }
    const NSRect frame = child.frame;
    RnAppKitView *hit = RnAppKitHitTest((RnAppKitView *)child,
                                        x - frame.origin.x,
                                        y - frame.origin.y);
    if (hit != nil) {
      return hit;
    }
  }
  return root;
}

@implementation RnAppKitView {
  RnTextLayout *_textLayout;
  BOOL _hasBackgroundColor;
  CGFloat _backgroundComponents[4];
  CGFloat _opacity;
  BOOL _clipsChildren;
  CGFloat _cornerRadius;
}

+ (instancetype)viewWithTag:(NSInteger)tag {
  RnAppKitView *view = [[RnAppKitView alloc] initWithFrame:NSZeroRect];
  view->_rnTag = tag;
  return view;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    _opacity = 1.0;
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
  if (_textLayout == nil) {
    return;
  }
  CGContextRef context = [NSGraphicsContext currentContext].CGContext;
  [_textLayout drawInContext:context size:self.bounds.size];
}

- (void)setRnOpacity:(CGFloat)opacity {
  _opacity = opacity;
  self.layer.opacity = (float)opacity;
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
