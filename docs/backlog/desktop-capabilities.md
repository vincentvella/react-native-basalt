# Desktop capabilities

Part of the [backlog](../BACKLOG.md). Not scheduled.

An entry here is a gap, not a plan. When one is picked up it becomes a change in
`openspec/changes/`, which is where the requirements and the task list live from
then on -- the entry stays, so the catalogue remains a complete answer to "what
is missing", and gains a pointer. Three entries have one today.

**Open (33):**

1. A second window's children do not see React context
2. `useWindow()` inside a second window reports the active window
3. The title bar and the error inspector are the main window's
4. `<Modal>` is still an in-surface overlay
5. Packaging is macOS and Linux only, and shallow
6. A window cannot be positioned on Linux
7. A window's lifecycle is an app's to influence now
8. Menus have no checkbox or radio items, and no dynamic enabling
9. A system tray icon
10. Windows notifications carry no identity of their own
11. Cursor control
12. Application lifecycle -- refusing to quit is proposed
13. Drag and drop -- proposed
14. Clipboard
15. Shell integration
16. Displays and screen -- proposed
17. Power and idle
18. Global shortcuts
19. Permissions
20. Secure storage
21. Auto-update
22. Printing
23. File system beyond the dialogs
24. Screen capture and media devices
25. Crash reporting
26. Window state is not remembered between launches
27. No progress in the taskbar or dock
28. A window cannot represent a file
29. No vibrancy, transparency or window shape
30. No online and offline detection
31. No spell checking
32. No dock, launcher or jump-list menu
33. No way to do work off the JavaScript thread

React Native has no cross-platform API for any of this, because it was built for
phones. That makes each one a design question before it is an implementation
question: invent a `react-native-basalt` API, follow what react-native-macos or
react-native-windows already chose, or leave it to userland modules. Nothing
here has been decided, and none of it is needed for the demo, which is why it
has gone unrecorded until now.

- ~~**More than one window**~~, and ~~**a window that can refuse to close**~~.
  Done on all three: `<Window>` opens one, `useCloseRequest()` guards it. A
  window is a surface is a React root, which is Fabric's grain rather than a
  simplification -- and the refusal is a registration rather than a returned
  `false`, because the window manager wants a synchronous answer and the handler
  is on another thread, so the decision has to exist before the attempt.

  The four entries below are what follows from that and are still open. Refusing
  to *quit* is a different event and is under Application lifecycle in the
  catalogue.
- **A second window's children do not see React context** from the tree they
  were written in, because they are rendered in a different root. Props, state
  and callbacks cross; a provider does not. `<Window>`'s header says so.
- **`useWindow()` inside a second window reports the active window**, not the
  one it is in. A TurboModule has no idea which surface called it, which is the
  same gap that would have to be closed for per-window menus or title bars. The
  size limits are the sharpest version: they are one set of numbers for one
  window, so Windows answers WM_GETMINMAXINFO for the app's own window and no
  other, to keep the three hosts saying the same thing.
- **The title bar and the error inspector are the main window's.** Both are
  process-wide seams; making them per-window is its own piece of work.
- **`<Modal>` is still an in-surface overlay**, though a real window is now
  something this platform could do.
- **Packaging is macOS and Linux only, and shallow.** `react-native run-macos`
  builds a real `.app` -- `Info.plist`, an identifier, declared URL schemes, and
  an ad-hoc signature, which macOS requires before it will grant notification
  permission at all. What it does not do is an icon (`.icns` needs `iconutil`),
  a real signing identity, notarisation, or a `.dmg`. `run-linux` writes a
  `.desktop` file and does not install it -- putting a file in
  `~/.local/share/applications` is a change to the session that a build command
  should not make on its own -- and there is no `.deb`, `.rpm` or Flatpak.
  Windows writes a per-user Start Menu shortcut carrying an AppUserModelID, at
  startup rather than from the CLI, and has no installer, no `.msi` and no code
  signature.
- **A window cannot be positioned on Linux**, and that is the end of it rather
  than a gap. The rest of window geometry is done: `useWindow()` gives an app
  `setSize`, `setPosition`, `center`, `setFullScreen`, `minimize`,
  `toggleMaximize`, `close`, and live `bounds`. What is left is the part GTK4
  will not do at all -- `setPosition` and `center` are no-ops on Linux and
  `bounds.x` is always zero there, because `gtk_window_move` is gone and Wayland
  has no equivalent. The rest of the lifecycle is done: a close attempt is
  reported, so an app can ask "are you sure", and `setMinimumSize`,
  `setMaximumSize`, `setResizable` and `setAlwaysOnTop` are there with
  `capabilities` saying which of them this desktop actually does -- on Linux the
  maximum and always-on-top are `false`, because GTK4 removed both calls and
  Wayland has no protocol for either.
