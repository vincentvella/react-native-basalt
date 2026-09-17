// Desktop notifications on Windows, which this host cannot show.
//
// A toast is `ToastNotificationManager::CreateToastNotifier`, and it wants an
// AppUserModelID that resolves to a shortcut installed in the Start Menu. That
// is the same "be an installed application" requirement macOS makes in a
// different shape, and a host run out of a build directory meets neither.
//
// The legacy alternative, `Shell_NotifyIcon` with `NIF_INFO`, does work for an
// unpackaged executable -- but it is a balloon from a tray icon, so it needs a
// tray icon to come from, and a tray icon is its own feature with its own
// lifetime and menu. It is the right answer to pair with one; see
// plan/backlog.md, where a tray icon has been on the list since before this.
//
// See core/Notifications.h for what Linux does and why it can.

#include "Notifications.h"

#include <string>
#include <vector>

namespace basalt {

NotificationSupport notificationSupport() {
  return {false,
          "a Windows toast needs an AppUserModelID and an installed shortcut, "
          "which a host run from a build directory does not have"};
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
