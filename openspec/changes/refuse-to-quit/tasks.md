# Tasks

## 1. The seam

- [x] Add quit interception to `core/WindowHost.h`, following `setHostWindowCloseIntercepted`
- [x] Emit a device event for the attempt, and a way to answer it -- plus
      `quitHost()` and a `quit()` method, since an app that intercepted the
      quit refused the one the system asked for and needs a way to ask again

## 2. Hosts

- [x] AppKit: answer `applicationShouldTerminate:` -- with `NSTerminateCancel`
      rather than the `NSTerminateLater` this task named. Later obliges the app
      to call `replyToApplicationShouldTerminate:` from the JavaScript thread
      while AppKit holds a modal run loop; an app that is slow, or whose
      handler threw, leaves a process nothing but kill can end. Cancel is a
      complete answer now, and the app quits itself when ready -- which is the
      arrangement the window half already uses
- [ ] Win32: answer `WM_QUERYENDSESSION`, and keep Alt+F4 on the main window working
- [x] GTK: hook the session manager -- `GtkApplication::query-end`, taking a
      `gtk_application_inhibit` cookie from inside the handler, which is what
      GTK's own documentation for that signal says to do; answering alone does
      not hold the session. **What it cannot do:** there is no application-level
      quit gesture on GTK to intercept. A GTK app ends when its last window
      closes, and that is a close-request `useCloseRequest` already guards, so
      session end is the whole of the difference between the two halves here

## 3. JavaScript and tests

- [x] `useQuitRequest(handler)`, exported from `react-native-basalt`, with
      `quit()` beside it
- [x] Ensure `BASALT_QUIT_AFTER_MS` bypasses the interception. It did not:
      that timer quits by calling `terminate:`, which arrives at the new
      handler, so the demo would have refused the harness and every scenario
      running it would have hung until its own timeout. GTK and Win32 need no
      exemption -- their session signals are not on the path `g_application_quit`
      and `PostQuitMessage` take
- [x] Demo in `js/quit.js`, scenario asserting a refused quit and an agreed
      one, plus `BASALT_TEST_QUIT` on both hosts -- a count rather than a flag,
      because one ask cannot show both halves. Checked against the bug: with
      the interception removed the scenario fails
