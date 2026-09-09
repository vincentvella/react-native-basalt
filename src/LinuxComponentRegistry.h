// The component registry for the Linux platform.
//
// `getDefaultComponentRegistryFactory()` is declared by ReactCommon but
// deliberately *not* defined there -- no .cpp in the whole tree implements it.
// Each host supplies its own, and in doing so declares which components its
// platform actually supports. Fantom has its own version registering the full
// set; ours registers only what has a GTK peer.

#pragma once

#include <react/renderer/componentregistry/ComponentDescriptorFactory.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>

namespace facebook::react {

inline ComponentRegistryFactory getDefaultComponentRegistryFactory() {
  return [](const EventDispatcher::Weak &eventDispatcher,
            const std::shared_ptr<const ContextContainer> &contextContainer) {
    static auto providerRegistry = []() {
      auto registry = std::make_shared<ComponentDescriptorProviderRegistry>();
      registry->add(concreteComponentDescriptorProvider<ViewComponentDescriptor>());
      // Text is three descriptors, and only one of them mounts. <Text> becomes
      // a Text node, its string a RawText node, and the outermost <Text>
      // becomes a Paragraph that folds the whole subtree into a single
      // AttributedString. Only Paragraph has a widget.
      registry->add(concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<TextComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<RawTextComponentDescriptor>());
      // TODO: Image once IImageLoader is implemented; ScrollView once there is
      // a GtkScrolledWindow peer.
      return registry;
    }();
    return providerRegistry->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
  };
}

} // namespace facebook::react
