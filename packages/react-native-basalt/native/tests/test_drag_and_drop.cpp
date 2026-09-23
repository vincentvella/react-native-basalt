// The parts of drag and drop that are a decision rather than a toolkit call.
//
// Which view is told, and whether it would take what is being dragged, are the
// same questions on three desktops and are answered here once. What each
// toolkit calls its drop target is not.

#include "TestHarness.h"

#include "DragAndDrop.h"

#include <sstream>
#include <string>

using basalt::DragPayload;
using basalt::DropAcceptsFiles;
using basalt::DropAcceptsNone;
using basalt::DropAcceptsText;

TEST(drop_an_unmarked_native_id_accepts_nothing) {
  // Every other nativeID an app sets, including the title bar's and its own.
  EXPECT_EQ((int)basalt::dropAcceptsFrom(""), (int)DropAcceptsNone);
  EXPECT_EQ((int)basalt::dropAcceptsFrom("my-view"), (int)DropAcceptsNone);
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-titlebar-drag"), (int)DropAcceptsNone);
  // Close, and not it: a prefix match must be a prefix.
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop"), (int)DropAcceptsNone);
}

TEST(drop_a_marker_says_what_it_takes) {
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:files"), (int)DropAcceptsFiles);
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:text"), (int)DropAcceptsText);
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:files,text"),
            (int)(DropAcceptsFiles | DropAcceptsText));
  // Order is the app's, not ours.
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:text,files"),
            (int)(DropAcceptsFiles | DropAcceptsText));
}

TEST(drop_an_unknown_kind_degrades_rather_than_refuses) {
  // A marker from a newer JavaScript than this host. Taking the kinds it
  // knows is the difference between a view that accepts files and one that
  // accepts nothing at all.
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:files,html"), (int)DropAcceptsFiles);
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:html"), (int)DropAcceptsNone);
  // And an empty list is a marker that accepts nothing, which is not a crash.
  EXPECT_EQ((int)basalt::dropAcceptsFrom("basalt-drop:"), (int)DropAcceptsNone);
}

TEST(drop_would_accept_asks_about_what_is_being_dragged) {
  DragPayload files;
  files.files.push_back("/tmp/one.txt");

  DragPayload text;
  text.text = "hello";

  DragPayload both;
  both.files.push_back("/tmp/one.txt");
  both.text = "hello";

  EXPECT(basalt::wouldAccept(DropAcceptsFiles, files));
  EXPECT(!basalt::wouldAccept(DropAcceptsFiles, text));
  EXPECT(basalt::wouldAccept(DropAcceptsText, text));
  EXPECT(!basalt::wouldAccept(DropAcceptsText, files));

  // Either kind is enough when a view takes both, and a view taking one still
  // takes a payload carrying both -- a file manager commonly offers a file as
  // a path and as its name.
  EXPECT(basalt::wouldAccept(DropAcceptsFiles | DropAcceptsText, files));
  EXPECT(basalt::wouldAccept(DropAcceptsFiles, both));
  EXPECT(basalt::wouldAccept(DropAcceptsText, both));
}

TEST(drop_nothing_is_accepted_by_nobody) {
  DragPayload nothing;
  EXPECT(!basalt::wouldAccept(DropAcceptsFiles | DropAcceptsText, nothing));
  // And a view that accepts nothing refuses a payload that has something,
  // which is the case an unmarked view hits on every drag that passes over it.
  DragPayload files;
  files.files.push_back("/tmp/one.txt");
  EXPECT(!basalt::wouldAccept(DropAcceptsNone, files));
}

TEST(drop_the_marker_round_trips) {
  // JavaScript writes these strings and C++ reads them, from two files that
  // cannot include each other. A round trip is what keeps them the same.
  for (std::uint16_t accepts : {(std::uint16_t)DropAcceptsFiles,
                                (std::uint16_t)DropAcceptsText,
                                (std::uint16_t)(DropAcceptsFiles | DropAcceptsText)}) {
    EXPECT_EQ((int)basalt::dropAcceptsFrom(basalt::dropTargetIdFor(accepts)), (int)accepts);
  }
}
