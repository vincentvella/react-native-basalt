# Phase 9 — `<TextInput>`

> **Done, 2026-09-09.** `src/GtkTextInput.*`,
> `packages/react-native-linux/src/overrides/TextInput.linux.js`.

**Goal:** type into a field and have React see it.

## The native half is React Native's, unchanged

React Native ships two C++ text inputs. Android's includes `fbjni` and calls
into a Java `FabricUIManager`, so it is unusable outside an Android build. The
iOS one is pure C++ -- its props, shadow node, state and event emitter include
nothing but ReactCommon headers -- and it measures through `TextLayoutManager`,
which on this platform is the Pango one. So it is compiled as-is and registered
under the name it declares, `TextInput`.

Nothing renames anything. React Native's own `componentNameByReactViewName`
already maps `RCTSinglelineTextInputView` to `TextInput`, because iOS needed
exactly the same bridge between a view config name and a C++ component name.

## The editing is a real GtkText

Not a cursor drawn on a `PangoLayout`. `GtkText` is the widget inside
`GtkEntry`, and it brings input methods, selection, the clipboard, and every
keybinding a Linux user expects -- none of which is worth reimplementing and all
of which is easy to get subtly wrong. It lives as a non-`RnView` child of the
view Fabric mounted, and `RnLayout` allocates it inside the view's content
inset, so `paddingHorizontal` on a field means what it means on a `<View>`.

Its text is styled with a `PangoAttrList` built from the same code that styles a
`<Text>`. Without that it renders in the GTK theme's colour, which on a dark
field is dark text on dark.

## The hard part is the loop, not the typing

React Native's `<TextInput>` is controlled. JavaScript owns the value; the
widget reports every change; JavaScript re-renders and sends the value back down
as a prop. Three things go wrong there, and all three did:

- Applying the prop fires GtkText's `changed` signal, which looks like the user
  typing, which reports a change, which re-renders. An `applying` flag breaks it.
- Assigning the text resets GtkText's cursor to the start, so the caret goes
  home on every keystroke of a controlled field. The position is saved and put
  back.
- A prop can be older than what the user has since typed. That is what React
  Native's `eventCount` is for, and `setTextAndSelection` drops a command that
  carries a stale one.

## The JavaScript half is ours, and that is a real cost

React Native's `TextInput.js` is
`if (Platform.OS === 'android') { ... } else if (Platform.OS === 'ios') { ... }`,
and on a third platform neither branch runs, so it renders undefined. There is
no third branch to add without forking fifteen hundred lines that would then
drift from upstream in silence, so `TextInput.linux.js` is a much smaller file
that renders the same native component the iOS path does.

That is a fork, and it is the first one. It should be read as a debt, not a
pattern: every prop React Native adds to `<TextInput>` is a prop this file will
not have. `plan/backlog.md` lists what is already missing.

## A threading bug this exposed

`schedulerDidDispatchCommand` arrives on the JavaScript thread, and
`GtkMountingManager::dispatchCommand` was calling straight into GTK from there.
That was survivable while the only commands were `ScrollView`'s, which move an
adjustment and nothing else. `focus` reaches the platform input method, and on
macOS AppKit asserts it is on the main thread and traps the process.

Commands now take the same trip through `g_idle_add` that mounting does, at the
same priority, so a command still lands behind the transaction that created the
view it names. The bug was there before this phase; a text field is just the
first thing that could not survive it.

## Verified

Typed into, through an X server, on Ubuntu 24.04 arm64: the field takes focus
from the `focus` command, the characters reach React through `onChangeText`, the
demo renders them in a sibling `<Text>`, and Enter fires `onSubmitEditing`,
whose handler uppercases the value and pushes it back down into the widget.
Eight unit tests cover the loop; one end-to-end scenario covers the round trip.

## Not done

- No `multiline`, so `RCTMultilineTextInputView` is unregistered.
- No `onKeyPress`, `onSelectionChange`, `onScroll` or `selection` prop.
- No shared focus registry, so `TextInput.State.currentlyFocusedInput()` is
  absent and no other component can ask what has focus.
- `blur` moves focus to the window rather than dropping it, because GTK has no
  "unfocus this widget".
- `placeholderTextColor`, `selectionColor` and `cursorColor` are parsed and
  ignored: GtkText takes those from CSS, not from a `PangoAttrList`.
