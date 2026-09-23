# Tasks

## 1. The seam

- [x] Define the payload and drop-target types in `core/` as a header-only model
- [x] ~~Add the drop registration to `MountingWalk`, beside the hover
      listeners.~~ Not needed, and the change is better for it: a view marks
      itself with `nativeID`, which every host already stores and React
      already sends on every commit. No registration, no ref, no lifetime.
      Same idea as `core/TitleBarRegions.h`
- [x] Add hit testing for a drag position, reusing each host's own hit test
      -- `RnAppKitHitTest`, `gtk_widget_pick`, `hitTest` -- and then walking
      up to the nearest marked ancestor

## 2. GTK

Dragging *out* is deliberately left for later on all three hosts: it is the
harder direction, and it is the one a test cannot drive at all, since the
system owns the drag once it starts. Dropping *in* is the half the proposal
leads with -- "an app here cannot take a file from the file manager".


- [x] Attach a `GtkDropTarget` to the surface root and route its signals
- [ ] Implement dragging out with a `GdkContentProvider`

## 3. AppKit

- [x] Implement `NSDraggingDestination` -- on a view of its own above the
      surface root, not on `RnAppKitView`. Every view would otherwise be a
      destination and AppKit would ask the deepest, which is the opposite of
      the rule: the deepest *marked* view decides
- [ ] Implement dragging out with `NSPasteboardWriting`

## 4. Win32

- [x] Register an `IDropTarget` per window and route its methods, with
      `OleInitialize` in place of `CoInitializeEx` -- a thread that only called
      the latter can open a URL and cannot accept a drop
- [ ] Implement dragging out with `DoDragDrop`

## 5. JavaScript and tests

- [x] Expose the React API -- `<DropTarget>` -- and export it from
      `react-native-basalt`
- [x] Add a demo to `js/drop.js`: two targets, one inside the other, because
      the rule that can be wrong is which one is told
- [x] Add `BASALT_TEST_DROP` on both hosts, and a scenario asserting the
      innermost target is told and the outer one is not. Entered below the
      toolkit, like taps: a real drag needs a source outside the process
- [x] Update `docs/TESTING.md` and `docs/backlog/desktop-capabilities.md`


## 6. What building it found

- [x] AppKit's `hitTest:` is not this platform's hit test. It answers with the
      first subview that claims the point rather than the one the renderer
      means, so the walk started two steps below the root and found no marker
      above it. `RnAppKitHitTest` -- what touches use -- is the right one.
- [x] `<DropTarget>` never touched the native module, so `BasaltWindows` was
      never constructed, its constructor never registered the host listener,
      and every drop was found in C++ and reported into a null std::function.
      The log said "onto tag 18" and JavaScript heard nothing.
- [x] A host component's ref carries `__nativeTag`, not `_nativeTag`. The old
      name is simply undefined under the new renderer, and an event that
      matches nothing looks exactly like an event for another view.
- [x] AppKit stored no `nativeID` at all. GTK and Win32 did; nothing on that
      host had needed it.
