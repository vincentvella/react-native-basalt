#import "RnMacView.h"

#import <QuartzCore/QuartzCore.h>

@implementation RnMacView {
  BOOL _hasBackgroundColor;
  CGFloat _backgroundComponents[4];
  CGFloat _opacity;
  BOOL _clipsChildren;
  CGFloat _cornerRadius;
}

+ (instancetype)viewWithTag:(NSInteger)tag {
  RnMacView *view = [[RnMacView alloc] initWithFrame:NSZeroRect];
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

- (void)insertRnChild:(RnMacView *)child atIndex:(NSInteger)index {
  NSArray<NSView *> *children = self.subviews;
  if (index >= (NSInteger)children.count) {
    [self addSubview:child];
    return;
  }
  [self addSubview:child positioned:NSWindowBelow relativeTo:children[(NSUInteger)index]];
}

- (void)removeRnChild:(RnMacView *)child {
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
  if (_opacity < 1.0) {
    [out appendFormat:@" opacity=%g", _opacity];
  }
  if (_clipsChildren) {
    [out appendString:@" clip"];
  }
  [out appendString:@"\n"];

  for (NSView *child in self.subviews) {
    if ([child isKindOfClass:[RnMacView class]]) {
      [(RnMacView *)child describeInto:out depth:depth + 1];
    }
  }
}

- (NSString *)describeTree {
  NSMutableString *out = [NSMutableString string];
  [self describeInto:out depth:0];
  return out;
}

@end
