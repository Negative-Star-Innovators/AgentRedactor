import AppKit
import ApplicationServices
import Foundation

enum HelperError: LocalizedError {
    case invalidArguments
    case accessibilityDenied
    case elementNotFound(String)
    case actionFailed(String, AXError)

    var errorDescription: String? {
        switch self {
        case .invalidArguments:
            return "Invalid helper arguments"
        case .accessibilityDenied:
            return "Accessibility permission is required for the test helper"
        case .elementNotFound(let description):
            return "Accessibility element not found: \(description)"
        case .actionFailed(let description, let error):
            return "\(description) failed with AXError \(error.rawValue)"
        }
    }
}

func attribute(_ element: AXUIElement, _ name: String) -> AnyObject? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success else {
        return nil
    }
    return value
}

func children(_ element: AXUIElement) -> [AXUIElement] {
    (attribute(element, kAXChildrenAttribute) as? [AXUIElement]) ?? []
}

func windows(_ application: AXUIElement) -> [AXUIElement] {
    (attribute(application, kAXWindowsAttribute) as? [AXUIElement]) ?? []
}

func application(pid: pid_t) -> AXUIElement {
    AXUIElementCreateApplication(pid)
}

func walk(_ element: AXUIElement, _ visit: (AXUIElement) -> Void) {
    visit(element)
    for child in children(element) {
        walk(child, visit)
    }
}

func find(_ roots: [AXUIElement], matching predicate: (AXUIElement) -> Bool) -> AXUIElement? {
    for root in roots {
        var found: AXUIElement?
        walk(root) { element in
            if found == nil && predicate(element) {
                found = element
            }
        }
        if let found { return found }
    }
    return nil
}

func findIdentifier(_ pid: pid_t, _ identifier: String) -> AXUIElement? {
    find(windows(application(pid: pid))) { element in
        attribute(element, kAXIdentifierAttribute) as? String == identifier
    }
}

func press(_ element: AXUIElement, _ description: String) throws {
    _ = AXUIElementPerformAction(element, "AXScrollToVisible" as CFString)
    let result = AXUIElementPerformAction(element, kAXPressAction as CFString)
    guard result == .success else { throw HelperError.actionFailed(description, result) }
}

func coreAuthApplication() -> AXUIElement? {
    let process = NSWorkspace.shared.runningApplications.first {
        $0.localizedName == "coreautha" || $0.bundleIdentifier == "com.apple.coreautha"
    }
    guard let pid = process?.processIdentifier else { return nil }
    return application(pid: pid)
}

func requireAccessibility() throws {
    guard AXIsProcessTrusted() else { throw HelperError.accessibilityDenied }
}

func activate(_ pid: pid_t) throws {
    guard let process = NSRunningApplication(processIdentifier: pid) else {
        throw HelperError.elementNotFound("process \(pid)")
    }
    process.activate(options: [.activateAllWindows])
}

func submitAuth() throws {
    guard let auth = coreAuthApplication(), let password = readLine(strippingNewline: true) else {
        throw HelperError.elementNotFound("Apple authentication window")
    }
    var pid: pid_t = 0
    AXUIElementGetPid(auth, &pid)
    try activate(pid)
    Thread.sleep(forTimeInterval: 0.1)

    let source = CGEventSource(stateID: .hidSystemState)
    for scalar in password.unicodeScalars {
        var units = Array(String(scalar).utf16)
        let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true)
        down?.keyboardSetUnicodeString(stringLength: units.count, unicodeString: &units)
        down?.postToPid(pid)
        let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false)
        up?.keyboardSetUnicodeString(stringLength: units.count, unicodeString: &units)
        up?.postToPid(pid)
        Thread.sleep(forTimeInterval: 0.01)
    }
    let returnDown = CGEvent(keyboardEventSource: source, virtualKey: 36, keyDown: true)
    returnDown?.postToPid(pid)
    let returnUp = CGEvent(keyboardEventSource: source, virtualKey: 36, keyDown: false)
    returnUp?.postToPid(pid)
}

