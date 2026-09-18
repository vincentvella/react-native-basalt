# Tasks

## 1. The seam

- [ ] Define the payload and drop-target types in `core/` as a header-only model
- [ ] Add the drop registration to `MountingWalk`, beside the hover listeners
- [ ] Add hit testing for a drag position, reusing the hover hit chain

## 2. GTK

- [ ] Attach a `GtkDropTarget` to the surface root and route its signals
- [ ] Implement dragging out with a `GdkContentProvider`

## 3. AppKit

- [ ] Implement `NSDraggingDestination` on the root view and route it
- [ ] Implement dragging out with `NSPasteboardWriting`

## 4. Win32

- [ ] Register an `IDropTarget` per window and route its methods
- [ ] Implement dragging out with `DoDragDrop`

## 5. JavaScript and tests

- [ ] Expose the React API and export it from `react-native-basalt`
- [ ] Add a demo to `js/`
- [ ] Add a test instrument for a synthetic drag, and a scenario asserting it
- [ ] Update `docs/TESTING.md` and `plan/backlog/desktop-capabilities.md`
