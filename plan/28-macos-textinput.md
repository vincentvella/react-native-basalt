# Phase 28 — `<TextInput>` on macOS, and the demo app running on both

> **Done, 2026-09-11.** The last of the four components. The demo app written
> for GTK, unchanged, now runs on macOS and produces the same tree.

## The peer

A real `NSTextField`, not a caret drawn on a paragraph -- input methods,
selection, the clipboard and every key binding a Mac user expects are not worth
reimplementing and are easy to get subtly wrong. The GTK side embeds a real
`GtkText` for the same reason.

The field paints nothing at all: no bezel, no border, no background, no focus
ring. The `RnAppKitView` behind it draws the background, the border and the
corner radius, because those are React Native style props and the field knows
nothing about them. Left on, an NSTextField's own bezel sits on top of whatever
the style asked for.

`secureTextEntry` is a different **class** on AppKit rather than a property, so
turning it on rebuilds the field. The text comes across; focus does not, which
is the honest limit of doing it that way.

## The controlled-value loop

The hard part of a text field is not typing. React Native's `<TextInput>` is a
controlled component: JavaScript owns the value, the field reports every change,
JavaScript re-renders and pushes the value back down as a prop. If applying that
prop looks like the user typing, the two chase each other forever; if a prop
that arrives late is applied anyway, a fast typist watches characters reorder
themselves.

`applying` breaks the first loop and `eventCount` the second, exactly as on GTK,
because neither is about a toolkit. `js/input.js` is the test that proves it:
its first field upper-cases whatever it is given, so the text the user typed is
never what comes back -- a field that ignored the prop and a field that fought
it both look wrong immediately.

Clicking it and typing "hello" gives `focused`, `changed: hello`, and a field
reading `HELLO`. That is the whole round trip through AppKit, the emitter, the
event beat, React, and the mutation coming back down.

## Where AppKit's focus model is not GTK's

An `NSTextField` does not become first responder itself. While it is being
edited the first responder is its *field editor*, a shared `NSTextView` on loan
from the window, so `resignFirstResponder` on the field is not the blur it looks
like and `becomeFirstResponder` is only half the story. Focus therefore comes
from overriding `becomeFirstResponder` on the field and blur from the delegate's
`controlTextDidEndEditing:`, which is what actually marks the end of an edit.

The same shape shows up in the tree dump: reporting whether a field is focused
means asking the window whether its first responder is this field *or a text
view whose delegate is this field*, because the second is what is true while
someone is typing.

## Two smaller findings

**`NSColor.placeholderTextColor` can resolve to nothing.** It is a dynamic
catalog colour, resolved against whatever appearance is current, and outside a
live one it resolves to a placeholder that exists and draws no pixels -- which
is what an offscreen snapshot showed. The field's own text colour at 45% renders
anywhere, and is the same on both desktops rather than being whatever each
toolkit's theme decided.

**`RawProps` must be parsed before it is read.** Building `TextInputProps` from
a freshly constructed `RawProps` trips an assertion inside React Native rather
than producing empty props, which is a much better failure than it sounds. The
GTK suite already did this correctly and its comment said so; writing the AppKit
tests without reading it first cost the time it deserved to.

## The demo app, on both

`js/index.js` is the app written for GTK in phase 04 and grown since: text,
images, a scrolling list of twenty-four rows, a text field, buttons, border
radii, borders, transforms. It has never known which desktop it is on.

It now renders on macOS, and `scripts/compare_all.sh` reports its tree identical
to the GTK one. Getting there needed two changes, and both were the demo's
problem rather than a platform's.

The heading rendered `Platform.OS`, which made the tree differ between the two
desktops for a reason that was not a bug -- so the richest app there is could
never be compared. It logs it instead, and the end-to-end suite's assertion
moved from the tree to the log, which checks the same fact.

And `g_strescape` escapes every byte above 0x7f into an octal escape, so a
string containing "·" came out as `\302\267` on GTK and as itself on AppKit --
a difference in a character neither platform had done anything to. GTK now
escapes backslash, quote and newline and nothing else, which matches AppKit and
is easier to read besides.

```
  views    identical, frames included
  press    identical, frames included
  scroll   identical, frames included
  image    identical, frames included
  text     identical, frames ignored
  a11y     identical, frames ignored
  input    identical, frames ignored
  index    identical, frames ignored

  the two hosts agree on all 8 apps
```

## What is missing

**No `maxLength`.** An `NSTextField` has no maximum length; enforcing one needs
a formatter or a delegate that rejects edits. GTK gets it from
`gtk_text_set_max_length`, so this is a real behavioural difference between the
two and the first thing here worth fixing.

**No multiline.** `<TextInput multiline>` wants an `NSTextView` in a scroll view,
which is a different peer with a different delegate protocol. Missing on GTK
too.

**No selection reporting on change.** The caret position is read from the field
editor, which only exists while the field has focus; a change event for an
unfocused field reports the caret at the end.

**No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`**, all of
which AppKit has some form of and none of which is wired.

**Focus is still not reachable by keyboard.** Nothing implements a tab order on
either platform, so a field can be clicked into and not tabbed into.
