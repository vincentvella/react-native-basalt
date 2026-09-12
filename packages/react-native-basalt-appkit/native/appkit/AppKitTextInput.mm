#import "AppKitTextInput.h"

#import "CoreTextLayout.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>

#include <cmath>
#include <string_view>

// A field that reports gaining focus.
//
// NSTextField does not: while it is being edited the first responder is its
// *field editor*, a shared NSTextView, so `resignFirstResponder` on the field
// is not the blur it looks like. Becoming first responder is the reliable half
// and is overridden here; the other half comes from the delegate's
// controlTextDidEndEditing:, which is what actually marks the end of an edit.
@protocol RnAppKitTextFieldOwner <NSObject>
- (void)rnFieldDidBecomeFirstResponder:(NSInteger)tag;
@end

@interface RnAppKitTextField : NSTextField
@property(nonatomic, assign) NSInteger rnTag;
@property(nonatomic, weak) id<RnAppKitTextFieldOwner> rnOwner;
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
@property(nonatomic, weak) id<RnAppKitTextFieldOwner> rnOwner;
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

// The bridge between AppKit's delegate protocol and the C++ manager, for the
// same reason the touch dispatcher and the scroll manager have one.
@interface RnAppKitTextInputDelegate : NSObject <NSTextFieldDelegate, RnAppKitTextFieldOwner>
@property(nonatomic, assign) basalt::AppKitTextInputManager *manager;
@end

@implementation RnAppKitTextInputDelegate

static NSInteger RnTagOf(id object) {
  if ([object respondsToSelector:@selector(rnTag)]) {
    return [object rnTag];
  }
  return 0;
}

- (void)controlTextDidChange:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleChanged(static_cast<facebook::react::Tag>(RnTagOf(notification.object)));
  }
}

- (void)controlTextDidEndEditing:(NSNotification *)notification {
  if (_manager != nullptr) {
    _manager->handleBlur(static_cast<facebook::react::Tag>(RnTagOf(notification.object)));
  }
}

- (void)rnFieldDidBecomeFirstResponder:(NSInteger)tag {
  if (_manager != nullptr) {
    _manager->handleFocus(static_cast<facebook::react::Tag>(tag));
  }
}

// Return. React Native calls this submitEditing, and follows it with
// endEditing on platforms where the field also gives up focus; an NSTextField
// keeps focus on Return, so only the submit is reported here -- the same
// asymmetry the GTK side has with GtkText's `activate`.
- (BOOL)control:(NSControl *)control
               textView:(NSTextView *)textView
    doCommandBySelector:(SEL)selector {
  (void)textView;
  if (selector == @selector(insertNewline:) && _manager != nullptr) {
    _manager->handleSubmit(static_cast<facebook::react::Tag>(RnTagOf(control)));
    return YES;
  }
  return NO;
}

@end

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;

AppKitTextInputManager::AppKitTextInputManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {
  RnAppKitTextInputDelegate *delegate = [[RnAppKitTextInputDelegate alloc] init];
  delegate.manager = this;
  delegate_ = delegate;
}

AppKitTextInputManager::~AppKitTextInputManager() {
  ((RnAppKitTextInputDelegate *)delegate_).manager = nullptr;
  delegate_ = nil;
}

