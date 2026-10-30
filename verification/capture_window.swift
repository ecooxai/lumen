import Foundation
import CoreGraphics
import AppKit

// Capture only a window owned by the explicitly supplied test process.
// The harness supplies neutral demo/public pages; never capture the desktop.
let args = CommandLine.arguments
if args.count == 2 && args[1] == "--check" {
    let session = CGSessionCopyCurrentDictionary() as? [String:Any] ?? [:]
    func flag(_ keys: [String]) -> Any {
        for key in keys { if let value = session[key] as? NSNumber { return value.boolValue } }
        return NSNull()
    }
    let status: [String:Any] = ["screenCaptureAccess":CGPreflightScreenCaptureAccess(),
        "sessionDictionaryAvailable": !session.isEmpty,
        "screenLocked":flag(["CGSSessionScreenIsLocked"]),
        "onConsole":flag(["kCGSessionOnConsoleKey"]),
        "loginComplete":flag(["kCGSessionLoginDoneKey"])]
    let data = try JSONSerialization.data(withJSONObject:status,options:[.sortedKeys])
    FileHandle.standardOutput.write(data);FileHandle.standardOutput.write(Data("\n".utf8));exit(0)
}
func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    exit(1)
}
func allowedApp(_ app:NSRunningApplication?) -> Bool {
    guard let app = app, !app.isTerminated, let name = app.executableURL?.lastPathComponent.lowercased() else { return false }
    return name == "lumen" || (name == "google chrome" && app.bundleIdentifier == "com.google.Chrome")
}
func visibility(_ pid:Int32) -> [String:Any] {
    let all = CGWindowListCopyWindowInfo([.optionAll,.excludeDesktopElements],kCGNullWindowID) as? [[String:Any]] ?? []
    let windows = all.filter {
        let b = $0[kCGWindowBounds as String] as? [String:Any] ?? [:]
        return ($0[kCGWindowOwnerPID as String] as? NSNumber)?.int32Value == pid &&
            ($0[kCGWindowLayer as String] as? NSNumber)?.intValue == 0 &&
            (b["Width"] as? Double ?? 0) * (b["Height"] as? Double ?? 0) > 40_000
    }
    return ["pid":pid,"ownedWindowFound": !windows.isEmpty,
        "ownedWindowOnScreen": windows.contains { ($0[kCGWindowIsOnscreen as String] as? NSNumber)?.boolValue == true },
        "active": NSRunningApplication(processIdentifier:pid)?.isActive ?? false,
        "applicationHidden": NSRunningApplication(processIdentifier:pid)?.isHidden ?? false,
        "frontmostMatchesTarget":NSWorkspace.shared.frontmostApplication?.processIdentifier == pid,
        "inputEventsPosted":false,"permissionsChanged":false]
}
if args.count == 3 && (args[1] == "--hide-owned" || args[1] == "--unhide-owned") {
    guard let pid = Int32(args[2]), pid > 1, let app = NSRunningApplication(processIdentifier:pid), allowedApp(app) else {
        fail("Invalid owned browser PID")
    }
    let hiddenBefore = app.isHidden
    let hide = args[1] == "--hide-owned"
    let accepted = hide ? app.hide() : app.unhide()
    for _ in 0..<15 {
        if app.isHidden == hide { break }
        RunLoop.current.run(until:Date(timeIntervalSinceNow:0.1))
    }
    let result: [String:Any] = ["operation":hide ? "normal NSRunningApplication.hide" : "normal NSRunningApplication.unhide",
        "pid":pid,"accepted":accepted,"hiddenBefore":hiddenBefore,"hiddenAfter":app.isHidden,
        "visibility":visibility(pid),"inputEventsPosted":false,"permissionsChanged":false,
        "scope":"owned app hide/unhide; not minimization"]
    let data=try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys])
    FileHandle.standardOutput.write(data);FileHandle.standardOutput.write(Data("\n".utf8));exit(0)
}
if args.count == 3 && args[1] == "--inspect-owned" {
    guard let pid = Int32(args[2]), pid > 1, allowedApp(NSRunningApplication(processIdentifier:pid)) else { fail("Invalid owned browser PID") }
    let data=try JSONSerialization.data(withJSONObject:visibility(pid),options:[.sortedKeys])
    FileHandle.standardOutput.write(data);FileHandle.standardOutput.write(Data("\n".utf8));exit(0)
}
if args.count == 3 && args[1] == "--activate-owned" {
    guard let pid = Int32(args[2]), pid > 1 else { fail("Invalid owned PID") }
    var owned: NSRunningApplication? = nil
    for _ in 0..<30 {
        owned = NSRunningApplication(processIdentifier:pid)
        if allowedApp(owned) { break }
        RunLoop.current.run(until:Date(timeIntervalSinceNow:0.1))
    }
    guard let app = owned, allowedApp(app) else {
        fail("Activation target must be the supplied running Lumen or Google Chrome process")
    }
    let activeBefore = app.isActive
    let accepted = app.activate(options:[.activateIgnoringOtherApps])
    for _ in 0..<20 {
        if app.isActive { break }
        RunLoop.current.run(until:Date(timeIntervalSinceNow:0.1))
    }
    let result: [String:Any] = ["operation":"normal NSRunningApplication activation", "pid":pid,
        "accepted":accepted,"activeBefore":activeBefore,"activeAfter":app.isActive,
        "frontmostMatchesTarget":NSWorkspace.shared.frontmostApplication?.processIdentifier == pid,
        "visibility":visibility(pid),"inputEventsPosted":false,"permissionsChanged":false]
    let data = try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys])
    FileHandle.standardOutput.write(data);FileHandle.standardOutput.write(Data("\n".utf8));exit(0)
}
guard args.count == 3, let pid = Int32(args[1]), pid > 1 else {
    fail("Usage: capture-window PID output.png")
}
let target = URL(fileURLWithPath: args[2]).standardizedFileURL
let outputRoot = URL(fileURLWithPath: FileManager.default.currentDirectoryPath).appendingPathComponent("out/verification/").standardizedFileURL.path + "/"
guard target.path.hasPrefix(outputRoot), target.pathExtension == "png" else {
    fail("Output must be a PNG beneath out/verification")
}
guard let windows = CGWindowListCopyWindowInfo([.optionAll, .excludeDesktopElements], kCGNullWindowID) as? [[String:Any]] else {
    fail("Window list unavailable")
}
func area(_ window: [String:Any]) -> Double {
    let bounds = window[kCGWindowBounds as String] as? [String:Any] ?? [:]
    return (bounds["Width"] as? Double ?? 0) * (bounds["Height"] as? Double ?? 0)
}
let owned = windows.filter {
    ($0[kCGWindowOwnerPID as String] as? NSNumber)?.int32Value == pid &&
    ($0[kCGWindowLayer as String] as? NSNumber)?.intValue == 0 && area($0) > 40_000
}.sorted { area($0) > area($1) }
guard let window = owned.first, let identifier = window[kCGWindowNumber as String] as? NSNumber else {
    fail("No ordinary content window owned by test PID")
}
let capture = Process()
capture.executableURL = URL(fileURLWithPath:"/usr/sbin/screencapture")
capture.arguments = ["-x", "-o", "-l", identifier.stringValue, target.path]
do { try capture.run(); capture.waitUntilExit() } catch { fail("Owned-window capture could not start: \(error.localizedDescription)") }
guard capture.terminationStatus == 0,
      let size = (try? FileManager.default.attributesOfItem(atPath:target.path)[.size]) as? NSNumber,
      size.intValue > 100 else {
    fail("Owned-window capture failed; check screen recording permission without changing it")
}
let result: [String:Any] = ["source":"macOS compositor owned-window capture", "testPID":pid,
    "windowID":identifier, "path":target.path, "bytes":size,
    "bounds":window[kCGWindowBounds as String] ?? [:], "onScreen":window[kCGWindowIsOnscreen as String] ?? NSNull(), "sharingState":window[kCGWindowSharingState as String] ?? NSNull(), "alpha":window[kCGWindowAlpha as String] ?? NSNull(), "targetActive":NSRunningApplication(processIdentifier:pid)?.isActive ?? false, "frontmostMatchesTarget":NSWorkspace.shared.frontmostApplication?.processIdentifier == pid]
let data = try JSONSerialization.data(withJSONObject:result, options:[.sortedKeys])
FileHandle.standardOutput.write(data)
FileHandle.standardOutput.write(Data("\n".utf8))
