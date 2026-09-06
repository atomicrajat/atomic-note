// A menu bar control for the Atomic Note companion service.
//
// The service is a LaunchAgent, so everything here is a thin wrapper over
// launchctl and one HTTP call to /health. It exists because "is the thing that
// listens to my voice notes currently running" is a question worth answering
// with a glance rather than a terminal.
//
// Deliberately an accessory app (LSUIElement): no dock icon, no window, no
// app switcher entry. It is a status item and nothing else.
//
// Build: ./menubar/build.sh

import Cocoa

// MARK: - Talking to the service

enum Service {
    static let label = "com.atomicnote.companion"
    static let port = ProcessInfo.processInfo.environment["ATOMIC_PORT"] ?? "8710"

    static var target: String { "gui/\(getuid())/\(label)" }
    static var plist: String {
        (NSHomeDirectory() as NSString)
            .appendingPathComponent("Library/LaunchAgents/\(label).plist")
    }
    static var logPath: String {
        (NSHomeDirectory() as NSString)
            .appendingPathComponent("Library/Logs/atomic-note/companion.log")
    }

    /// Run a command and return its exit status. Used for launchctl, which is
    /// the only way to ask launchd to do anything.
    @discardableResult
    static func run(_ launchPath: String, _ args: [String]) -> Int32 {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: launchPath)
        task.arguments = args
        task.standardOutput = Pipe()
        task.standardError = Pipe()
        do { try task.run() } catch { return -1 }
        task.waitUntilExit()
        return task.terminationStatus
    }

    @discardableResult
    static func launchctl(_ args: [String]) -> Int32 {
        run("/bin/launchctl", args)
    }

    /// Is the agent registered with launchd? That is what decides whether it
    /// comes back at login, which is a different question from whether it is
    /// answering right now.
    static var isLoaded: Bool {
        launchctl(["print", target]) == 0
    }

    static var isInstalled: Bool {
        FileManager.default.fileExists(atPath: plist)
    }

    static func start()   { if launchctl(["kickstart", "-k", target]) != 0 { enable() } }
    static func stop()    { launchctl(["kill", "SIGTERM", target]) }
    static func restart() { launchctl(["kickstart", "-k", target]) }
    static func enable()  { launchctl(["bootstrap", "gui/\(getuid())", plist]) }
    static func disable() { launchctl(["bootout", target]) }

    /// Ask the service itself, rather than trusting launchd's opinion. A
    /// process can be alive and not yet answering — during a model load, for
    /// instance — and the distinction matters to someone about to record.
    static func health(_ done: @escaping (String?) -> Void) {
        guard let url = URL(string: "http://127.0.0.1:\(port)/health") else {
            done(nil); return
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 3
        URLSession.shared.dataTask(with: request) { data, _, _ in
            let text = data.flatMap { String(data: $0, encoding: .utf8) }?
                .trimmingCharacters(in: .whitespacesAndNewlines)
            DispatchQueue.main.async { done(text?.isEmpty == false ? text : nil) }
        }.resume()
    }
}

// MARK: - The status item

final class Controller: NSObject, NSMenuDelegate {
    private let item = NSStatusItem.build()
    private let menu = NSMenu()
    private var timer: Timer?

    private var answering = false
    private var detail = "checking…"

    override init() {
        super.init()
        menu.delegate = self
        item.menu = menu
        render()

        // Five seconds is often enough to feel live without polling a local
        // HTTP server for no reason. The menu also refreshes on open.
        timer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in
            self?.refresh()
        }
        refresh()
    }

    func menuWillOpen(_ menu: NSMenu) { refresh() }

    private func refresh() {
        Service.health { [weak self] text in
            guard let self else { return }
            self.answering = text != nil
            self.detail = text ?? (Service.isLoaded ? "not answering" : "stopped")
            self.render()
        }
    }

    private func render() {
        // Filled when it is answering, slashed when it is not — readable at
        // menu bar size, where text would not be.
        let symbol = answering ? "mic.circle.fill" : "mic.slash.circle"
        item.button?.image = NSImage(systemSymbolName: symbol,
                                     accessibilityDescription: "Atomic Note")
        item.button?.image?.isTemplate = true
        item.button?.toolTip = "Atomic Note — \(detail)"

        menu.removeAllItems()

        guard Service.isInstalled else {
            menu.addItem(disabled("Service not installed"))
            menu.addItem(disabled("Run ./companion/install-service.sh"))
            menu.addItem(.separator())
            menu.addItem(action("Quit", #selector(quit)))
            return
        }

        menu.addItem(disabled(answering ? "● Running" : "○ Not running"))
        menu.addItem(disabled("   \(detail)"))
        menu.addItem(.separator())

        if answering {
            menu.addItem(action("Stop", #selector(stop)))
            menu.addItem(action("Restart", #selector(restartService)))
        } else {
            menu.addItem(action("Start", #selector(start)))
        }

        menu.addItem(.separator())
        let login = action("Start at Login", #selector(toggleLogin))
        login.state = Service.isLoaded ? .on : .off
        menu.addItem(login)

        menu.addItem(.separator())
        menu.addItem(action("Open Vault", #selector(openVault)))
        menu.addItem(action("View Logs", #selector(openLogs)))
        menu.addItem(.separator())
        menu.addItem(action("Quit", #selector(quit)))
    }

    // MARK: menu plumbing

    private func disabled(_ title: String) -> NSMenuItem {
        let entry = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        entry.isEnabled = false
        return entry
    }

    private func action(_ title: String, _ selector: Selector) -> NSMenuItem {
        let entry = NSMenuItem(title: title, action: selector, keyEquivalent: "")
        entry.target = self
        return entry
    }

    /// launchd takes a moment to act, so every command re-checks shortly after
    /// rather than redrawing from an assumption about what happened.
    private func settleThenRefresh() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) { [weak self] in
            self?.refresh()
        }
    }

    @objc private func start() { Service.start(); settleThenRefresh() }
    @objc private func stop() { Service.stop(); settleThenRefresh() }
    @objc private func restartService() { Service.restart(); settleThenRefresh() }

    @objc private func toggleLogin() {
        // "Start at Login" is the durable switch: unloading the agent is what
        // actually keeps it away after a reboot. Stop alone does not, because
        // the agent is configured to come back.
        if Service.isLoaded { Service.disable() } else { Service.enable() }
        settleThenRefresh()
    }

    @objc private func openVault() {
        let vault = ProcessInfo.processInfo.environment["ATOMIC_VAULT"]
            ?? (NSHomeDirectory() as NSString).appendingPathComponent("AtomicNote")
        NSWorkspace.shared.open(URL(fileURLWithPath: vault))
    }

    @objc private func openLogs() {
        NSWorkspace.shared.open(URL(fileURLWithPath: Service.logPath))
    }

    @objc private func quit() { NSApp.terminate(nil) }
}

private extension NSStatusItem {
    static func build() -> NSStatusItem {
        NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    }
}

// MARK: - Entry point

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let controller = Controller()
app.run()
