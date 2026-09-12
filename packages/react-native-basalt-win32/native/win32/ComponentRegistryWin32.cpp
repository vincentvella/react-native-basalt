// Which components this platform claims.
//
// `getDefaultComponentRegistryFactory()` is declared by ReactCommon and defined
// nowhere in it: each host supplies its own and, in doing so, says what it can
// put on screen. Phase 19 found out the hard way that it cannot be shared --
// `ParagraphComponentDescriptor` constructs a `TextLayoutManager`, so a
// platform without a text engine fails to link rather than failing to render
// text. See `core/ComponentRegistry.h`.
//
// The set here must match what `Win32MountingManager::hasComponent` answers
// for. When they disagree the registry wins, Fabric builds shadow nodes nothing
// can mount, and the app renders blank rectangles rather than reporting
// anything -- which is why the two live in the same directory and should be
// read together.

#include "ComponentRegistry.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/image/ImageComponentDescriptor.h>
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

      // Text is three descriptors and only one of them mounts. <Text> becomes a
      // Text node, its string a RawText node, and the outermost <Text> a
      // Paragraph that folds the whole subtree into a single AttributedString.
      registry->add(concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<TextComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<RawTextComponentDescriptor>());

      // Image's descriptor pulls an ImageManager out of the ContextContainer,
      // creating one if absent. React Native's cxx ImageManager is a stub, so
      // it produces no pixels; the mounting manager loads them from the props,
      // which is what Android does too.
      registry->add(concreteComponentDescriptorProvider<ImageComponentDescriptor>());

      // ScrollView's content child arrives as "ScrollContentView", which
      // React Native's own registry rewrites to "View" before it reaches here,
      // so it needs no entry of its own. What the descriptor is really for is
      // ScrollViewState: without it the substituted UnimplementedNativeView has
      // nowhere to keep a content offset, so a ScrollView renders and never
      // scrolls -- silently, which is why the absence used to be worth naming.
      registry->add(concreteComponentDescriptorProvider<ScrollViewComponentDescriptor>());

      // Deliberately absent, and each absence is a decision rather than an
      // oversight:
      //
      //   TextInput   needs an EDIT peer and the controlled-value loop.
      //   ExpoImage   the seam exists in core/ExpoImageComponent.h and the
      //               props class is portable, but nothing here mounts one yet.
      //
      // See plan/backlog.md.
      return registry;
    }();

    return providerRegistry->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer});
  };
}

} // namespace facebook::react
