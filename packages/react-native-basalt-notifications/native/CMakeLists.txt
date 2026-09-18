# The notifications capability, contributed to whichever host is being built.
#
# A package does not build a library of its own: the host is one binary and
# everything native is compiled into it. So this appends to global properties
# the core and host CMakeLists read, which is the whole contract -- see
# core/PackageModules.h for why the registration is generated rather than
# resolved at static initialisation.

set(BASALT_NOTIFICATIONS_DIR ${CMAKE_CURRENT_LIST_DIR})

# Platform-independent: the Expo modules, compiled into basalt_core.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_CORE_SOURCES
  ${BASALT_NOTIFICATIONS_DIR}/ExpoNotificationModules.cpp)

# The header the seam lives in, so core and the hosts can include it.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_INCLUDES ${BASALT_NOTIFICATIONS_DIR})

# The installer the generated translation unit calls.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_EXPO_INSTALLERS installNotificationExpoModules)

# One implementation per desktop, each added to that host's target. A desktop
# with none would simply contribute nothing here and report that it cannot
# notify, which is the no-op-but-reportable rule.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_GTK_SOURCES
  ${BASALT_NOTIFICATIONS_DIR}/gtk/GtkNotifications.cpp)
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_SOURCES
  ${BASALT_NOTIFICATIONS_DIR}/appkit/AppKitNotifications.mm)
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_WIN32_SOURCES
  ${BASALT_NOTIFICATIONS_DIR}/win32/Win32Notifications.cpp)