AppKitTextInputManager::Entry *AppKitTextInputManager::entryFor(Tag tag) {
  const auto it = entries_.find(tag);
  return it == entries_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void AppKitTextInputManager::makeField(Entry &entry, bool secure) {
  NSString *existing = entry.field != nil ? entry.field.stringValue : @"";
  [entry.field removeFromSuperview];

  NSTextField *field;
  if (secure) {
    RnAppKitSecureTextField *secureField = [[RnAppKitSecureTextField alloc] initWithFrame:NSZeroRect];
    secureField.rnTag = entry.tag;
    secureField.rnOwner = (id<RnAppKitTextFieldOwner>)delegate_;
    field = secureField;
  } else {
    RnAppKitTextField *plain = [[RnAppKitTextField alloc] initWithFrame:NSZeroRect];
    plain.rnTag = entry.tag;
    plain.rnOwner = (id<RnAppKitTextFieldOwner>)delegate_;
    field = plain;
  }

  // The RnAppKitView behind it draws the background, the border and the corner
  // radius, because those are React Native style props and the field knows
  // nothing about them. So the field itself paints nothing at all -- otherwise
  // an NSTextField's own bezel sits on top of whatever the style asked for.
  field.bordered = NO;
  field.bezeled = NO;
  field.drawsBackground = NO;
  field.focusRingType = NSFocusRingTypeNone;
  field.delegate = (id<NSTextFieldDelegate>)delegate_;
  field.stringValue = existing;
  // Return has to reach doCommandBySelector: rather than being swallowed as a
  // "do nothing" action, which is what an NSTextField with no target does.
  field.target = nil;
  field.action = nullptr;

  entry.field = field;
  entry.secure = secure;
  entry.view.rnEditable = field;
  [entry.view addSubview:field];
}

void AppKitTextInputManager::update(RnAppKitView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;

  const auto props = std::dynamic_pointer_cast<const TextInputProps>(shadowView.props);
  const bool secure = props != nullptr && props->traits.secureTextEntry;

  if (inserted || entry.field == nil || entry.secure != secure) {
    // A secure field is a different class on AppKit, not a property, so
    // toggling secureTextEntry means building a new one. The text comes across;
    // focus does not, which is the honest limit of doing it this way.
    makeField(entry, secure);
  }

  // Yoga has resolved border and padding into the content inset; the field is
  // placed inside it, so `paddingHorizontal` on a field means what it means on
  // a <View>.
  const auto &frame = shadowView.layoutMetrics.frame;
  const auto &insets = shadowView.layoutMetrics.contentInsets;
  const CGFloat innerWidth = (CGFloat)frame.size.width - insets.left - insets.right;
  const CGFloat innerHeight = (CGFloat)frame.size.height - insets.top - insets.bottom;
  entry.field.frame = NSMakeRect(insets.left,
                                 insets.top,
                                 std::max<CGFloat>(0, innerWidth),
                                 std::max<CGFloat>(0, innerHeight));

  if (props == nullptr) {
    return;
  }

  // Controlled component: JavaScript owns the value. Three things have to be
  // true at once, and each fails differently.
  //
  // Setting it must not look like typing, or the change we report provokes a
  // re-render that sets it again and the two chase each other -- `applying`.
  //
  // A prop older than what the user has since typed must not be applied at all,
  // or a fast typist watches characters reorder themselves. That is what
  // React Native counts events for, and dropping such a value *without
  // recording it* is deliberate: the next render, once JavaScript has caught
  // up, applies it.
  //
  // And it is applied when the prop *changes*, not when it differs from the
  // field, which is the only thing that tells a controlled field from an
  // uncontrolled one. See the header.
  const bool stale = props->mostRecentEventCount < entry.eventCount;
  const bool changed = !entry.sawProps || props->text != entry.lastPropText;
  if (changed && !stale) {
    entry.lastPropText = props->text;
    entry.sawProps = true;

    const char *currentUtf8 = entry.field.stringValue.UTF8String;
    const std::string current = currentUtf8 != nullptr ? currentUtf8 : "";
    if (props->text != current) {
      entry.applying = true;
      NSString *incoming = [NSString stringWithUTF8String:props->text.c_str()];
      entry.field.stringValue = incoming != nil ? incoming : @"";
      entry.applying = false;
      entry.lastReportedText = props->text;
    }
  }

  // The field renders in AppKit's own font and colour, which has nothing to do
  // with the `style` this component was given -- on a dark field that is dark
  // text on dark. buildTextAttributes is what the style becomes, and it was
  // written for the text layer with this as its eventual second caller.
  //
  // fontSizeMultiplier is 1: nothing on this platform scales text for
  // accessibility settings yet, and passing 0 would multiply the size away.
  NSDictionary<NSAttributedStringKey, id> *attributes =
      buildTextAttributes(props->getEffectiveTextAttributes(1.0F));
  entry.field.font = attributes[NSFontAttributeName];
  entry.field.textColor = attributes[NSForegroundColorAttributeName];
  if (NSParagraphStyle *style = attributes[NSParagraphStyleAttributeName]) {
    entry.field.alignment = style.alignment;
  }

  if (props->placeholder.empty()) {
    entry.field.placeholderString = nil;
  } else {
    NSString *placeholder = [NSString stringWithUTF8String:props->placeholder.c_str()];
    if (placeholder == nil) {
      placeholder = @"";
    }
    // Styled like the text, so a placeholder in a 20pt field is not 13pt --
    // the plain `placeholderString` uses AppKit's own font and a fixed grey.
    NSMutableDictionary *placeholderAttributes = [attributes mutableCopy];
    // Without the paragraph style. buildTextAttributes sets word wrapping,
    // which is right for a paragraph and wrong for a single-line field: an
    // NSTextField given a wrapping placeholder draws no placeholder at all.
    // The alignment it also carried is already on the field itself, above.
    [placeholderAttributes removeObjectForKey:NSParagraphStyleAttributeName];
    if (props->placeholderTextColor) {
      const auto components =
          facebook::react::colorComponentsFromColor(props->placeholderTextColor);
      placeholderAttributes[NSForegroundColorAttributeName] =
          [NSColor colorWithSRGBRed:components.red
                              green:components.green
                               blue:components.blue
                              alpha:components.alpha];
    } else {
      // The text colour, faded -- not `NSColor.placeholderTextColor`.
      //
      // That one is a dynamic catalog colour: it resolves against whatever
      // appearance is current, and outside a live one it can resolve to nothing
      // at all, which is a placeholder that exists and draws no pixels. It also
      // would not match what the GTK side shows, which comes from its own
      // theme. A fixed fraction of the field's own colour renders anywhere and
      // is the same on both desktops.
      NSColor *text = placeholderAttributes[NSForegroundColorAttributeName];
      NSColor *srgb = [text colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
      if (srgb != nil) {
        placeholderAttributes[NSForegroundColorAttributeName] =
            [NSColor colorWithSRGBRed:srgb.redComponent
                                green:srgb.greenComponent
                                 blue:srgb.blueComponent
                                alpha:srgb.alphaComponent * 0.45];
      }
    }
    entry.field.placeholderAttributedString =
        [[NSAttributedString alloc] initWithString:placeholder attributes:placeholderAttributes];
  }

  // `editable` is the prop; `readOnly` is the newer spelling of its inverse,
  // and React Native honours both.
  entry.field.editable = props->traits.editable && !props->readOnly;
  entry.field.selectable = YES;

  if (props->maxLength > 0 && props->maxLength < 1000000) {
    // NSTextField has no maximum length; enforcing one needs a formatter or a
    // delegate that rejects edits. Not implemented, and saying so beats
    // silently accepting more than the app asked for.
  }

  if (inserted && props->autoFocus) {
    [entry.view.window makeFirstResponder:entry.field];
  }
}

void AppKitTextInputManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The delegate dispatches by tag, so dropping the entry is what stops events
  // reaching a dead view; taking the field out of the hierarchy is what stops
  // it being drawn.
  [it->second.field removeFromSuperview];
  it->second.field.delegate = nil;
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

std::shared_ptr<const TextInputEventEmitter> AppKitTextInputManager::emitterFor(Tag tag) const {
  return std::dynamic_pointer_cast<const TextInputEventEmitter>(lookup_(tag));
}

TextInputEventEmitter::Metrics AppKitTextInputManager::metricsFor(const Entry &entry) const {
  TextInputEventEmitter::Metrics metrics{};
  const char *utf8 = entry.field != nil ? entry.field.stringValue.UTF8String : nullptr;
  metrics.text = utf8 != nullptr ? utf8 : "";
  metrics.eventCount = entry.eventCount;
  metrics.target = entry.tag;

  // The caret, from the field editor -- an NSTextField has no selection of its
  // own, because the editing is done by a shared NSTextView on loan from the
  // window while the field has focus.
  int cursor = static_cast<int>(metrics.text.size());
  if (entry.field != nil) {
    if (NSText *editor = entry.field.currentEditor) {
      cursor = static_cast<int>(editor.selectedRange.location);
    }
  }
  metrics.selectionRange = AttributedString::Range{cursor, 0};

  // The scroll-shaped fields exist because iOS's text view is a scroll view.
  // Nothing here scrolls yet, so they describe a viewport the size of the
  // field, which is true and keeps JavaScript's arithmetic sane.
  const auto width = static_cast<facebook::react::Float>(
      entry.view != nil ? entry.view.bounds.size.width : 0);
  const auto height = static_cast<facebook::react::Float>(
      entry.view != nil ? entry.view.bounds.size.height : 0);
  metrics.containerSize = {.width = width, .height = height};
  metrics.contentSize = metrics.containerSize;
  metrics.layoutMeasurement = metrics.containerSize;
  metrics.zoomScale = 1.0F;

  return metrics;
}

void AppKitTextInputManager::handleChanged(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr || entry->applying) {
    // This is a prop being applied, not the user typing.
    return;
  }

  const char *utf8 = entry->field.stringValue.UTF8String;
  const std::string value = utf8 != nullptr ? utf8 : "";
  if (value == entry->lastReportedText) {
    return;
  }
  entry->lastReportedText = value;
  entry->eventCount++;

  if (const auto emitter = emitterFor(tag)) {
    emitter->onChange(metricsFor(*entry));
  }
}

void AppKitTextInputManager::handleSubmit(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr) {
    return;
  }
  if (const auto emitter = emitterFor(tag)) {
    emitter->onSubmitEditing(metricsFor(*entry));
  }
}

