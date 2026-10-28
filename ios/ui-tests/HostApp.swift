import UIKit

// The test host supplies UIKit's real safe area on each simulator model. It is
// separate from the browser and contains no browser implementation.
final class MetricsController: UIViewController {
    private let label = UILabel()
    private let keyboardField = UITextField()
    override var prefersStatusBarHidden: Bool { true }
    override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .all }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .white
        label.accessibilityIdentifier = "nativeSafeArea"
        label.numberOfLines = 0
        label.font = .monospacedSystemFont(ofSize: 14, weight: .regular)
        view.addSubview(label)
        keyboardField.accessibilityIdentifier = "nativeKeyboardControl"
        keyboardField.placeholder = "Native keyboard control"
        keyboardField.borderStyle = .roundedRect
        keyboardField.autocapitalizationType = .none
        keyboardField.autocorrectionType = .no
        keyboardField.spellCheckingType = .no
        view.addSubview(keyboardField)
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        let i = view.safeAreaInsets
        let values: [String: Double] = ["width": Double(view.bounds.width), "height": Double(view.bounds.height),
            "left": Double(i.left), "top": Double(i.top), "right": Double(i.right), "bottom": Double(i.bottom),
            "pad": UIDevice.current.userInterfaceIdiom == .pad ? 1 : 0]
        let data = try! JSONSerialization.data(withJSONObject: values, options: [.sortedKeys])
        label.text = String(data: data, encoding: .utf8)
        label.frame = view.bounds.inset(by: i).insetBy(dx: 20, dy: 20)
        keyboardField.frame = CGRect(x: i.left + 20, y: i.top + 100, width: min(300, view.bounds.width - i.left - i.right - 40), height: 44)
    }
}

@main
final class HarnessApp: UIResponder, UIApplicationDelegate {
    var window: UIWindow?
    func application(_ application: UIApplication, didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        let window = UIWindow(frame: UIScreen.main.bounds)
        window.rootViewController = MetricsController()
        window.makeKeyAndVisible()
        self.window = window
        return true
    }
}
