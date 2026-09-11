// The component registry for the GTK platform. See core/ComponentRegistry.h for
// why this is per-platform rather than shared.

#include "ComponentRegistry.h"
#include "ExpoImageComponent.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/image/ImageComponentDescriptor.h>
#include <react/renderer/components/iostextinput/TextInputComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
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
      // Text is three descriptors, and only one of them mounts. <Text> becomes
      // a Text node, its string a RawText node, and the outermost <Text>
      // becomes a Paragraph that folds the whole subtree into a single
      // AttributedString. Only Paragraph has a widget.
      registry->add(concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<TextComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<RawTextComponentDescriptor>());
      // Image's descriptor pulls an ImageManager out of the ContextContainer,
      // creating one if absent. React Native's cxx ImageManager is a stub, so
      // it produces no pixels; GtkImageLoader does that from the props instead.
      registry->add(concreteComponentDescriptorProvider<ImageComponentDescriptor>());
      // Unlike Image's, ScrollView's descriptor is a bare alias for
      // ConcreteComponentDescriptor -- no manager, no ContextContainer entry.
      // Its content child is a plain View, so nothing else is needed. Leaving
      // it out does not fail loudly: the registry silently substitutes
      // UnimplementedNativeView, which has no ScrollViewState, and the symptom
      // is a ScrollView that renders but never scrolls.
      registry->add(concreteComponentDescriptorProvider<ScrollViewComponentDescriptor>());
      // React Native's *iOS* TextInput, whose C++ is portable: it measures
      // through a TextLayoutManager, which here is the Pango one. Android's
      // needs fbjni. Its component name is "TextInput".
      registry->add(concreteComponentDescriptorProvider<TextInputComponentDescriptor>());
      // expo-image's view. Registered whether or not the build has Expo in it:
      // the descriptor is ordinary Fabric C++, and an app that never renders
      // one pays a registry entry. See core/ExpoImageComponent.h.
      registry->add(concreteComponentDescriptorProvider<ExpoImageComponentDescriptor>());
      return registry;
    }();
    return providerRegistry->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
  };
}

} // namespace facebook::react