- **A window's lifecycle is an app's to influence now** -- `useCloseRequest()`
  and `<Window onCloseRequest>` refuse the close and report the attempt, which
  is where "are you sure" goes. So is how big it may be -- a minimum, a maximum,
  a resizable flag and always-on-top, with `capabilities` answering which of
  those this desktop does. What is left of the lifecycle is **quitting**, which
  is a different event from closing a window: Cmd-Q, and the Windows and GNOME
  session-end signals, each need their own seam. Proposed, as
  `openspec/changes/refuse-to-quit`.
  The title can be influenced too, on Windows, with the title bar's colours
  and a hidden style that lets the app draw its own header -- `useTitleBar`,
  `<TitleBar>`, `<TitleBar.DragRegion>` and `useTitleBarMetrics` in
  react-native-basalt. Linux and macOS ignore those calls until their hosts
  implement them.
- **Menus have no checkbox or radio items, and no dynamic enabling.** The
  application menu itself is done where a platform has one: `<Menu>`
  with `<Menu.Item role="copy" />`, over NSMenu and an HMENU. `Menu.isSupported`
  is false on Linux and is not a gap -- GNOME's guidelines have said to use a
  header bar with a menu button since GNOME 3, and GTK4 removed the widget.
  Context menus are done too, on all three -- `useContextMenu().show(items,
  where)`, which answers with the index chosen or null. That one is *more*
  portable than the menu bar rather than less: a popup is something every
  desktop has always had, including the one with no menu bar.

  A right-click opens one now, on all three: a secondary click arrives as
  `onPointerDown` with `button === 2` and does *not* fire `onPress`, which is
  what it means on every desktop. See core/PointerButtons.h.

  What is left: no checkbox or radio items; no submenus in a popup, which is
  deliberate rather than missing; no dynamic enabling without re-rendering the
  whole menu; and the role labels are English, because nothing here is
  localised.
- ~~**Native file dialogs.**~~ Done on all three: `useDialog().openFile()`,
  `saveFile()` and `openFolder()`, over `GtkFileDialog`, `NSOpenPanel` /
  `NSSavePanel` and `IFileDialog`. What is left is the rest of what a desktop
  calls a dialog -- a message box with arbitrary buttons (`Alert` is limited to
  three on Windows, and has no text field on Linux; see the core modules
  section), and no API for a print or colour dialog.
- **A system tray icon.** Half done, and not as a feature: `Win32Notifications.cpp`
  owns a hidden one because `Shell_NotifyIcon` needs an icon to notify from.
  What an app cannot do is put its own icon there, give it a tooltip, a menu or
  a click handler, which is what the feature would be. Nothing equivalent exists
  on GTK or AppKit.
- **Windows notifications carry no identity of their own.** `Shell_NotifyIcon`
  shows one at a time per icon and replaces whatever was there, so
  `dismissNotification` can only take down the current one and
  `presentedNotifications` answers what this process last asked for. A person
  who has already dismissed one still sees it counted as presented. The fix is
  `ToastNotificationManager`, which means WinRT, a manifest and a registered
  activator COM class.
- **Cursor control** beyond what the `cursor` style property covers: setting a
  busy cursor for the window, hiding it, and capturing it to a region.

### The rest of the surface, catalogued

Everything above grew out of something the demo or an app needed, which is why
it is mostly windows, menus and dialogs: those are what came up. This is the
other direction -- Electron's main-process surface and the consent dialogs a
desktop actually shows, gone through one at a time and checked against the
repository rather than remembered. Status is *done*, *partial* or *absent*, and
partial always says which half.

Nothing here is scheduled, and the three entries that carry a pointer are
proposed rather than started. It is here so that "what is missing" is an answer
rather than a search.

Where these land when they are built is its own question, and a decided one:
`openspec/changes/split-optional-capabilities-into-packages` draws the line
between what core owes every app and what belongs in a capability package. It
was written against this catalogue, so a new entry here is also a new entry for
that boundary to place.

