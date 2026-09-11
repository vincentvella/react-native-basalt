// macOS's half of the platform-services seam: clipboard, opening a URL, alerts.

#include "PlatformServices.h"

#import <Cocoa/Cocoa.h>

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

// --- Alerts ------------------------------------------------------------------

void showAlert(const AlertRequest &request, AlertCallback onButton) {
  // Onto the main thread and back at once. This is called from the JavaScript
  // thread, and a modal run loop there would deadlock the runtime -- so the
  // dialog is scheduled and this returns immediately, with the answer arriving
  // through the callback.
  dispatch_async(dispatch_get_main_queue(), ^{
    @autoreleasepool {
      NSAlert *alert = [[NSAlert alloc] init];
      NSString *title = [NSString stringWithUTF8String:request.title.c_str()];
      NSString *message = [NSString stringWithUTF8String:request.message.c_str()];
      alert.messageText = title != nil ? title : @"";
      alert.informativeText = message != nil ? message : @"";

      // AppKit draws buttons right to left from the first added, and React
      // Native's first button is the one it considers primary -- so adding them
      // in order puts the primary on the right, which is where a Mac user
      // expects it and which is also what the returned index has to agree with.
      for (const auto &label : request.buttons) {
        NSString *text = [NSString stringWithUTF8String:label.c_str()];
        [alert addButtonWithTitle:text != nil ? text : @"OK"];
      }

      NSTextField *field = nil;
      if (request.hasTextInput) {
        field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
        NSString *value = [NSString stringWithUTF8String:request.defaultText.c_str()];
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
