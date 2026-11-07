// Velopack libc binding harness — proves the macOS updater wiring
// (VelopackUpdateProvider + the dlopen'd velopack_libc_osx.dylib) works
// end-to-end against a loopback feed WITHOUT vpk/dotnet:
//
//   1. dlopen + symbol resolution from macos/third_party/velopack
//   2. UpdateFeed.resolvedBaseURL loopback gating of AGENTREDACTOR_UPDATE_FEED
//   3. vpkc_app_run startup hooks return safely (no staged packages)
//   4. feed check against http://127.0.0.1:<port> detects a newer version
//      (with release notes) and downloads the nupkg into Velopack's package
//      cache — the exact code path test_update_feed.py's swap half drives
//      (apply itself needs a real vpk-packed bundle, which only CI can make)
//   5. a same-version feed reports up-to-date
//
// The binary must run from inside a fake <Name>.app with a vpk-style
// manifest (Contents/Resources/sq.version) and a Contents/MacOS/UpdateMac
// placeholder — that is what vpk pack produces; see run.sh which sets it up.
//
// Usage: velopack-harness <dylib-path> <scenario> [feed-url]
//   scenarios: resolved-url | app-run | check <feed-url> | packages-dir

import Foundation

var arguments = ProcessInfo.processInfo.arguments
arguments.removeFirst()
guard arguments.count >= 2 else {
    print("usage: velopack-harness <dylib-path> <scenario> [feed-url]")
    exit(2)
}
let dylibPath = arguments[0]
let scenario = arguments[1]

func fail(_ message: String) -> Never {
    print("FAIL: \(message)")
    exit(1)
}

switch scenario {
case "resolved-url":
    // Loopback gating of AGENTREDACTOR_UPDATE_FEED (run.sh asserts the
    // output for loopback / non-loopback / unset env values).
    print("resolved=\(UpdateFeed.resolvedBaseURL)")

case "app-run":
    guard let lib = VelopackLibC(dylibPath: URL(fileURLWithPath: dylibPath)) else {
        fail("dlopen/symbol resolution failed for \(dylibPath)")
    }
    lib.appRun(nil) // must return: no staged packages, nothing to consume
    print("PASS: app-run returned")

case "check":
    guard arguments.count >= 3 else { fail("check needs a feed URL") }
    let feedURL = arguments[2]
    guard let lib = VelopackLibC(dylibPath: URL(fileURLWithPath: dylibPath)) else {
        fail("dlopen/symbol resolution failed for \(dylibPath)")
    }
    let provider = VelopackUpdateProvider(lib: lib, feedBaseURL: feedURL)
    let semaphore = DispatchSemaphore(value: 0)
    var outcome = ""
    Task {
        switch await provider.checkForUpdates() {
        case .success(.upToDate):
            outcome = "uptodate"
        case .success(.updateReady(let info)):
            outcome = "ready \(info.version) notes=\(info.notesMarkdown ?? "-")"
        case .failure(let error):
            outcome = "error \(error.localizedDescription)"
        }
        semaphore.signal()
    }
    semaphore.wait()
    print("CHECK-RESULT: \(outcome)")

case "packages-dir":
    // Where this (fake) app's downloaded packages land — run.sh asserts the
    // downloaded nupkg appears here after a successful check.
    let home = ProcessInfo.processInfo.environment["HOME"] ?? NSHomeDirectory()
    print("packages=\(home)/Library/Caches/velopack/dev.velopack.harness/packages")

default:
    fail("unknown scenario: \(scenario)")
}
