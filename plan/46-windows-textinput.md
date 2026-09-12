# Phase 46 — `<TextInput>` on Windows

> **Done, 2026-09-12.** The last of the five components. Windows mounts
> everything the other two desktops do.

    BASALT_TEST_TAP: tapping (200, 84)
    BASALT_TEST_TAP: focused the field there
    [js] focused
    BASALT_TEST_TYPE: "hello"
    [js] changed: h
    [js] changed: he
    [js] changed: hel
    [js] changed: hell
    [js] changed: hello

...and the field reads `HELLO`, because `js/input.js` upper-cases whatever it is
given. That is the case worth running: the text the user typed is never what
comes back, so a field that ignored the prop and a field that fought it both
look wrong immediately.

## The demo app, on the third desktop

`js/index.js` -- the app written for GTK in phase 04 and grown since: text,
images, a scrolling list of twenty-four rows, a text field, buttons, border
radii, borders, transforms -- runs on Windows unchanged. Pressing its "focus the
field" button runs `field.current.focus()`, which is the `focus` command taking
the trip through `postToUiThread` that every command takes, and typing after it
produces "hello, Vince" in the sibling `<Text>`.

That is the point of the whole exercise, so it is worth stating plainly: the app
has never known which desktop it is on, and there are now three.

## The peer is a window, and everything hard follows from that

A real `EDIT` control, not a caret drawn on a DirectWrite layout. That brings
the IME, selection, the clipboard, undo, double-click word selection and
Ctrl+arrow -- none of it worth reimplementing, all of it easy to get subtly
wrong. GTK embeds a `GtkText` and AppKit an `NSTextField` for the same reason.

The difference is what a peer *is*. On both other desktops a React Native view
is a widget, so a text field is a widget inside a widget and the toolkit does
the rest. Here every view is a plain C++ object painted with Direct2D into one
HWND, and an EDIT is a child window. Three things follow, and none has an
equivalent on GTK or AppKit:

**It has to be positioned by hand.** `syncBounds` walks each field's view up to
the surface root and moves the control there. The host calls it after every
transaction *and after every wheel*, because a scroll moves views and produces
no mutation at all -- which is the one that would have been found late.

**It is always on top.** Child windows paint above whatever Direct2D drew, in an
order the React tree has no say in. A field cannot be covered by a later
sibling. It does not clip to a scrolled ancestor either; it is hidden when it
leaves the ancestor's box instead, which looks right and is not the same thing.

**Its events arrive at the parent.** `EN_CHANGE`, `EN_SETFOCUS` and
`EN_KILLFOCUS` are `WM_COMMAND` notifications to the host window, and the
colours are answered from `WM_CTLCOLOREDIT`. So `hostProc` forwards both, and
the mounting manager grew methods a mounting manager would not otherwise need.

## The controlled-value loop, and the bug the other two desktops still have

React Native's `<TextInput>` is controlled: JavaScript owns the value, the
control reports every change, JavaScript re-renders and pushes the value back
down. `applying` stops the write from looking like typing; `mostRecentEventCount`
stops a prop older than the user's latest keystroke from being applied. Both are
copied from the GTK file, because neither is about a toolkit.

What is *not* copied is when the prop is applied at all, and finding out why
cost the afternoon this phase is worth writing up for.

An **uncontrolled** field lost its text on the second keystroke. React Native's
`TextInput.js` sends `text={value ?? defaultValue}`, so an uncontrolled field
with no default sends `undefined` -- which arrives in C++ as the empty string,
indistinguishable from a controlled field that JavaScript has just cleared. It
also re-sends `mostRecentEventCount` on every change, and that alone is an
Update mutation. So a manager that applies `text` whenever it differs from the
control wipes the field every time the user types.

iOS does not have this because it does not read the prop: `updateState:` applies
the *state's* attributed string, and the native side writes the user's own text
into that state on every keystroke, so applying it back is a no-op. Reproducing
that here means writing `TextInputState` from the platform, which is a larger
change than this phase.

The rule instead is: apply the prop when the **prop** changes, not when it
differs from the control. A controlled field's value changes as the user types;
an uncontrolled field's never does. That fixes it in one line and is what
`win32_an_uncontrolled_field_keeps_what_was_typed` pins.

GTK and AppKit both apply on "differs from the widget", so both should have the
same bug; `plan/backlog.md` records it. It went unnoticed because `js/input.js`
looks at the *controlled* field, and the uncontrolled ones are only ever typed
into by hand.

## Two things an EDIT does not know about a React Native style

