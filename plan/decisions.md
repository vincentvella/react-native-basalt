# Decisions

So they are not re-litigated.

## Path A over a Node-based reimplementation — 2026-09-08

**Rejected:** building on GTKX (`gtkx-org/gtkx`) / `react-native-gtkx`
(`itsmepetrov/react-native-gtkx`), which reimplement the RN API on Node with a
JS-side Yoga tree and no ReactCommon.

**Chosen:** a true out-of-tree platform embedding ReactCommon/Fabric/Hermes.

**Why:** the Node approach ships far sooner but yields a parallel ecosystem —
no existing RN native module works, ever. Path A costs much more up front and
gives a real porting path. Accepted cost: each native library still needs a
Linux backend written; "portable in principle" is not "works on day one".

## clang, not GCC — 2026-09-08

RN builds `-Wall -Werror -Wpedantic` and is a clang codebase. GCC fails on
`#pragma mark`, folly's `__int128` under `-Wpedantic`, and
`-Wsubobject-linkage` in `NetworkIOAgent`. Rather than paper over RN's own
warnings with `-Wno-*`, the project uses clang. Revisit only if GCC support
becomes a distribution requirement.

## Vendor folly/fast_float, use system glog/boost/fmt — 2026-09-08

folly is pinned to RN's exact version (`2024.11.18.00`) because RN builds a
trimmed subset of it and version skew is likely to hurt. glog and boost are
taken from the system despite RN pinning older versions, because the API
surface RN uses is stable — with one exception already hit, glog >= 0.6
requiring `GLOG_USE_GLOG_EXPORT`.

## RnLayout does no layout — 2026-09-08

Yoga resolves absolute frames before any mutation arrives, so `measure` returns
0 and `allocate` places children at their assigned rects. Letting GTK
participate in sizing would mean two layout systems disagreeing.
`react-native-gtkx` reached the same conclusion independently.

## The registry owns views between Remove and Delete — 2026-09-08

`g_object_ref_sink` on Create, `g_object_unref` on Delete. Fabric's Remove
detaches without destroying, and a view may be re-Inserted before its Delete
arrives, so parenting alone cannot own the lifetime.
