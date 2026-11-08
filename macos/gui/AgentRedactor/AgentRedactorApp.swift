import SwiftUI

@main
struct AgentRedactorApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(appDelegate.appState)
                .frame(minWidth: 780, minHeight: 560)
        }
        .windowStyle(.titleBar)
        .defaultSize(width: 1000, height: 820)
    }
}