**`padding`.** Yoga has already resolved border and padding into
`contentInsets`, so the control is placed inside them -- the same numbers GTK
allocates its `GtkText` inside. Not `EM_SETRECT`, which is the obvious answer
and is documented as multiline-only: it compiles, it returns, and it does
nothing, which is an afternoon nobody should spend twice.

**Vertical centring.** A single-line EDIT draws at the top of its client area.
Windows' own fields are sized to their font so it never shows; a 44-point React
Native field with 16-point text puts the caret near the top and looks broken. So
the control is a strip one line high, centred in the content box -- measured
from the font actually in use, because a family substitution changes it.

That also keeps the control away from the corners, which solved a second problem
for free: the view behind it paints the rounded background, and a full-height
rectangular child window painted square corners straight over it.

## Where Windows has the easier job

`secureTextEntry` is `EM_SETPASSWORDCHAR`, a message. On AppKit it is a
different *class*, so turning it on rebuilds the field and focus is lost --
which phase 28 recorded as "the honest limit of doing it that way". Here it
toggles with the text and the caret intact.

`maxLength` is `EM_SETLIMITTEXT`. macOS has no equivalent at all and phase 28
called it "the first thing here worth fixing"; GTK and Windows both have it.

## Fifteen tests, which make a real window

Every other test on this platform runs without one, because Direct2D renders
offscreen. These cannot: a text field's peer *is* a window, so a test that mocks
it away tests nothing that could break. The window is `WS_POPUP` and never
shown, which keeps it runnable on a build agent.

One thing in there is worth naming because getting it wrong would have made the
suite worse than useless. The test window forwards `WM_COMMAND` to the manager
exactly as the host does. Without that the field's `EN_CHANGE` reaches nothing,
`eventCount` stays at zero, and every staleness assertion passes trivially --
a test that agrees with a broken implementation. Two of them did exactly that
before the forwarding was added.

What still cannot be asserted from a test is the events themselves: an
`EventEmitter` built by hand has no `EventDispatcher`, so `onChange` goes
nowhere. The transcript at the top is the proof for those.

## Checking it, and the snapshot that cannot

`BASALT_TEST_TYPE` sends real `WM_CHAR` messages to whichever field has focus,
so what it skips is the keyboard driver and nothing above it. `BASALT_TEST_TAP`
gained one step for this phase: it focuses the field under the point, because a
*real* click never reaches the touch dispatcher at all -- the peer is a child
window, so USER32 routes the click to it and the control focuses itself. That is
the one part of a click a synthesised tap cannot reproduce.

Enter was checked from outside the process, by posting `WM_KEYDOWN(VK_RETURN)`
to the focused control -- the Windows equivalent of the `xdotool` the Linux
end-to-end suite uses. It reached `onSubmitEditing` through
`TranslateMessage`, which is the real keyboard path and not a synthesised one.
A carriage return cannot travel in `BASALT_TEST_TYPE`: the environment variable
loses it somewhere between PowerShell and the child process.

**`BASALT_SNAPSHOT` cannot see a text field.** It renders the `RnWin32View` tree
offscreen, and a peer is not in that tree -- so a field comes out as its
background with no text, no placeholder and no caret. That is not a bug in the
snapshot; it is the same fact as "the peer is a window", seen from the other
side. Checking a field's appearance means `PrintWindow` on the live window, and
that is how the pictures for this phase were taken.

## What is missing

**No multiline.** `<TextInput multiline>` wants `ES_MULTILINE` and a different
set of behaviours around Enter and scrolling. Missing on all three desktops.

**No `onKeyPress`, `onSelectionChange` or `selection`.** The first two have
obvious homes -- the subclass already sees every `WM_CHAR`, and `EM_SETSEL` is
already read for metrics -- and neither is wired.

**No tab order**, so a field can be clicked into and not tabbed into. Tab is
currently swallowed to stop the EDIT beeping at it, which is the right
placeholder and the wrong end state. Missing on all three.

**No `keyboardType`, `autoCapitalize`, `autoCorrect` or `spellCheck`.** An EDIT
has `ES_NUMBER` and `ES_UPPERCASE` for the first two; the rest need the IME.

**`placeholderTextColor`, `selectionColor` and `cursorColor` are ignored.** The
cue banner and the selection take their colours from the system, and there is no
message to change either. The same gap GTK records for the same reason.

**A field with no `backgroundColor` paints the system window colour**, because
the control cannot see through itself to what Direct2D drew behind it. A field
over a coloured parent is the case this gets visibly wrong.
