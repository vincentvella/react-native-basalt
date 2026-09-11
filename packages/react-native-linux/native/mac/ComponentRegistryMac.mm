// The component registry for the macOS platform. See core/ComponentRegistry.h
// for why this is per-platform rather than shared.
//
// One entry, and the shortness is the honest part. `<View>` is all that has an
// AppKit peer today, and every descriptor left out is a piece of work with a
// name: Paragraph needs a Core Text TextLayoutManager, Image an image loader,
// ScrollView a clipping scroller, TextInput an NSTextField peer.
//
// Registering them early would cost nothing visible and be worse than the gap.
// A registered descriptor with no mounting peer does not error -- Fabric builds
// the shadow nodes, lays them out, and the app renders blank rectangles where
// its text should be. Leaving them out means React Native substitutes
// UnimplementedNativeView, which at least says so.

#include "ComponentRegistry.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>

namespace facebook::react {

ComponentRegistryFactory getDefaultComponentRegistryFactory() {
  return [](const EventDispatcher::Weak &eventDispatcher,
            const std::shared_ptr<const ContextContainer> &contextContainer) {
    static auto providerRegistry = []() {
      auto registry = std::make_shared<ComponentDescriptorProviderRegistry>();
      registry->add(concreteComponentDescriptorProvider<ViewComponentDescriptor>());
      return registry;
    }();
    return providerRegistry->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
  };
}

} // namespace facebook::react
