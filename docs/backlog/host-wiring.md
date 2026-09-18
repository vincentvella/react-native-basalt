# Host wiring

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. Scheduler::reportMount is never called
2. IDevUIDelegate / LogBox: JS errors currently go to g_warning and nothing else,
3. Dev support is tied to the dev server, so an offline `__DEV__` bundle cannot run
4. TurboModules the demo's own JavaScript asked for and did not get, none fatal t
5. src/LinuxNetworking
6. The linux platform redirects nine React Native shims to their 
7. Nothing checks that the shim list in metro-config

- `Scheduler::reportMount` is never called. It only drives mount hooks (perf
  tooling, Fantom's test observation), so nothing renders wrongly without it,
  but a real host reports. Needs the mounting manager to hold a
  `SchedulerTaskExecutor`, as `TesterAppDelegate` does.
- `IDevUIDelegate` / LogBox: JS errors currently go to `g_warning` and nothing
  else, so a mistake in an app is a log line and a window that keeps sitting
  there. Cheaper than it sounds: ReactCxxPlatform already *has* a `LogBox`
  module and only declines to hand it over because this host passes a null
  `logBoxSurfaceDelegate`. The work is supplying that delegate and a surface to
  render into, most likely a second GTK window, not writing an error overlay.
- **Dev support is tied to the dev server, so an offline `__DEV__` bundle cannot
  run.** `ReactCxxTurboModuleProvider` serves `DevSettings` only when a
  `DevServerHelper` exists, and LogBox reads that module at import time, so a
  `--dev` bundle loaded from disk throws before React renders. Android and iOS
  do not work this way: a debug build has dev support whether or not Metro is
  reachable. It costs nothing today because development goes through Metro,
  where it works; it would matter for a debuggable build shipped without a
  packager. Fixing it means providing the module ourselves rather than relying
  on ReactCxxPlatform's condition.
- TurboModules the demo's own JavaScript asked for and did not get, none fatal
  today: `BlobModule`, `DeviceEventManager`, `SoundManager`, `IntentAndroid`,
  `RedBox`, `ReactDevToolsSettingsManager`. That is a record of one run rather
  than a list of what to build; for the APIs this platform actually owes an
  implementation, see "Core modules this platform does not provide" below.
- `src/LinuxNetworking.cpp` supports only string request bodies. Blob, form-data
  and base64 need a Blob implementation first.
- The `linux` platform redirects nine React Native shims to their `.android.js`
  siblings. Each is a place this platform could diverge, and a place upstream
  could change under it; only `Platform` diverges today.
- Nothing checks that the shim list in `metro-config.js` still matches
  React Native. A new shim upstream shows up as an undefined export at runtime.
