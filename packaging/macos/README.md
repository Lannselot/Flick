# macOS release and verification

Flick supports macOS 13 or newer with Qt 6.5 or newer. Release bundles are
universal and must contain both `arm64` and `x86_64` slices.

Configure, build, install, and inspect an unsigned local candidate with:

```sh
cmake -S . -B build/macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DBUILD_TESTING=ON
cmake --build build/macos
ctest --test-dir build/macos --output-on-failure
cmake --install build/macos --prefix stage
codesign --force --deep --sign - stage/Flick.app
packaging/macos/verify-bundle.sh stage/Flick.app
```

Tagged releases use the protected `release-macos` GitHub Environment. Its
secrets provide the Developer ID Application certificate, signing identity,
temporary keychain password, and App Store Connect notarization key. The
workflow signs with the hardened runtime and secure timestamp, notarizes,
staples, runs Gatekeeper assessment, attests, and publishes the verified zip.

## Real-device release matrix

Retain results for each release. Run the matrix on Apple Silicon and supported
Intel hardware with mouse and trackpad, light and dark mode, Retina scaling,
fullscreen/Spaces, clipboard, drag-and-drop, native Open and Settings panels,
Finder double-click/Open With/reveal, settings relaunch, directory mutation,
errors, large-image confirmation, and every supported static and animated
format. On single and dual wide-gamut displays, compare tagged and untagged
fixtures with Preview and confirm that moving the window refreshes color
without another decode.

The hosted CI job is build and packaging evidence, not a substitute for this
real-device matrix or release credentials.
