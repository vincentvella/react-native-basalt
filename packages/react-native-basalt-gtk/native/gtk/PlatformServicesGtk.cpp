// Linux's half of the platform-services seam: clipboard, opening a URI, alerts.

#include "PlatformServices.h"

#include <gtk/gtk.h>

#include <memory>
#include <utility>

namespace basalt {

namespace {

GdkClipboard *defaultClipboard() {
  GdkDisplay *display = gdk_display_get_default();
  return display != nullptr ? gdk_display_get_clipboard(display) : nullptr;
}

} // namespace

// --- Clipboard ---------------------------------------------------------------

// GdkClipboard reads asynchronously -- it may have to ask another process for
// the text -- and this seam is synchronous because `Clipboard.getString()`
// returns a promise that core resolves at once.
//
// The compromise is the content provider's *local* value, which is what this
// application last put there, plus nothing. Reading another application's
// clipboard needs the async read and a promise core does not yet hand down.
// Saying so here beats an empty string that looks like an empty clipboard.
std::string clipboardText() {
  GdkClipboard *clipboard = defaultClipboard();
  if (clipboard == nullptr) {
    return {};
  }

  GdkContentProvider *provider = gdk_clipboard_get_content(clipboard);
  if (provider == nullptr) {
    // Owned by another application. See the note above.
    return {};
  }

  GValue value = G_VALUE_INIT;
  g_value_init(&value, G_TYPE_STRING);
  std::string text;
  if (gdk_content_provider_get_value(provider, &value, nullptr)) {
    const char *string = g_value_get_string(&value);
    if (string != nullptr) {
      text = string;
    }
  }
  g_value_unset(&value);
  return text;
}

void setClipboardText(const std::string &text) {
  GdkClipboard *clipboard = defaultClipboard();
  if (clipboard == nullptr) {
    return;
  }
  gdk_clipboard_set_text(clipboard, text.c_str());
}

// --- Opening things ----------------------------------------------------------

bool canOpenUrl(const std::string &url) {
  if (url.empty()) {
    return false;
  }
  char *scheme = g_uri_parse_scheme(url.c_str());
  if (scheme == nullptr) {
    return false;
  }
  // Whether anything is registered to handle the scheme. The desktop's own
  // answer, from the same database that would do the opening.
  GAppInfo *handler = g_app_info_get_default_for_uri_scheme(scheme);
  g_free(scheme);
  if (handler == nullptr) {
    return false;
  }
  g_object_unref(handler);
  return true;
}

bool openUrl(const std::string &url) {
  GError *error = nullptr;
  const gboolean launched =
      g_app_info_launch_default_for_uri(url.c_str(), nullptr, &error);
  if (launched == FALSE) {
    if (error != nullptr) {
      g_warning("could not open %s: %s", url.c_str(), error->message);
      g_clear_error(&error);
    }
    return false;
  }
  return true;
}

// --- Alerts ------------------------------------------------------------------

namespace {

struct PendingAlert {
  AlertRequest request;
  AlertCallback onButton;
  GtkWidget *field{nullptr};
};

void onAlertResponse(GObject *source, GAsyncResult *result, gpointer userData) {
  std::unique_ptr<PendingAlert> pending{static_cast<PendingAlert *>(userData)};

  GError *error = nullptr;
  const int index =
      gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
  if (error != nullptr) {
    // Dismissed without choosing -- Escape, or the window closed. React Native
    // has no "cancelled" here, and reporting the last button would run whatever
    // it does; index 0 is the primary, which is what a plain alert's only
    // button is.
    g_clear_error(&error);
    pending->onButton(0, {});
    return;
  }
  pending->onButton(index, {});
}

gboolean showAlertOnMainThread(gpointer userData) {
  auto *pending = static_cast<PendingAlert *>(userData);

  GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", pending->request.title.c_str());
  if (!pending->request.message.empty()) {
    gtk_alert_dialog_set_detail(dialog, pending->request.message.c_str());
  }

  std::vector<const char *> labels;
  labels.reserve(pending->request.buttons.size() + 1);
  for (const auto &label : pending->request.buttons) {
    labels.push_back(label.c_str());
  }
  labels.push_back(nullptr);
  gtk_alert_dialog_set_buttons(dialog, labels.data());
  // The first is React Native's primary, and the same one onAlertResponse
  // reports when the dialog is dismissed without a choice.
  gtk_alert_dialog_set_default_button(dialog, 0);
  gtk_alert_dialog_set_cancel_button(dialog, 0);

  gtk_alert_dialog_choose(dialog, nullptr, nullptr, onAlertResponse, pending);
  g_object_unref(dialog);
  return G_SOURCE_REMOVE;
}

} // namespace

void showAlert(const AlertRequest &request, AlertCallback onButton) {
  if (request.hasTextInput) {
    // GtkAlertDialog has no text field, and building one means a GtkDialog with
    // its own layout. Not done: a prompt that silently became a plain alert
    // would lose whatever the user was meant to type.
    g_warning("Alert.prompt is not implemented on this platform");
  }

  auto *pending = new PendingAlert{request, std::move(onButton), nullptr};
  // Onto the GTK main thread. This is called from the JavaScript thread, and
  // a dialog may only be created there.
  g_idle_add_full(G_PRIORITY_DEFAULT, showAlertOnMainThread, pending, nullptr);
}

void postDelayed(double milliseconds, std::function<void()> work) {
  // Heap-allocated because g_timeout_add takes a void*, and freed by the
  // destroy notify whether the callback ran or the source was removed.
  auto *held = new std::function<void()>(std::move(work));
  g_timeout_add_full(
      G_PRIORITY_DEFAULT,
      static_cast<guint>(milliseconds),
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      held,
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

void postToUiThread(std::function<void()> work) {
  auto *held = new std::function<void()>(std::move(work));
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      held,
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

bool isUiThread() {
  // The thread running the default main context is the one GTK draws from, and
  // asking that rather than remembering a thread id keeps this true in a test
  // binary that never starts a loop -- where the honest answer is "no".
  return g_main_context_is_owner(g_main_context_default()) != 0;
}

} // namespace basalt
