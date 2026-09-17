// macOS's half of the platform-services seam: clipboard, opening a URL, alerts.

#include "PlatformServices.h"
#include "TestDialog.h"

#include <cstdint>

#import <Cocoa/Cocoa.h>

// The picker's delegate, and its owner.
//
// NSSharingServicePicker holds its delegate weakly and is itself released as
// soon as `showRelativeToRect:` returns, so something has to keep both alive
// until the person has chosen. That something is this, which retains itself
// until the delegate method fires -- the same shape AppKitTouchDispatcher's
// input target has, for the same reason.
@interface RnAppKitSharePicker : NSObject <NSSharingServicePickerDelegate>
- (instancetype)initWithItems:(NSArray *)items done:(basalt::ShareCallback)done;
- (void)show:(NSView *)anchor;
@end

@implementation RnAppKitSharePicker {
  NSSharingServicePicker *_picker;
  basalt::ShareCallback _done;
  RnAppKitSharePicker *_self;
}

- (instancetype)initWithItems:(NSArray *)items done:(basalt::ShareCallback)done {
  self = [super init];
  if (self != nil) {
    _picker = [[NSSharingServicePicker alloc] initWithItems:items];
    _picker.delegate = self;
    _done = std::move(done);
  }
  return self;
}

- (void)show:(NSView *)anchor {
  // Retained until the delegate answers. Without this the picker is on screen
  // and the only thing that knows what to do with the answer has been freed.
  _self = self;
  [_picker showRelativeToRect:NSMakeRect(0, 0, 1, 1)
                       ofView:anchor
                preferredEdge:NSMinYEdge];
}

- (void)sharingServicePicker:(NSSharingServicePicker *)picker
     didChooseSharingService:(NSSharingService *)service {
  (void)picker;
  // nil means the picker was dismissed without a choice, which is React
  // Native's `dismissedAction` and not a failure.
  if (_done) {
    _done(service != nil ? basalt::ShareOutcome::Shared : basalt::ShareOutcome::Dismissed, "");
    _done = nullptr;
  }
  _picker.delegate = nil;
  _self = nil;
}

@end

