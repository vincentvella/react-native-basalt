# Design

## Context

See proposal.md - Why. The constraint that shapes this is that drag and drop is
its own protocol on each toolkit, negotiated with the window system, rather than
anything that can be built from the press and move events this platform already
delivers.

## Goals / Non-Goals

- Goal: one seam, three implementations, with the same payload model.
- Goal: files and plain text, which every desktop agrees on.
- Non-Goal: dragging between two windows of this application, which is the same
  protocol but needs the per-window work in `plan/backlog/desktop-capabilities.md`.
- Non-Goal: custom drag imagery beyond what each toolkit gives by default.

## Decisions

**Registration per view rather than a global handler.** The mounting managers
already keep per-tag bookkeeping for hover listeners, and the drop target has to
be found by hit testing in exactly the same way -- so it follows the shape that
is there rather than introducing a second one.

**The payload is a list of typed items.** Each toolkit models this differently
-- a `GdkContentProvider`, an `NSPasteboard`, an `IDataObject` -- and a list of
`{type, value}` is the smallest thing all three can carry without inventing a
format none of them has.

## Risks / Trade-offs

- A synthetic drag cannot be injected the way a tap can: the window system is a
  participant. The test instrument will have to enter at the drop target seam,
  which means the scenario proves routing and payload handling rather than the
  toolkit's own negotiation.
- OLE requires the thread to be an STA and `RegisterDragDrop` per window, which
  is per-window state the Win32 host does not yet keep for anything else.

## Open Questions

- Does a drop on a second window need to work before per-window geometry does?
