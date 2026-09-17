#include "TestDialog.h"

#include <cstdlib>
#include <string>
#include <utility>

namespace basalt {

std::optional<int> scriptedDialogButton() {
  static const std::optional<int> button = []() -> std::optional<int> {
    const char *value = std::getenv("BASALT_TEST_DIALOG");
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    // A button index, and `dismiss` for the way out -- which is the last
    // button, the one React Native's alerts and this project's share picker
    // both put the cancelling choice in.
    if (std::string(value) == "dismiss") {
      return -1;
    }
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value) {
      return std::nullopt;
    }
    return static_cast<int>(parsed);
  }();
  return button;
}

void presentAlert(const AlertRequest &request, AlertCallback onButton) {
  const std::optional<int> scripted = scriptedDialogButton();
  if (!scripted.has_value()) {
    showAlert(request, std::move(onButton));
    return;
  }

  // The last button for `dismiss`, and the named one otherwise, clamped: a
  // script that names a button an app did not offer should answer the nearest
  // real one rather than an index nothing will understand.
  const int last = request.buttons.empty() ? 0 : static_cast<int>(request.buttons.size()) - 1;
  int button = *scripted < 0 ? last : *scripted;
  if (button > last) {
    button = last;
  }

  // Straight back, on this thread. The platform implementations marshal to the
  // main thread because they have to touch a window; there is no window here,
  // and an answer that arrives before the caller has returned is something
  // every caller already handles -- `AsyncPromise` and the alert module's
  // callback both settle whenever they are told to.
  onButton(button, request.defaultText);
}

std::optional<int> scriptedMenuChoice() {
  static const std::optional<int> choice = []() -> std::optional<int> {
    const char *value = std::getenv("BASALT_TEST_MENU");
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    if (std::string(value) == "dismiss") {
      return -1;
    }
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value) {
      return std::nullopt;
    }
    return static_cast<int>(parsed);
  }();
  return choice;
}

void presentMenu(const MenuRequest &request, MenuCallback onChosen) {
  const std::optional<int> scripted = scriptedMenuChoice();
  if (!scripted.has_value()) {
    showMenu(request, std::move(onChosen));
    return;
  }

  // Out of range answers as a dismissal rather than being clamped, which is the
  // opposite of what presentAlert does with a button. A menu's entries are not
  // interchangeable -- picking the nearest one would run something the script
  // did not ask for -- whereas an alert's last button is always the way out.
  const int last = static_cast<int>(request.entries.size()) - 1;
  const int index = (*scripted < 0 || *scripted > last) ? -1 : *scripted;
  onChosen(index);
}

} // namespace basalt
