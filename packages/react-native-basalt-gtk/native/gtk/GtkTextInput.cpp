#include "GtkTextInput.h"

#include "PangoTextLayout.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>

#include <cmath>
#include <string_view>

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;

GtkTextInputManager::GtkTextInputManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void GtkTextInputManager::update(RnView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;
  entry.owner = this;

  if (inserted) {
    entry.editable = rn_view_set_editable(view, TRUE);

    g_signal_connect(entry.editable, "changed", G_CALLBACK(onChanged), &entry);
    g_signal_connect(entry.editable, "activate", G_CALLBACK(onActivate), &entry);

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(onFocusEnter), &entry);
    g_signal_connect(focus, "leave", G_CALLBACK(onFocusLeave), &entry);
    gtk_widget_add_controller(GTK_WIDGET(entry.editable), focus);
  }

  // Yoga has resolved border and padding into the content inset; the GtkText is
  // allocated inside it, so `paddingHorizontal` on a field means what it means
  // on a <View>.
  const auto &insets = shadowView.layoutMetrics.contentInsets;
  const GtkBorder border = {
      .left = static_cast<int16_t>(std::lround(insets.left)),
      .right = static_cast<int16_t>(std::lround(insets.right)),
      .top = static_cast<int16_t>(std::lround(insets.top)),
      .bottom = static_cast<int16_t>(std::lround(insets.bottom)),
  };
  rn_view_set_peer_insets(view, &border);

  const auto props = std::dynamic_pointer_cast<const TextInputProps>(shadowView.props);
  if (props == nullptr || entry.editable == nullptr) {
    return;
  }

  // Controlled component: JavaScript owns the value. Setting it must not look
  // like typing, or the change we report provokes a re-render that sets it
  // again, and the two chase each other.
  const char *current = gtk_editable_get_text(GTK_EDITABLE(entry.editable));
  if (props->text != (current != nullptr ? current : "")) {
    entry.applying = true;
    // Preserve the cursor: assigning the text resets it to the start, which
    // sends the caret home on every keystroke of a controlled input.
    const int cursor = gtk_editable_get_position(GTK_EDITABLE(entry.editable));
    gtk_editable_set_text(GTK_EDITABLE(entry.editable), props->text.c_str());
    gtk_editable_set_position(GTK_EDITABLE(entry.editable),
                              MIN(cursor, static_cast<int>(props->text.size())));
    entry.applying = false;
    entry.lastReportedText = props->text;
  }

  gtk_text_set_placeholder_text(entry.editable,
                                props->placeholder.empty() ? nullptr : props->placeholder.c_str());

  // GtkText renders in the GTK theme's colour and font, which has nothing to do
  // with the `style` this component was given -- on a dark field that is dark
  // text on dark. A PangoAttrList is how GtkText takes the style instead.
  //
  // fontSizeMultiplier is 1: nothing on this platform scales text for
  // accessibility settings yet, and passing 0 would multiply the size away.
  PangoAttrList *attributes = buildTextAttributes(props->getEffectiveTextAttributes(1.0F));
  gtk_text_set_attributes(entry.editable, attributes);
  pango_attr_list_unref(attributes);

  // `editable` is the prop; `readOnly` is the newer spelling of its inverse,
  // and React Native honours both.
  const bool writable = props->traits.editable && !props->readOnly;
  gtk_editable_set_editable(GTK_EDITABLE(entry.editable), writable ? TRUE : FALSE);

  gtk_text_set_visibility(entry.editable, props->traits.secureTextEntry ? FALSE : TRUE);

  if (props->maxLength > 0 && props->maxLength < 1000000) {
    gtk_text_set_max_length(entry.editable, props->maxLength);
  }

  if (inserted && props->autoFocus) {
    gtk_widget_grab_focus(GTK_WIDGET(entry.editable));
  }
}

void GtkTextInputManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The signal handlers hold a pointer to the Entry. Dropping the editable
  // first takes them with it.
  if (it->second.view != nullptr && RN_IS_VIEW(it->second.view)) {
    rn_view_set_editable(it->second.view, FALSE);
  }
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

std::shared_ptr<const TextInputEventEmitter> GtkTextInputManager::emitterFor(Tag tag) const {
  return std::dynamic_pointer_cast<const TextInputEventEmitter>(lookup_(tag));
}