namespace basalt {

// --- Clipboard ---------------------------------------------------------------

std::string clipboardText() {
  @autoreleasepool {
    NSString *text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
    return text != nil ? std::string(text.UTF8String) : std::string{};
  }
}

void setClipboardText(const std::string &text) {
  @autoreleasepool {
    NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
    // Clearing first is not optional: without it the new string is added
    // alongside whatever types were already there, and a paste can pick the old
    // one.
    [pasteboard clearContents];
    NSString *value = [NSString stringWithUTF8String:text.c_str()];
    [pasteboard setString:value != nil ? value : @"" forType:NSPasteboardTypeString];
  }
}

// --- Opening things ----------------------------------------------------------

bool canOpenUrl(const std::string &url) {
  @autoreleasepool {
    NSString *string = [NSString stringWithUTF8String:url.c_str()];
    if (string == nil) {
      return false;
    }
    NSURL *parsed = [NSURL URLWithString:string];
    if (parsed == nil || parsed.scheme == nil) {
      return false;
    }
    // Whether an application is registered for the scheme. Asking this is a
    // Launch Services lookup, which is the honest answer -- unlike on iOS it
    // needs no declared list of schemes.
    return [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:parsed] != nil;
  }
}

bool openUrl(const std::string &url) {
  @autoreleasepool {
    NSString *string = [NSString stringWithUTF8String:url.c_str()];
    if (string == nil) {
      return false;
    }
    NSURL *parsed = [NSURL URLWithString:string];
    if (parsed == nil) {
      return false;
    }
    return [NSWorkspace.sharedWorkspace openURL:parsed];
  }
}

// --- Sharing -----------------------------------------------------------------

// The one desktop with a share service of its own, so the one that does not go
// through core/ShareFallback.h.
//
// NSSharingServicePicker is the real thing: the list is whatever the machine
// has -- Mail, Messages, AirDrop, whatever an app installed -- and this host
// supplies only the items and a place to show it.
//
// Anchored to the bottom-left of the window rather than to a view the user
// pressed. A share sheet on a Mac normally hangs off the button that opened it,
// and React Native's API does not say which button that was: `Share.share()`
// takes content and nothing about where it came from. `options.anchor` is a
// node handle on iOS and is not carried here.
void shareContent(const ShareRequest &request, ShareCallback onDone) {
  // The only dialog on any of the three desktops that is not built on an alert,
  // so the only one the instrument has to be consulted for by hand; everything
  // else reaches it through core/TestDialog.h's presentAlert. A picker that is
  // up stops [NSApp terminate:] outright, so an automated run that opened one
  // would hang until it was killed.
  if (const std::optional<int> scripted = scriptedDialogButton()) {
    onDone(*scripted < 0 ? ShareOutcome::Dismissed : ShareOutcome::Shared, "");
    return;
  }

  // Copied before the block rather than captured by reference. The caller's
  // request is a stack temporary and this returns before the block runs, so a
  // reference into it would be read after it is gone -- which reads as a share
  // with nothing in it, not as a crash.
  const ShareRequest content = request;

  // Onto the main thread and back at once, for the same reason showAlert is:
  // this is called from the JavaScript thread, and the picker's own run loop
  // there would deadlock the runtime.
  dispatch_async(dispatch_get_main_queue(), ^{
    @autoreleasepool {
      NSMutableArray *items = [NSMutableArray array];
      if (!content.message.empty()) {
        NSString *message = [NSString stringWithUTF8String:content.message.c_str()];
        if (message != nil) {
          [items addObject:message];
        }
      }
      if (!content.url.empty()) {
        NSString *text = [NSString stringWithUTF8String:content.url.c_str()];
        NSURL *url = text != nil ? [NSURL URLWithString:text] : nil;
        // A URL as an NSURL rather than as a string, because that is what makes
        // a service treat it as a link -- Mail attaches it, Messages previews
        // it. One that does not parse is shared as the text it is.
        [items addObject:url != nil ? (id)url : (id)(text != nil ? text : @"")];
      }
      if (items.count == 0) {
        onDone(ShareOutcome::Failed, "nothing to share");
        return;
      }

      NSWindow *window = NSApp.keyWindow != nil ? NSApp.keyWindow : NSApp.mainWindow;
      NSView *anchor = window.contentView;
      if (anchor == nil) {
        onDone(ShareOutcome::Failed, "no window to show a share sheet in");
        return;
      }

      RnAppKitSharePicker *picker = [[RnAppKitSharePicker alloc] initWithItems:items done:onDone];
      [picker show:anchor];
    }
  });
}

// --- Alerts ------------------------------------------------------------------

void showAlert(const AlertRequest &request, AlertCallback onButton) {
  // Copied before the block, for the same reason shareContent does it: a block
  // capturing a variable of reference type captures the reference, not the
  // object behind it. The caller's request is a stack local and this returns
  // before the block runs, so reading through it afterwards is reading freed
  // memory -- which shows up as a dialog with no text in it rather than as a
  // crash, and was found by the share sheet next door coming out empty.
  const AlertRequest content = request;

  // Onto the main thread and back at once. This is called from the JavaScript
  // thread, and a modal run loop there would deadlock the runtime -- so the
  // dialog is scheduled and this returns immediately, with the answer arriving
  // through the callback.
  dispatch_async(dispatch_get_main_queue(), ^{
    @autoreleasepool {
      NSAlert *alert = [[NSAlert alloc] init];
      NSString *title = [NSString stringWithUTF8String:content.title.c_str()];
      NSString *message = [NSString stringWithUTF8String:content.message.c_str()];
      alert.messageText = title != nil ? title : @"";
      alert.informativeText = message != nil ? message : @"";

      // AppKit draws buttons right to left from the first added, and React
      // Native's first button is the one it considers primary -- so adding them
      // in order puts the primary on the right, which is where a Mac user
      // expects it and which is also what the returned index has to agree with.
      for (const auto &label : content.buttons) {
        NSString *text = [NSString stringWithUTF8String:label.c_str()];
        [alert addButtonWithTitle:text != nil ? text : @"OK"];
      }

      NSTextField *field = nil;
      if (content.hasTextInput) {
        field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
        NSString *value = [NSString stringWithUTF8String:content.defaultText.c_str()];
        field.stringValue = value != nil ? value : @"";
        alert.accessoryView = field;
        // Otherwise the field is on screen and not typing into.
        [alert.window setInitialFirstResponder:field];
      }

      // NSAlertFirstButtonReturn is 1000, and buttons count up from there in
      // the order they were added -- which is React Native's order.
      const auto finish = [onButton, field](NSModalResponse response) {
        std::string text;
        if (field != nil && field.stringValue.UTF8String != nullptr) {
          text = field.stringValue.UTF8String;
        }
        onButton((int)(response - NSAlertFirstButtonReturn), text);
      };

      // A sheet rather than `runModal`, and not only because it looks more like
      // a Mac. `runModal` spins its own run loop on the main thread, which
      // stops the main queue -- so every mount transaction, timer and animation
      // frame would be held until somebody clicked a button. A sheet returns
      // immediately and answers through its completion handler, which is also
      // what the GTK side's `gtk_alert_dialog_choose` does.
      // `?:` with an omitted middle is a GNU extension, and this project
      // builds with -Werror.
      NSWindow *window = NSApp.keyWindow != nil ? NSApp.keyWindow : NSApp.mainWindow;
      if (window != nil) {
        [alert beginSheetModalForWindow:window
                      completionHandler:^(NSModalResponse response) {
                        finish(response);
                      }];
        return;
      }

      // No window to attach to -- before the first one is on screen. A modal
      // run loop is the only option left, and blocking briefly is better than
      // dropping the alert.
      finish([alert runModal]);
    }
  });
}

void postDelayed(double milliseconds, std::function<void()> work) {
  const auto when = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(milliseconds * NSEC_PER_MSEC));
  dispatch_after(when, dispatch_get_main_queue(), ^{
    work();
  });
}

void postToUiThread(std::function<void()> work) {
  dispatch_async(dispatch_get_main_queue(), ^{
    work();
  });
}

bool isUiThread() {
  return [NSThread isMainThread];
}

} // namespace basalt
