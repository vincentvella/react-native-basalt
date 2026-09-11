// The component registry for the macOS platform. See core/ComponentRegistry.h
// for why this is per-platform rather than shared.
//
// `<View>` and `<Text>`, and the shortness is still the honest part. Every
// descriptor left out is a piece of work with a name: Image needs an image
// loader, ScrollView a clipping scroller, TextInput an NSTextField peer.
//
// Paragraph arrived with CoreTextLayoutManager.mm, and could not have arrived
// before it: registering it constructs a `TextLayoutManager`, whose stub this
// build drops so a platform's own can be the only definition. That is the link
// error phase 19 found, and it is what kept this list at one entry.
//
// Registering them early would cost nothing visible and be worse than the gap.
// A registered descriptor with no mounting peer does not error -- Fabric builds
// the shadow nodes, lays them out, and the app renders blank rectangles where
// its text should be. Leaving them out means React Native substitutes
// UnimplementedNativeView, which at least says so.

#include "ComponentRegistry.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>

namespace facebook::react {

ComponentRegistryFactory getDefaultComponentRegistryFactory() {
  return [](const EventDispatcher::Weak &eventDispatcher,
            const std::shared_ptr<const ContextContainer> &contextContainer) {
    static auto providerRegistry = []() {
      auto registry = std::make_shared<ComponentDescriptorProviderRegistry>();
      registry->add(concreteComponentDescriptorProvider<ViewComponentDescriptor>());
      // Text is three descriptors and only one of them mounts. <Text> becomes a
      // Text node, its string a RawText node, and the outermost <Text> a
      // Paragraph that folds the whole subtree into a single AttributedString.
      registry->add(concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<TextComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<RawTextComponentDescriptor>());
      return registry;
    }();
    return providerRegistry->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
  };
}

} // namespace facebook::react
