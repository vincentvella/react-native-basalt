// Does the shared core actually stand on its own?
//
// `basalt_core` compiles without a toolkit on the include path, which says
// nothing about whether it *links* without one. A header that is never included
// cannot betray you; an undefined symbol at link time can.
//
// So this is a program that links the core and nothing platform-specific, and
// touches each module the core provides. If it links, a new desktop platform
// can build the core as-is. If it does not, the missing symbols are exactly the
// list of what that platform has to implement -- which is the more useful
// outcome of the two, and the reason this prints them rather than asserting.
//
// It is not a test of behaviour. It never constructs a runtime and never
// renders anything. It answers one question: what does a second platform owe?

#include "AppearanceModule.h"
#include "ColorScheme.h"
#include "ComponentRegistry.h"
#include "ExpoRuntime.h"
#include "FontRegistry.h"
#include "PlatformConstantsModule.h"
#include "SourceCodeModule.h"
#include "StatusBarModule.h"

#include <cstdio>
#include <string>

// The seam, stubbed, so that the link answers the question fully rather than
// stopping at the first thing missing. A real platform implements these against
// fontconfig, Core Text or DirectWrite; a platform that has not yet gets this
// far and no further, which is the point.
namespace basalt {
bool registerFont(const std::string &, const std::string &) {
  return false;
}
std::string resolveFontFamily(const std::string &name) {
  return name;
}
bool isFontRegistered(const std::string &) {
  return false;
}
unsigned long fontGeneration() {
  return 0;
}
std::string registeredFontNamesJoined(char) {
  return {};
}

// The colour-scheme seam, stubbed the same way. A real platform answers from
// NSAppearance, GtkSettings or the Windows registry; one that has not yet gets
// this far and no further.
ColorScheme systemColorScheme() {
  return ColorScheme::Light;
}
void startObservingColorScheme() {}
} // namespace basalt

int main() {
  using basalt::DesktopPlatformConstantsModule;
  using basalt::DesktopAppearanceModule;
  using basalt::DesktopSourceCodeModule;
  using basalt::DesktopStatusBarModule;

  // Each of these is a symbol the linker must find in the core alone.
  const std::string url = basalt::scriptURLFor("bundle.js", false, "localhost", 8081, "index");
  const bool expo = basalt::hasExpoRuntime();

  // The seams: declared by the core, implemented by a platform. On a platform
  // that has not implemented them, the link fails here and names them.
  const bool registered = basalt::isFontRegistered("nothing");
  const unsigned long generation = basalt::fontGeneration();
  const char *scheme = basalt::colorSchemeName(basalt::effectiveColorScheme());

  // Named so the link has to find each module the core claims to provide.
  std::printf("core links.\n");
  std::printf("  modules           %s, %s, %s, %s\n",
              std::string(DesktopPlatformConstantsModule::kModuleName).c_str(),
              std::string(DesktopSourceCodeModule::kModuleName).c_str(),
              std::string(DesktopStatusBarModule::kModuleName).c_str(),
              std::string(DesktopAppearanceModule::kModuleName).c_str());
  std::printf("  scriptURLFor      %s\n", url.c_str());
  std::printf("  expo runtime      %s\n", expo ? "compiled in" : "not compiled in");
  std::printf("  font seam         isFontRegistered=%d generation=%lu\n",
              registered ? 1 : 0,
              generation);
  std::printf("  colour scheme     %s\n", scheme);
  return 0;
}