- **Application lifecycle** -- *absent*, all of it.
  - **Quitting cannot be refused.** The gap `useCloseRequest()` leaves: an app can
  guard every window and still lose data to Cmd-Q, which goes through
  `applicationShouldTerminate:` and never asks a window whether it minds. The
  Windows and GNOME session-end signals (`WM_QUERYENDSESSION`, the session
  manager's) are the same question. Each needs its own seam; the flag machinery
  in core/WindowHost.h is the shape to copy. Proposed, as
  `openspec/changes/refuse-to-quit`.
  - **No single-instance lock.** A second launch starts a second process. Every
  desktop expects the first to be raised and handed the arguments instead --
  which is also how a file association or a URL reaches a running app.
  - **No launch at login**, no recent-documents list, no dock or taskbar badge,
  no jump list, and no standard About panel.


- **Drag and drop** -- *absent*, in both directions. The largest single item here:
  it needs a drop-target seam, hit testing against the drag position, and a
  representation for what is being dragged, over `GtkDropTarget`,
  `NSDraggingDestination` and OLE's `IDropTarget`. Dragging *out* is the half
  people forget and the half a file manager needs. Proposed, as
  `openspec/changes/add-drag-and-drop`.

- **Clipboard** -- *partial*. Text works, through React Native's own `Clipboard`.
  Images, HTML, RTF and a list of files are each a separate pasteboard type on
  each platform, and none is carried.

- **Shell integration** -- *partial*. Opening a URL works (`Linking`). Opening a
  path with its default application, revealing a file in the file manager, and
  moving one to the trash do not. Custom URL schemes are declared by
  `app.identity.json` and `Linking.getInitialURL()` answers on a cold start, but a
  URL delivered to an app that is *already running* is not reported -- which needs
  the single-instance lock above to be anywhere to deliver it to.

- **Displays and screen** -- *partial, and not exposed at all*. The window controls
  already ask about the display for `center()` and full screen -- `NSScreen`,
  `MonitorFromWindow` -- so the platform knows. An app does not: there is no
  display list, no per-display scale factor or work area, no pointer position, and
  no event when a monitor is plugged in or the arrangement changes. React Native's
  `Dimensions` reports the window, which is the right answer to a different
  question. Proposed, as `openspec/changes/expose-displays`.

- **Power and idle** -- *absent*. Suspend, resume, lock, unlock, on-battery and
  battery level; how long the person has been idle; and asking the system not to
  sleep while something is running. The last is the one a media or build app needs
  and cannot fake.

- **Global shortcuts** -- *absent*. A menu accelerator works while the app is
  focused, which is a different thing from a shortcut that works when it is not.

- **Permissions** -- *absent as a concept*, which matters more than any single one
  of them. macOS gates the camera, the microphone, screen recording, location,
  accessibility and full disk access behind TCC, and each needs a usage string in
  `Info.plist` *and* a request at runtime; Windows has its own capability prompts.
  Today `cli/packageApp.js` writes no usage strings and nothing asks for anything,
  so an app that reaches for a camera is denied without a prompt. Notifications
  are the one consent flow that works, and only because expo-notifications drove
  it. What is missing is the seam -- "ask for X, tell me the answer, tell me when
  it changes" -- rather than any particular permission.

- **Secure storage** -- *absent*. No Keychain, Credential Manager or libsecret, so
  an app storing a token has nowhere but a file.

- **Auto-update** -- *absent*, and worth deciding rather than building: it is
  Sparkle on macOS, MSIX or a custom updater on Windows, and the package manager
  on Linux, which is three answers rather than one API.

- **Printing** -- *absent*. No print dialog and no page rendering.

- **File system beyond the dialogs** -- *absent*. No watching a directory, and no
  security-scoped bookmarks, which is how a sandboxed macOS app keeps access to a
  file the person chose last week.

- **Screen capture and media devices** -- *absent*. No display or window capture,
  no camera or microphone enumeration.

- **Crash reporting** -- *absent*. A host that segfaults leaves an `.ips` on macOS
  and nothing an app or its author sees.

  The eight below came from reading Electron's own contents page against this
  list, which is worth doing once more than never: each is something a desktop
  app routinely does and none of them had been written down.

- **Window state is not remembered between launches** -- *absent*. Every desktop
  app reopens where it was, the size it was, maximised if it was. `useWindow()`
  has every piece needed to do it by hand and no app should have to.
- **No progress in the taskbar or dock** -- *absent*. A determinate or
  indeterminate bar on the dock icon or the taskbar button, which is what a
  download or an export is expected to show.
- **A window cannot represent a file** -- *absent*. macOS puts a proxy icon and
  an edited dot in the title bar for the document a window is showing; Windows
  conventionally marks the title. An editor wants both.
- **No vibrancy, transparency or window shape** -- *absent*. Translucent
  material behind content is how a native macOS sidebar looks, and a
  transparent or shaped window is how anything that is not a rectangle is
  drawn.
- **No online and offline detection** -- *absent*. Whether the machine has a
  network, and an event when that changes. React Native's own answer is
  NetInfo, which is a community package rather than core.
- **No spell checking** -- *absent* as a service. `spellCheck` on a
  `<TextInput>` is listed under TextInput as an unimplemented prop; the desktop
  version is larger -- a dictionary, a language, and a context menu of
  corrections, which is what the platform's own text controls do.
- **No dock, launcher or jump-list menu** -- *absent*. The menu a desktop shows
  when you press and hold the icon: recent documents, and the two or three
  actions an app wants offered before it is even running.
- **No way to do work off the JavaScript thread** -- *absent*. Electron has
  `utilityProcess` and MessagePorts. Here the equivalent is a worklet runtime or
  a second JSI runtime, and Reanimated already brings one -- so this is about
  whether an app can use it for its own work, not about building one.
