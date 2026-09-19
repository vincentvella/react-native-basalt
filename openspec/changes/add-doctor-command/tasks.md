# Tasks

## 1. Share what `init` already does

- [ ] Lift the step machinery -- the three states and the summary -- out of
      `cli/init.ts` so both commands report the same way
- [ ] Give each existing check a read-only form, which `init` already has for
      the Metro config and does not for the rest

## 2. Check the machine

- [ ] cmake, ninja and a compiler, per platform, from the list in
      `docs/PORTING.md`
- [ ] The system libraries each desktop needs: GTK4 development headers on
      Linux, the macOS SDK, vcpkg on Windows
- [ ] The app's React Native against `supported-versions.json`, reporting the
      version found and the versions supported
- [ ] Report a desktop that cannot be checked from here as unchecked, never as
      passing

## 3. The command

- [ ] `cli/doctor.ts`, as a second verb on the `react-native-basalt` bin
- [ ] Non-zero exit when anything is wrong, so CI can run it
- [ ] Have `release.yml`'s Expo job run it after `init`, where it would have
      caught the host packages being missing

## 4. Which desktops

- [x] `init` adds a host package per desktop, all three by default
- [x] `app.json`'s `basalt.desktops` narrows the list
- [x] An unrecognised name is reported and the full set used
- [x] Document the key in the README beside `init`

## 5. Records

- [ ] Update the `packaging` spec
- [ ] A line in `docs/DECISIONS.md` only if the toolchain checks turn out to
      need a rule; the packaging boundary already has one
