// Rendering a view tree to a PNG without showing it.
//
// Shared by the demo and the mount harness, and the only way either of them
// proves anything: a tree dump prints the frames that were *set*, which looks
// identical whether or not the coordinate system, the layer tree and the paint
// path are right. A picture does not.

#pragma once

#ifdef __OBJC__
#import "RnMacView.h"

// Renders `root` to a PNG at `path`. Returns false if the file could not be
// written. Needs no window server and shows nothing on screen.
bool RnMacWriteSnapshot(RnMacView *root, NSString *path);
#endif
