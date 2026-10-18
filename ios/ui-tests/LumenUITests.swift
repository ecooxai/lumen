import XCTest
import UIKit

final class LumenUITests: XCTestCase {
    private let browser = XCUIApplication(bundleIdentifier: "com.ecooxai.lumen")
    private let host = XCUIApplication()
    private var run = UUID().uuidString
    private var base: String { ProcessInfo.processInfo.environment["LUMEN_UI_TEST_BASE"] ?? "http://127.0.0.1:8789" }

    override func setUpWithError() throws {
        continueAfterFailure = false
        let profile = ProcessInfo.processInfo.environment["LUMEN_UI_TEST_PROFILE_ROOT"] ?? ""
        XCTAssertTrue(profile.hasPrefix("/"), "run.sh must supply the installed app's container profile path")
        browser.launchArguments = [base + "/fixture?run=" + run]
        browser.launchEnvironment["LUMEN_PROFILE_DIR"] = profile + "/" + run
        browser.launchEnvironment["LUMEN_NO_REPLAYKIT"] = "1"
        browser.launchEnvironment["LUMEN_DEBUG_GEOMETRY"] = "1"
    }

    override func tearDownWithError() throws {
        if browser.state == .runningForeground {
            attach("final-browser", windowScreenshot())
        }
        browser.terminate()
        host.terminate()
        XCUIDevice.shared.orientation = .portrait
    }

    private func windowScreenshot() -> XCUIScreenshot {
        // App screenshots on a rotated simulator can crop the portrait buffer
        // before attaching landscape orientation metadata. The actual window
        // preserves its full pixel buffer and still detects a terminated app.
        browser.windows.firstMatch.screenshot()
    }

    private func uprightImage(_ image: UIImage) -> UIImage {
        if image.imageOrientation == .up { return image }
        let format = UIGraphicsImageRendererFormat()
        format.scale = image.scale
        // UIImage.size and draw(in:) both honor its orientation. CGImage alone
        // does not, and PNG export otherwise loses that orientation metadata.
        return UIGraphicsImageRenderer(size: image.size, format: format).image { _ in
            image.draw(in: CGRect(origin: .zero, size: image.size))
        }
    }

