# Native Apple interaction regression

This standalone XCUITest harness drives the **installed custom Lumen browser**.
The tiny `LumenUIHarnessHost` application supplies UIKit's safe-area measurements;
it does not replace Lumen or render the browser fixture. No XcodeGen or third-party
test package is required.

Build Lumen's Simulator app using the repository Makefile, then run from the
repository root on a Mac with Xcode and an installed Simulator runtime:

```sh
bash ios/ui-tests/run.sh build-sim/Lumen.app SIMULATOR-UDID
```

Run once on an iPhone and once on an iPad. The launcher installs the supplied app,
starts a loopback fixture server, uses a unique profile directory inside Lumen's
own data container, and observes each Lumen process with
`tools/monitor_process.py` at one-second intervals for its entire test lifetime.
Only the selected installed binary path is matched. It terminates the test app
and its own fixture server at completion; it does not erase simulator contents.
Set `LUMEN_UI_TEST_PORT` if port 8789 is occupied. The Simulator's software
keyboard must be enabled for the keyboard-appearance assertion.

The test checks:

- Page viewport dimensions against UIKit safe insets, in portrait and landscape.
- Actual painted positions and real DOM clicks at the top and bottom of the page.
- One-finger scrolling and correct taps after rotation and foreground restoration.
- Software keyboard appearance, text reaching the page input, and dismissal on blur.
- Native workspace menu opening, iPad outside-popover dismissal, and a subsequent
  page click proving that the browser's event loop resumed.

The fixture uses distinct solid colors to locate its controls in real screenshots
because the custom-rendered DOM currently has no native accessibility elements.
Success is then checked against actual DOM events received by the fixture server.
The geometry checks compare native UIKit measurements, rendered pixels and page
JavaScript values; they do not assume fixed notch sizes.

Results, screenshots, browser-event JSON and CPU/RSS CSV/JSON are written under
`build-ui-tests/gpt-6-astra-chatgpt-xctest-<UTC timestamp>/`. Open `LumenUI.xcresult`
in Xcode to inspect screenshot attachments and assertion failures. Resource means
span navigation, interaction and orientation changes, so evaluate idle CPU from
the time series instead of calling the entire-run mean an idle measurement.
The launcher fails if successful UI assertions lack CPU/RSS evidence.

Portable fixture infrastructure checks:

```sh
python3 ios/ui-tests/test_fixture_server.py
```

The harness does not test Google account authorization, live ChatGPT messages,
YouTube hardware video decoding, pinch zoom, or real-device signing. Those need
their own tests and must not be inferred from this fixture.