void AppKitTextInputManager::handleFocus(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr) {
    return;
  }
  if (const auto emitter = emitterFor(tag)) {
    emitter->onFocus(metricsFor(*entry));
  }
}

void AppKitTextInputManager::handleBlur(Tag tag) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr) {
    return;
  }
  if (const auto emitter = emitterFor(tag)) {
    emitter->onBlur(metricsFor(*entry));
    emitter->onEndEditing(metricsFor(*entry));
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool AppKitTextInputManager::dispatchCommand(Tag tag,
                                             const std::string &name,
                                             const folly::dynamic &args) {
  Entry *entry = entryFor(tag);
  if (entry == nullptr || entry->field == nil) {
    return false;
  }

  if (name == "focus") {
    [entry->view.window makeFirstResponder:entry->field];
    return true;
  }

  if (name == "blur") {
    // Handing focus back to the window is the closest thing AppKit has to
    // "unfocus this": focus moves, it is not dropped. It ends the edit, so the
    // delegate reports a blur and JavaScript still sees onBlur.
    NSWindow *window = entry->view.window;
    if (window != nil && window.firstResponder != window) {
      [window makeFirstResponder:window];
    }
    return true;
  }

  if (name == "setTextAndSelection") {
    // [eventCount, text, start, end]. An eventCount older than what the user
    // has since typed means this command is stale and must be dropped, which is
    // the whole reason React Native counts them.
    if (args.isArray() && args.size() >= 2) {
      const int eventCount = static_cast<int>(args[0].asInt());
      if (eventCount < entry->eventCount) {
        return true;
      }
      entry->applying = true;
      const auto text = args[1].isString() ? args[1].asString() : std::string{};
      NSString *incoming = [NSString stringWithUTF8String:text.c_str()];
      entry->field.stringValue = incoming != nil ? incoming : @"";
      if (args.size() >= 4 && args[2].isInt() && args[3].isInt()) {
        const NSInteger start = static_cast<NSInteger>(args[2].asInt());
        const NSInteger end = static_cast<NSInteger>(args[3].asInt());
        if (NSText *editor = entry->field.currentEditor) {
          editor.selectedRange = NSMakeRange((NSUInteger)start, (NSUInteger)MAX(0, end - start));
        }
      }
      entry->applying = false;
      entry->lastReportedText = text;
    }
    return true;
  }

  return false;
}

} // namespace basalt