    private func attach(_ name: String, _ shot: XCUIScreenshot) {
        let attachment = XCTAttachment(image: uprightImage(shot.image))
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func attachRaw(_ name: String, _ shot: XCUIScreenshot) {
        let attachment = XCTAttachment(screenshot: shot)
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func shotGeometry(_ shot: XCUIScreenshot) -> String {
        let image = shot.image
        let pixels = image.cgImage.map { "\($0.width)x\($0.height)" } ?? "none"
        let blue = findRect([0, 85, 204], screenshot: shot).map { NSCoder.string(for: $0) } ?? "none"
        let rawBlue = findRect([0, 85, 204], screenshot: shot, oriented: false).map { NSCoder.string(for: $0) } ?? "none"
        return "state=\(browser.state.rawValue) frame=\(NSCoder.string(for: browser.frame)) " +
            "imageSize=\(NSCoder.string(for: image.size)) scale=\(image.scale) " +
            "imageOrientation=\(image.imageOrientation.rawValue) cgPixels=\(pixels) " +
            "deviceOrientation=\(XCUIDevice.shared.orientation.rawValue) blue=\(blue) rawBlue=\(rawBlue)"
    }

    private func state() -> [String: Any] {
        let finished = expectation(description: "fixture state")
        var result: [String: Any] = [:]
        var request = URLRequest(url: URL(string: base + "/state?run=" + run)!)
        request.cachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        request.timeoutInterval = 2
        URLSession.shared.dataTask(with: request) { data, _, _ in
            if let data = data, let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] { result = value }
            finished.fulfill()
        }.resume()
        wait(for: [finished], timeout: 3)
        return result
    }

    @discardableResult
    private func awaitState(_ description: String, timeout: TimeInterval = 15, _ predicate: ([String: Any]) -> Bool) -> [String: Any] {
        let until = Date().addingTimeInterval(timeout)
        var last: [String: Any] = [:]
        repeat {
            last = state()
            if predicate(last) { return last }
            RunLoop.current.run(until: Date().addingTimeInterval(0.15))
        } while Date() < until
        XCTFail("Timed out waiting for \(description). Last fixture state: \(last)")
        return last
    }

    private func nativeMetrics(_ orientation: UIDeviceOrientation) -> [String: Double] {
        if host.state == .notRunning { host.launch() } else { host.activate() }
        XCUIDevice.shared.orientation = orientation
        let label = host.staticTexts["nativeSafeArea"]
        XCTAssertTrue(label.waitForExistence(timeout: 10))
        let until = Date().addingTimeInterval(10)
        repeat {
            if let data = label.label.data(using: .utf8),
               let value = try? JSONSerialization.jsonObject(with: data) as? [String: Double],
               let w = value["width"], let h = value["height"],
               (orientation.isLandscape ? w > h : h > w) { return value }
            RunLoop.current.run(until: Date().addingTimeInterval(0.15))
        } while Date() < until
        XCTFail("UIKit test host did not settle in the requested orientation")
        return [:]
    }

    private func checkViewport(_ m: [String: Double]) {
        let safeWidth = m["width"]! - m["left"]! - m["right"]!
        let sidebar: Double = safeWidth >= 1000 ? 84 : 0
        let width = safeWidth - sidebar
        let height = m["height"]! - m["top"]! - m["bottom"]! - 44
        awaitState("safe-area viewport \(width)x\(height)") { value in
            guard let w = value["width"] as? Double, let h = value["height"] as? Double else { return false }
            return abs(w - width) < 1.1 && abs(h - height) < 1.1
        }
    }

    // DOM is drawn by Lumen rather than represented by UIKit accessibility
    // elements. Locate our fixture's unique solid colors in the real screenshot,
    // then assert the real DOM event reported by the server after a native tap.
    private func findRect(_ rgb: [UInt8], screenshot shot: XCUIScreenshot, oriented: Bool = true) -> CGRect? {
        let cg = (oriented ? uprightImage(shot.image) : shot.image).cgImage!
        let width = cg.width, height = cg.height
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        pixels.withUnsafeMutableBytes { data in
            let context = CGContext(data: data.baseAddress, width: width, height: height, bitsPerComponent: 8,
                bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.draw(cg, in: CGRect(x: 0, y: 0, width: CGFloat(width), height: CGFloat(height)))
        }
        var x0 = width, y0 = height, x1 = -1, y1 = -1, count = 0
        for y in 0..<height {
            for x in 0..<width {
                let i = (y * width + x) * 4
                if abs(Int(pixels[i]) - Int(rgb[0])) <= 5 && abs(Int(pixels[i + 1]) - Int(rgb[1])) <= 5 && abs(Int(pixels[i + 2]) - Int(rgb[2])) <= 5 && pixels[i + 3] > 240 {
                    x0 = min(x0, x); y0 = min(y0, y); x1 = max(x1, x); y1 = max(y1, y); count += 1
                }
            }
        }
        if count <= 200 { return nil }
        let sx = browser.frame.width / CGFloat(width), sy = browser.frame.height / CGFloat(height)
        return CGRect(x: CGFloat(x0) * sx, y: CGFloat(y0) * sy, width: CGFloat(x1 - x0 + 1) * sx, height: CGFloat(y1 - y0 + 1) * sy)
    }

    private func rect(_ rgb: [UInt8], screenshot: XCUIScreenshot? = nil) -> CGRect {
        if let bounds = findRect(rgb, screenshot: screenshot ?? windowScreenshot()) { return bounds }
        XCTFail("Fixture color \(rgb) must be visible in the rendered browser")
        return .zero
    }

    private func awaitBrowserPaint(_ metrics: [String: Double]) -> XCUIScreenshot {
        // activate() can return while UIKit still displays the previous app's
        // transition snapshot. Wait for Lumen's actual settled fixture pixels.
        let until = Date().addingTimeInterval(5)
        let width = CGFloat(metrics["width"]!), height = CGFloat(metrics["height"]!)
        let safeWidth = metrics["width"]! - metrics["left"]! - metrics["right"]!
        let left = CGFloat(metrics["left"]! + (safeWidth >= 1000 ? 84 : 0) + 20)
        let top = CGFloat(metrics["top"]! + 44 + 24)
        var observations: [String] = []
        var previous = ""
        repeat {
            if browser.state == .runningForeground {
                let shot = windowScreenshot()
                if abs(browser.frame.width - width) <= 1.1 && abs(browser.frame.height - height) <= 1.1,
                   let painted = findRect([0, 85, 204], screenshot: shot),
                   abs(painted.minX - left) <= 2 && abs(painted.minY - top) <= 2 &&
                   abs(painted.width - 160) <= 2 && abs(painted.height - 56) <= 2 { return shot }
                let observation = shotGeometry(shot)
                if observation != previous { observations.append(observation); previous = observation }
            }
            RunLoop.current.run(until: Date().addingTimeInterval(0.15))
        } while Date() < until
        let shot = windowScreenshot()
        attach("browser-paint-timeout", shot)
        attachRaw("window-at-browser-paint-timeout-raw", shot)
        let appShot = browser.screenshot()
        attachRaw("app-at-browser-paint-timeout-raw", appShot)
        let screen = XCUIScreen.main.screenshot()
        attachRaw("screen-at-browser-paint-timeout-raw", screen)
        let details = "UIKit host metrics: \(metrics)\n" +
            "Expected blue: \(NSCoder.string(for: CGRect(x: left, y: top, width: 160, height: 56)))\n" +
            "Observed changes:\n\(observations.joined(separator: "\n"))\n" +
            "Final window screenshot: \(shotGeometry(shot))\n" +
            "App screenshot: \(shotGeometry(appShot))\n" +
            "Whole screen screenshot: \(shotGeometry(screen))\n\(browser.debugDescription)"
        let attachment = XCTAttachment(string: details)
        attachment.name = "browser-paint-geometry"
        attachment.lifetime = .keepAlways
        add(attachment)
        XCTFail("Lumen did not present the fixture at its native safe-area geometry within 5 seconds. \(details)")
        return shot
    }

    private func coordinate(_ point: CGPoint) -> XCUICoordinate {
        browser.coordinate(withNormalizedOffset: CGVector(dx: 0, dy: 0)).withOffset(CGVector(dx: point.x, dy: point.y))
    }

    private func tap(_ bounds: CGRect) {
        coordinate(CGPoint(x: bounds.midX, y: bounds.midY)).tap()
    }

    private func tapSoftwareKeyboard(_ text: String) {
        // SDL forwards a hidden UITextField's native input to the page. It can
        // show the keyboard without exposing AX keyboard focus for typeText().
        // Tap the real keys so this still verifies UIKit -> SDL -> DOM input.
        let keyboard = browser.keyboards.firstMatch
        for character in text {
            let label = character == " " ? "space" : String(character)
            let key = keyboard.keys[label].firstMatch
            if !key.exists && character.isNumber {
                // Full-size iPad keyboards may already expose a number row.
                // iPhone keyboards expose the 123 layout switch as "more".
                let switches = ["more", "numbers", "123"].map { keyboard.keys[$0].firstMatch }
                guard let numbers = switches.first(where: { $0.exists && $0.isHittable }) else {
                    XCTFail("The visible keyboard has no number-layout switch. \(keyboard.debugDescription)")
                    return
                }
                numbers.tap()
            }
            guard key.waitForExistence(timeout: 3) && key.isHittable else {
                XCTFail("The visible keyboard must expose key \(label). \(keyboard.debugDescription)")
                return
            }
            key.tap()
        }
    }

    func testSafeAreasTouchRotationKeyboardAndMenuDismissal() throws {
        let portrait = nativeMetrics(.portrait)
        browser.launch()
        awaitState("fixture startup") { $0["ready"] as? Bool == true }
        checkViewport(portrait)
        let initial = awaitBrowserPaint(portrait)
        attach("portrait-safe-area", initial)
        let top = rect([0, 85, 204], screenshot: initial)
        let bottom = rect([0, 107, 79], screenshot: initial)
        XCTAssertEqual(top.minY, CGFloat(portrait["top"]!) + 44 + 24, accuracy: 2)
        XCTAssertEqual(bottom.maxY, CGFloat(portrait["height"]! - portrait["bottom"]! - 20), accuracy: 2)
        tap(top)
        awaitState("portrait top target tap") { ($0["clicks"] as? [String])?.contains("tap") == true }
        tap(bottom)
        awaitState("portrait bottom target tap") { ($0["clicks"] as? [String])?.contains("bottom") == true }

        let from = coordinate(CGPoint(x: browser.frame.width * 0.75, y: browser.frame.height * 0.72))
        let to = coordinate(CGPoint(x: browser.frame.width * 0.75, y: browser.frame.height * 0.34))
        from.press(forDuration: 0.08, thenDragTo: to)
        tap(rect([0, 85, 204])) // The click report also reads the current page scrollY.
        awaitState("one-finger scroll") { ($0["scrollY"] as? Double ?? 0) > 100 }

        let landscape = nativeMetrics(.landscapeLeft)
        browser.activate()
        checkViewport(landscape)
        let rotated = awaitBrowserPaint(landscape)
        attach("landscape-safe-area", rotated)
        let landscapeTop = rect([0, 85, 204], screenshot: rotated)
        let safeWidth = landscape["width"]! - landscape["left"]! - landscape["right"]!
        XCTAssertEqual(landscapeTop.minX, CGFloat(landscape["left"]! + (safeWidth >= 1000 ? 84 : 0) + 20), accuracy: 2)
        let clicks = (state()["clicks"] as? [String] ?? []).count
        tap(landscapeTop)
        awaitState("tap after rotation") { ($0["clicks"] as? [String] ?? []).count > clicks }

        let restored = nativeMetrics(.portrait)
        browser.activate()
        checkViewport(restored)
        _ = awaitBrowserPaint(restored)
        tap(rect([255, 248, 192]))
        XCTAssertTrue(browser.keyboards.firstMatch.waitForExistence(timeout: 8), "The software keyboard must appear for the page field")
        tapSoftwareKeyboard("lumen ipad 123")
        awaitState("typed page input") { $0["value"] as? String == "lumen ipad 123" }
        attach("keyboard-input", windowScreenshot())
        tap(rect([0, 85, 204]))
        let hidden = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: browser.keyboards.firstMatch)
        XCTAssertEqual(XCTWaiter.wait(for: [hidden], timeout: 8), .completed, "Blurring the page field must hide the keyboard")

        // Recover page origin from our known fixture geometry instead of hardcoding
        // a particular iPhone notch. With one tab the toolbar is 44 points high.
        let visible = rect([0, 85, 204])
        let viewport = state()
        let plus = CGPoint(x: visible.minX - 20 + CGFloat(viewport["width"] as! Double) - 74, y: visible.minY - 24 - 22)
        coordinate(plus).press(forDuration: 0.75)
        XCTAssertTrue(browser.staticTexts["Workspace"].waitForExistence(timeout: 8))
        attach("workspace-menu", windowScreenshot())
        if portrait["pad"] == 1 {
            coordinate(CGPoint(x: 12, y: browser.frame.height * 0.5)).tap()
        } else {
            browser.buttons["Cancel"].firstMatch.tap()
        }
        let dismissed = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: browser.staticTexts["Workspace"])
        XCTAssertEqual(XCTWaiter.wait(for: [dismissed], timeout: 8), .completed)
        let before = (state()["clicks"] as? [String] ?? []).count
        tap(rect([0, 85, 204]))
        awaitState("browser resumes after native menu dismissal") { ($0["clicks"] as? [String] ?? []).count > before }
        attach("after-menu-dismissal", windowScreenshot())
    }
}
