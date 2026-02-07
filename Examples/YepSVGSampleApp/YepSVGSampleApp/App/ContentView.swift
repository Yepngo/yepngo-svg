import SwiftUI

struct ContentView: View {
    var body: some View {
        TabView {
            BadgeDemoView()
                .tabItem {
                    Label("Demo", systemImage: "photo")
                }

            W3CTestHarnessView()
                .tabItem {
                    Label("W3C Harness", systemImage: "checklist")
                }
        }
    }
}
