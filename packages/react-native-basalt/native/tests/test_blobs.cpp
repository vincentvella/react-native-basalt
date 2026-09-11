// Tests for the blob byte store.
//
// Core's, so compiled into both platforms' suites -- a blob is a byte array
// with a name everywhere, and there is nothing here a toolkit could change.
//
// What is *not* tested here is the module: createFromParts and readAsText take
// jsi values, which need a runtime, and the thing worth proving about them --
// that a Blob round-trips through JavaScript -- is proven by js/blob.js running
// on both hosts. This covers the half underneath, where an off-by-one lives.

#include "TestHarness.h"

#include "BlobRegistry.h"

#include <sstream>
#include <string>

namespace {

// Unique per test, so one test's leftovers cannot be another's fixture -- the
// registry is process-wide and nothing evicts from it.
std::string freshId(const char *name) {
  static int counter = 0;
  return std::string("test-") + name + "-" + std::to_string(++counter);
}

} // namespace

TEST(a_stored_blob_reads_back) {
  const auto id = freshId("roundtrip");
  basalt::storeBlob(id, "hello");

  EXPECT_EQ(static_cast<int>(basalt::blobSize(id)), 5);
  const auto bytes = basalt::blobBytes(id);
  EXPECT(bytes.has_value());
  EXPECT_EQ(bytes.value_or(""), std::string("hello"));

  basalt::releaseBlob(id);
}

TEST(an_unknown_blob_is_absent_rather_than_empty) {
  // The distinction matters: "" is a legal blob, and an app that asked for a
  // released one should be told so rather than handed nothing.
  EXPECT(!basalt::blobBytes("never-stored").has_value());
  EXPECT(!basalt::blobSlice("never-stored", 0, 10).has_value());
  EXPECT_EQ(static_cast<int>(basalt::blobSize("never-stored")), 0);
}

TEST(slicing_takes_the_part_asked_for) {
  const auto id = freshId("slice");
  basalt::storeBlob(id, "0123456789");

  EXPECT_EQ(basalt::blobSlice(id, 0, 3).value_or(""), std::string("012"));
  EXPECT_EQ(basalt::blobSlice(id, 2, 3).value_or(""), std::string("234"));
  EXPECT_EQ(basalt::blobSlice(id, 7, 3).value_or(""), std::string("789"));

  basalt::releaseBlob(id);
}

// JavaScript computes offsets from sizes it was told, so a disagreement should
// be a short read rather than an exception three layers from the cause.
TEST(a_slice_past_the_end_is_clamped_not_rejected) {
  const auto id = freshId("clamp");
  basalt::storeBlob(id, "abcde");

  // Runs off the end.
  EXPECT_EQ(basalt::blobSlice(id, 3, 100).value_or("!"), std::string("de"));
  // Starts past the end: empty, and still *present*, because the blob exists.
  const auto beyond = basalt::blobSlice(id, 50, 10);
  EXPECT(beyond.has_value());
  EXPECT_EQ(beyond.value_or("!"), std::string(""));

  basalt::releaseBlob(id);
}

TEST(releasing_removes_it) {
  const auto id = freshId("release");
  basalt::storeBlob(id, "temporary");
  EXPECT(basalt::blobBytes(id).has_value());

  basalt::releaseBlob(id);
  EXPECT(!basalt::blobBytes(id).has_value());

  // Twice is not an error: JavaScript's Blob.close() can be called again, and
  // a garbage collector may follow it.
  basalt::releaseBlob(id);
}

TEST(storing_twice_replaces) {
  const auto id = freshId("replace");
  basalt::storeBlob(id, "first");
  basalt::storeBlob(id, "second");

  EXPECT_EQ(basalt::blobBytes(id).value_or(""), std::string("second"));
  basalt::releaseBlob(id);
}

// Blobs are bytes, not text. A store that assumed UTF-8 or stopped at a NUL
// would corrupt every image an app ever fetched.
TEST(binary_content_survives) {
  const auto id = freshId("binary");
  const std::string bytes("\x00\x01\xFF\x00 tail", 9);
  basalt::storeBlob(id, bytes);

  EXPECT_EQ(static_cast<int>(basalt::blobSize(id)), 9);
  EXPECT(basalt::blobBytes(id).value_or("") == bytes);

  basalt::releaseBlob(id);
}