TextInputEventEmitter::Metrics GtkTextInputManager::metricsFor(const Entry &entry) const {
  const char *text = entry.editable != nullptr
      ? gtk_editable_get_text(GTK_EDITABLE(entry.editable))
      : "";

  TextInputEventEmitter::Metrics metrics{};
  metrics.text = text != nullptr ? text : "";
  metrics.eventCount = entry.eventCount;
  metrics.target = entry.tag;

  const int cursor = entry.editable != nullptr
      ? gtk_editable_get_position(GTK_EDITABLE(entry.editable))
      : 0;
  metrics.selectionRange = AttributedString::Range{cursor, 0};

  // The scroll-shaped fields exist because iOS's text view is a scroll view.
  // Nothing here scrolls yet, so they describe a viewport the size of the
  // field, which is true and keeps JavaScript's arithmetic sane.
  const auto width = static_cast<facebook::react::Float>(
      entry.view != nullptr ? gtk_widget_get_width(GTK_WIDGET(entry.view)) : 0);
  const auto height = static_cast<facebook::react::Float>(
      entry.view != nullptr ? gtk_widget_get_height(GTK_WIDGET(entry.view)) : 0);
  metrics.containerSize = {.width = width, .height = height};
  metrics.contentSize = metrics.containerSize;
  metrics.layoutMeasurement = metrics.containerSize;
  metrics.zoomScale = 1.0F;

  return metrics;
}

void GtkTextInputManager::onChanged(GtkEditable * /*editable*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (entry->applying) {
    // This is a prop being applied, not the user typing.
    return;
  }

  const char *text = gtk_editable_get_text(GTK_EDITABLE(entry->editable));
  const std::string value = text != nullptr ? text : "";
  if (value == entry->lastReportedText) {
    return;
  }
  entry->lastReportedText = value;
  entry->eventCount++;

  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onChange(entry->owner->metricsFor(*entry));
  }
}

void GtkTextInputManager::onActivate(GtkText * /*editable*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    // Enter. React Native calls this submitEditing, and follows it with
    // endEditing on platforms where the field also gives up focus; GtkText
    // keeps focus on activate, so only the submit is reported.
    emitter->onSubmitEditing(entry->owner->metricsFor(*entry));
  }
}

void GtkTextInputManager::onFocusEnter(GtkEventControllerFocus * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onFocus(entry->owner->metricsFor(*entry));
  }
}

void GtkTextInputManager::onFocusLeave(GtkEventControllerFocus * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onBlur(entry->owner->metricsFor(*entry));
    emitter->onEndEditing(entry->owner->metricsFor(*entry));
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool GtkTextInputManager::dispatchCommand(Tag tag,
                                          const std::string &name,
                                          const folly::dynamic &args) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (entry.editable == nullptr) {
    return false;
  }

  if (name == "focus") {
    gtk_widget_grab_focus(GTK_WIDGET(entry.editable));
    return true;
  }

  if (name == "blur") {
    // GTK has no "unfocus this widget"; focus moves, it is not dropped. Handing
    // it to the window's default is the closest thing, and it makes the focus
    // controller report a leave, so JavaScript still sees onBlur.
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(entry.editable));
    if (root != nullptr) {
      gtk_widget_grab_focus(GTK_WIDGET(root));
    }
    return true;
  }

  if (name == "setTextAndSelection") {
    // [eventCount, text, start, end]. An eventCount older than what the user
    // has since typed means this command is stale and must be dropped, which is
    // the whole reason React Native counts them.
    if (args.isArray() && args.size() >= 2) {
      const int eventCount = static_cast<int>(args[0].asInt());
      if (eventCount < entry.eventCount) {
        return true;
      }
      entry.applying = true;
      const auto text = args[1].isString() ? args[1].asString() : std::string{};
      gtk_editable_set_text(GTK_EDITABLE(entry.editable), text.c_str());
      if (args.size() >= 4 && args[2].isInt()) {
        const int start = static_cast<int>(args[2].asInt());
        const int end = static_cast<int>(args[3].asInt());
        gtk_editable_select_region(GTK_EDITABLE(entry.editable), start, end);
      }
      entry.applying = false;
      entry.lastReportedText = text;
    }
    return true;
  }

  return false;
}

} // namespace basalt
