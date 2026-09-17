// Desktop notifications on macOS, which this host cannot show.
//
// Not a missing implementation, and the reason is worth the file.
// `UNUserNotificationCenter` is the only supported API -- `NSUserNotification`
// has been deprecated since 10.14 -- and for a process with no bundle
// identifier `+currentNotificationCenter` does not return nil or fail: it
// raises `NSInternalInconsistencyException: bundleProxyForCurrentProcess is
// nil` and terminates the process. Verified, not assumed.
//
// This host is a bare executable. `react-native run-macos` builds a binary, not
// an `.app`, so there is no `Info.plist` and no bundle identifier. The API is
// therefore unsafe to touch here rather than merely unavailable, which is why
// the check below is on the bundle identifier and not on a call in a try block:
// a `@try` around it would still have to enter Apple's code to find out.
//
// When this host learns to package itself -- which is a real piece of work,
// because notifications, the Dock icon, the menu bar name, file associations
// and a registered URL scheme all come with the same bundle -- the
// implementation is a dozen lines against `UNUserNotificationCenter` and goes
// behind the same check. See plan/backlog.md.

#include "Notifications.h"

#import <Foundation/Foundation.h>

#include <string>
#include <vector>

namespace basalt {

NotificationSupport notificationSupport() {
  if (NSBundle.mainBundle.bundleIdentifier == nil) {
    return {false,
            "this host is not a bundled application, and macOS only delivers "
            "notifications for one"};
  }
  // A bundled build has a bundle identifier and could use
  // UNUserNotificationCenter; nothing builds one yet, so saying so is more
  // useful than reaching for an API and finding out in the crash log.
  return {false, "notifications are not implemented for a bundled macOS build yet"};
}

bool showNotification(const std::string &identifier, const NotificationContent &content) {
  (void)identifier;
  (void)content;
  return false;
}

bool dismissNotification(const std::string &identifier) {
  (void)identifier;
  return false;
}

void dismissAllNotifications() {}

std::vector<std::string> presentedNotifications() {
  return {};
}

} // namespace basalt
