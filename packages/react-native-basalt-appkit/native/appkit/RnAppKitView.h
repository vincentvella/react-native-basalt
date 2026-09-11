// The macOS view layer: an NSView that Fabric's mounting can drive.
//
// Deliberately the same shape as the GTK one in `gtk/RnView.h` -- a view owns a
// tag and an absolute frame, does no layout of its own, and places each child
// at the rect the shadow tree already resolved. Yoga has done the layout by the
// time a mutation arrives, and a second layout system underneath it is the
// thing to avoid, not to add.
//
// Two decisions are worth stating because they are easy to get wrong and hard
// to notice afterwards.
//
// The view is **flipped**. AppKit's default origin is bottom-left; React
// Native's is top-left, and every frame Fabric produces assumes top-left. An
// unflipped view renders a correct-looking layout upside down about its own
// centre, which reads as a mysterious ordering bug rather than a coordinate
// one.
//
// The view is **layer-backed**, and props are layer properties rather than
// drawing code. Background colour, opacity, corner radius, clipping and
// transform all exist on CALayer with the semantics React Native wants, so
// mapping onto them is both less code and closer to what the platform will do
// well. The GTK side has to compose these by hand in a snapshot; here it would
// be work to avoid using them.

#pragma once

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class RnAppKitView;

// Where a view sends the mouse.
//
// Set on the surface root only. A view that has no handler walks up to one that
// does, which is how every view in the tree feeds a single dispatcher without
// anything having to attach one per view -- the same arrangement the GTK side
// gets by putting its controllers on the root.
//
// Points arrive in the handler's own coordinates, which is the surface root's,
// which is what React Native calls the page.
@protocol RnAppKitInputHandler <NSObject>
- (void)rnMouseDownAt:(NSPoint)point;
- (void)rnMouseDraggedTo:(NSPoint)point;
- (void)rnMouseUpAt:(NSPoint)point;
@end

// The deepest view at a point, in `root`'s coordinates, or nil for a miss.
//
// A free function because it is a pure function of the view tree and nothing
// else, which is also what makes it testable: hit testing is the part of input
// most likely to be quietly wrong, and it needs no mouse to exercise. It lives
// here rather than with the dispatcher so that testing it needs no React Native.
RnAppKitView *_Nullable RnAppKitHitTest(RnAppKitView *_Nullable root, CGFloat x, CGFloat y);

@interface RnAppKitView : NSView

// Fabric's tag for this view. Set once, at creation.
@property(nonatomic, readonly) NSInteger rnTag;

+ (instancetype)viewWithTag:(NSInteger)tag;

// The frame the shadow tree resolved, in the parent's coordinates, top-left
// origin. Applied directly: nothing here recomputes it.
- (void)setRnFrameX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;

// Components are premultiplied-free 0..1, as React Native's colour components
// arrive. Passing hasColor:NO means "no background", which is not the same as
// transparent black: a view with no background does not paint at all.
- (void)setRnBackgroundColorRed:(CGFloat)red
                          green:(CGFloat)green
                           blue:(CGFloat)blue
                          alpha:(CGFloat)alpha
                       hasColor:(BOOL)hasColor;

- (void)setRnOpacity:(CGFloat)opacity;

// The paragraph this view draws, or nil for a view that draws none.
//
// A view either paints a layer or draws text; nothing here does both, because
// React Native's <Text> is a Paragraph node with no children of its own. Held
// as an opaque object so this header stays free of Core Text -- see
// RnTextLayout.h for the object, and CoreTextLayout.h for what builds one --
// both the mounting manager and the measurement seam go through that, so the
// size a view draws at is the size Yoga was told.
- (void)setRnTextLayout:(nullable id)layout;
- (void)setRnClipsChildren:(BOOL)clips;
- (void)setRnCornerRadius:(CGFloat)radius;

// Fabric's Insert and Remove carry an index into the parent's child list, so
// that list has to stay in mutation order.
- (void)insertRnChild:(RnAppKitView *)child atIndex:(NSInteger)index;
- (void)removeRnChild:(RnAppKitView *)child;

// The same one-line-per-view format the GTK side produces, so the two can be
// compared and eventually asserted on by the same tests.
// Set on the surface root. See RnAppKitInputHandler.
@property(nonatomic, weak, nullable) id<RnAppKitInputHandler> rnInputHandler;

- (NSString *)describeTree;

@end

NS_ASSUME_NONNULL_END
#endif
