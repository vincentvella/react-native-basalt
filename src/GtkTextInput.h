// <TextInput> on GTK4.
//
// The native half is React Native's own *iOS* TextInput: its shadow node,
// props, state and event emitter are pure C++ and measure through a
// TextLayoutManager, which on this platform is the Pango one. Android's variant
// includes fbjni and calls into a Java FabricUIManager, so it is unusable here;
// see plan/decisions.md. The component name is therefore "TextInput", which is
// what packages/react-native-linux' TextInput.linux.js asks for.
//
// The editing itself is a real GtkText -- the widget behind GtkEntry -- rather
// than a cursor drawn on a PangoLayout. That brings input methods, selection,
// the clipboard, and every keybinding a Linux user expects, none of which is
// worth reimplementing and all of which is easy to get subtly wrong.
//
// The awkward part of a text field is not typing, it is that React Native's
// <TextInput> is a controlled component: JavaScript owns the value, and the
// widget must not fight it. Every `changed` signal is reported to JavaScript,
// which re-renders and sends the text back down as a prop -- so applying that
// prop must not itself look like the user typing, or the two chase each other.
// `applying_` is what breaks that loop.

#pragma once

#include "RnView.h"

#include <react/renderer/components/iostextinput/TextInputShadowNode.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace rnlinux {

class GtkTextInputManager {
 public:
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit GtkTextInputManager(EmitterLookup lookup);

  // Called for every mutation touching a TextInput. Creates the editable the
  // first time, then applies props.
  void update(RnView *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // focus, blur, and setTextAndSelection. Returns false for anything else.
  bool dispatchCommand(facebook::react::Tag tag,
                       const std::string &name,
                       const folly::dynamic &args);

 private:
  struct Entry {
    RnView *view{nullptr};
    GtkText *editable{nullptr};
    facebook::react::Tag tag{0};
    GtkTextInputManager *owner{nullptr};

    // React Native counts events so it can ignore a prop update that is older
    // than what the user has since typed. Every emitted metric carries it.
    int eventCount{0};

    // True while a prop is being pushed into the widget, so the `changed`
    // signal it provokes is not reported back as the user typing.
    bool applying{false};

    std::string lastReportedText;
  };

  static void onChanged(GtkEditable *editable, gpointer userData);
  static void onActivate(GtkText *editable, gpointer userData);
  static void onFocusEnter(GtkEventControllerFocus *controller, gpointer userData);
  static void onFocusLeave(GtkEventControllerFocus *controller, gpointer userData);

  // Fills in the parts of Metrics every event carries.
  facebook::react::TextInputEventEmitter::Metrics metricsFor(const Entry &entry) const;

  std::shared_ptr<const facebook::react::TextInputEventEmitter> emitterFor(
      facebook::react::Tag tag) const;

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;
};

} // namespace rnlinux
