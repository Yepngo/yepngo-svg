import Foundation

enum BasicBadgeSVG {
    static let xml = """
    <svg width="320" height="320" viewBox="0 0 320 320" xmlns="http://www.w3.org/2000/svg">
      <rect x="20" y="20" width="280" height="280" rx="24" fill="#e6f4ff" stroke="#1976d2" stroke-width="8"/>
      <circle cx="160" cy="160" r="82" fill="#ffd54f" stroke="#f57c00" stroke-width="10"/>
      <polygon points="160,88 186,150 254,154 202,198 218,266 160,228 102,266 118,198 66,154 134,150" fill="#ff9801"/>
      <ellipse cx="160" cy="272" rx="82" ry="18" fill="#c8e6c9"/>
    </svg>
    """

    static var data: Data {
        Data(xml.utf8)
    }
}