func run(_ arguments: [String]) throws {
    guard arguments.count >= 2 else { throw HelperError.invalidArguments }
    try requireAccessibility()
    let command = arguments[1]
    var pid: pid_t = 0
    if ["wait-window", "window-count", "activate", "click", "click-title", "set-text", "select-popup", "exists", "close-window", "dump"].contains(command) {
        guard arguments.count >= 3, let parsed = Int32(arguments[2]) else {
            throw HelperError.invalidArguments
        }
        pid = parsed
    }

    switch command {
    case "wait-window":
        let deadline = Date().addingTimeInterval(30)
        while Date() < deadline {
            if !windows(application(pid: pid)).isEmpty { return }
            Thread.sleep(forTimeInterval: 0.2)
        }
        throw HelperError.elementNotFound("main window")
    case "window-count":
        print(windows(application(pid: pid)).count)
    case "dump":
        for window in windows(application(pid: pid)) {
            walk(window) { element in
                let role = attribute(element, kAXRoleAttribute) as? String ?? "?"
                let title = attribute(element, kAXTitleAttribute) as? String ?? ""
                let identifier = attribute(element, kAXIdentifierAttribute) as? String ?? ""
                let description = attribute(element, kAXDescriptionAttribute) as? String ?? ""
                print("\(role) title=\(title) id=\(identifier) description=\(description)")
            }
        }
    case "activate":
        try activate(pid)
    case "click":
        guard arguments.count == 4, let element = findIdentifier(pid, arguments[3]) else {
            throw HelperError.elementNotFound(arguments.count == 4 ? arguments[3] : "identifier")
        }
        try press(element, arguments[3])
    case "click-title":
        guard arguments.count == 4 else { throw HelperError.invalidArguments }
        guard let element = find(windows(application(pid: pid)), matching: {
            attribute($0, kAXTitleAttribute) as? String == arguments[3]
        }) else {
            throw HelperError.elementNotFound(arguments[3])
        }
        try press(element, arguments[3])
    case "set-text":
        guard arguments.count == 5, let element = findIdentifier(pid, arguments[3]) else {
            throw HelperError.elementNotFound(arguments.count >= 4 ? arguments[3] : "identifier")
        }
        try activate(pid)
        let status = AXUIElementSetAttributeValue(element, kAXFocusedAttribute as CFString, true as CFTypeRef)
        guard status == .success else { throw HelperError.actionFailed("Focus text field", status) }
        Thread.sleep(forTimeInterval: 0.1)
        let source = CGEventSource(stateID: .hidSystemState)
        for scalar in arguments[4].unicodeScalars {
            var units = Array(String(scalar).utf16)
            let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true)
            down?.keyboardSetUnicodeString(stringLength: units.count, unicodeString: &units)
            down?.postToPid(pid)
            let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false)
            up?.keyboardSetUnicodeString(stringLength: units.count, unicodeString: &units)
            up?.postToPid(pid)
            Thread.sleep(forTimeInterval: 0.01)
        }
        Thread.sleep(forTimeInterval: 0.1)
    case "select-popup":
        guard arguments.count == 5, let popup = findIdentifier(pid, arguments[3]) else {
            throw HelperError.elementNotFound(arguments.count >= 4 ? arguments[3] : "identifier")
        }
        try press(popup, arguments[3])
        Thread.sleep(forTimeInterval: 0.3)
        let app = application(pid: pid)
        guard let item = find([app], matching: {
            attribute($0, kAXRoleAttribute) as? String == "AXMenuItem" &&
                attribute($0, kAXTitleAttribute) as? String == arguments[4]
        }) else {
            throw HelperError.elementNotFound(arguments[4])
        }
        try press(item, arguments[4])
    case "exists":
        guard arguments.count == 4 else { throw HelperError.invalidArguments }
        print(findIdentifier(pid, arguments[3]) != nil)
    case "close-window":
        guard let close = find(windows(application(pid: pid)), matching: {
            attribute($0, kAXSubroleAttribute) as? String == "AXCloseButton"
        }) else {
            throw HelperError.elementNotFound("window close button")
        }
        try press(close, "Close window")
    case "auth-count":
        print(coreAuthApplication().map { windows($0).count } ?? 0)
    case "cancel-auth":
        guard let auth = coreAuthApplication(), let cancel = find(windows(auth), matching: {
            attribute($0, kAXTitleAttribute) as? String == "Cancel"
        }) else {
            throw HelperError.elementNotFound("Apple authentication Cancel button")
        }
        try press(cancel, "Cancel authentication")
    case "submit-auth":
        try submitAuth()
    default:
        throw HelperError.invalidArguments
    }
}

do {
    try run(CommandLine.arguments)
} catch {
    FileHandle.standardError.write(Data((error.localizedDescription + "\n").utf8))
    exit(1)
}
