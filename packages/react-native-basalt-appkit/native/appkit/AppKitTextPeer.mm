#import "AppKitTextPeer.h"

// Both peers report gaining focus the same way, and neither superclass does it
// for us. See the protocol's header comment for why only the gaining half.
@interface RnAppKitTextField : NSTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@end

@implementation RnAppKitTextField
- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

@interface RnAppKitSecureTextField : NSSecureTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@end

@implementation RnAppKitSecureTextField
- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

@interface RnAppKitTextView : NSTextView
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextPeerOwner> rnOwner;
@end

@implementation RnAppKitTextView
- (BOOL)becomeFirstResponder {
  const BOOL became = [super becomeFirstResponder];
  if (became) {
    [_rnOwner rnFieldDidBecomeFirstResponder:_rnTag];
  }
  return became;
}
@end

NSView *RnPeerNew(BOOL multiline, BOOL secure, NSInteger tag, id owner, id delegate) {
  if (multiline) {
    RnAppKitTextView *view = [[RnAppKitTextView alloc] initWithFrame:NSZeroRect];
    view.rnTag = tag;
    view.rnOwner = owner;
    view.delegate = (id<NSTextViewDelegate>)delegate;

    // The RnAppKitView behind it draws the background, border and corner
    // radius from React Native's style props, so the peer paints none of its
    // own -- the same reason the single-line field is unbezeled.
    view.drawsBackground = NO;
    view.focusRingType = NSFocusRingTypeNone;
    // No insets of its own: the view already places the peer inside the
    // content inset Yoga resolved.
    view.textContainerInset = NSZeroSize;
    view.textContainer.lineFragmentPadding = 0;
    // Wrap rather than run off the side. The measured box is already tall
    // enough for the wrapped text, because the shadow node measures a
    // multiline input against the real constraints.
    view.textContainer.widthTracksTextView = YES;
    view.horizontallyResizable = NO;
    view.verticallyResizable = YES;
    // Plain text: React Native styles the whole field through props, and rich
    // text would let a paste bring its own font in.
    view.richText = NO;
    view.allowsUndo = YES;
    return view;
  }

  NSTextField *field;
  if (secure) {
    RnAppKitSecureTextField *secureField =
        [[RnAppKitSecureTextField alloc] initWithFrame:NSZeroRect];
    secureField.rnTag = tag;
    secureField.rnOwner = owner;
    field = secureField;
  } else {
    RnAppKitTextField *plain = [[RnAppKitTextField alloc] initWithFrame:NSZeroRect];
    plain.rnTag = tag;
    plain.rnOwner = owner;
    field = plain;
  }

  // One line, and clipped rather than wrapped. An NSTextField wraps by default
  // once its text outruns its width, which is not what a single-line
  // <TextInput> does anywhere else -- it scrolls horizontally. Invisible until
  // a field holds more than it can show, which is why it survived this long.
  field.usesSingleLineMode = YES;
  field.lineBreakMode = NSLineBreakByClipping;
  field.cell.scrollable = YES;
  field.cell.wraps = NO;

  field.bordered = NO;
  field.bezeled = NO;
  field.drawsBackground = NO;
  field.focusRingType = NSFocusRingTypeNone;
  field.delegate = (id<NSTextFieldDelegate>)delegate;
  // Return has to reach doCommandBySelector: rather than being swallowed as a
  // "do nothing" action, which is what an NSTextField with no target does.
  field.target = nil;
  field.action = nullptr;
  return field;
}

BOOL RnPeerIsMultiline(NSView *peer) {
  return peer != nil && [peer isKindOfClass:[NSTextView class]];
}

NSString *RnPeerText(NSView *peer) {
  if (peer == nil) {
    return @"";
  }
  if (RnPeerIsMultiline(peer)) {
    NSString *text = ((NSTextView *)peer).string;
    return text != nil ? text : @"";
  }
  NSString *value = ((NSTextField *)peer).stringValue;
  return value != nil ? value : @"";
}

void RnPeerSetText(NSView *peer, NSString *text) {
  if (peer == nil) {
    return;
  }
  NSString *value = text != nil ? text : @"";
  if (RnPeerIsMultiline(peer)) {
    ((NSTextView *)peer).string = value;
    return;
  }
  ((NSTextField *)peer).stringValue = value;
}

NSRange RnPeerSelection(NSView *peer) {
  if (peer == nil) {
    return NSMakeRange(0, 0);
  }
  if (RnPeerIsMultiline(peer)) {
    // An NSTextView is its own editor, so the selection is always available --
    // which is the one place multiline is simpler than single line here.
    return ((NSTextView *)peer).selectedRange;
  }
  NSTextField *field = (NSTextField *)peer;
  if (NSText *editor = field.currentEditor) {
    return editor.selectedRange;
  }
  return NSMakeRange(field.stringValue.length, 0);
}

void RnPeerSetSelection(NSView *peer, NSRange range) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    NSTextView *view = (NSTextView *)peer;
    const NSUInteger length = view.string.length;
    const NSUInteger location = MIN(range.location, length);
    [view setSelectedRange:NSMakeRange(location, MIN(range.length, length - location))];
    return;
  }
  if (NSText *editor = ((NSTextField *)peer).currentEditor) {
    editor.selectedRange = range;
  }
}

void RnPeerSetEditable(NSView *peer, BOOL editable) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    NSTextView *view = (NSTextView *)peer;
    view.editable = editable;
    view.selectable = YES;
    return;
  }
  NSTextField *field = (NSTextField *)peer;
  field.editable = editable;
  field.selectable = YES;
}

void RnPeerSetTextStyle(NSView *peer, NSFont *font, NSColor *colour, NSTextAlignment alignment) {
  if (peer == nil) {
    return;
  }
  if (RnPeerIsMultiline(peer)) {
    NSTextView *view = (NSTextView *)peer;
    if (font != nil) {
      view.font = font;
    }
    if (colour != nil) {
      view.textColor = colour;
    }
    view.alignment = alignment;
    // What typing continues in. Without it the font and colour apply to what
    // is there and the next character reverts to the system default.
    NSMutableDictionary *typing = [NSMutableDictionary dictionary];
    if (font != nil) {
      typing[NSFontAttributeName] = font;
    }
    if (colour != nil) {
      typing[NSForegroundColorAttributeName] = colour;
    }
    view.typingAttributes = typing;
    return;
  }
  NSTextField *field = (NSTextField *)peer;
  if (font != nil) {
    field.font = font;
  }
  if (colour != nil) {
    field.textColor = colour;
  }
  field.alignment = alignment;
}

void RnPeerSetPlaceholder(NSView *peer, NSAttributedString *placeholder) {
  if (peer == nil || RnPeerIsMultiline(peer)) {
    return;
  }
  ((NSTextField *)peer).placeholderAttributedString = placeholder;
}

NSResponder *RnPeerEditor(NSView *peer) {
  if (peer == nil) {
    return nil;
  }
  // An NSTextView is the editor; an NSTextField borrows one from the window
  // and only while it has focus.
  return RnPeerIsMultiline(peer) ? (NSResponder *)peer : ((NSTextField *)peer).currentEditor;
}
