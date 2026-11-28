import Foundation
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers
// Pixlpal app icons: teal (#008080) line drawings on white in a rounded square, drawn on a 500 x 500 grid.
let teal = CGColor(colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!, components: [0, 128.0/255, 128.0/255, 1])!
func save(_ name: String, size: Int, _ draw: (CGContext) -> Void){
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0, space: cs, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1)); ctx.fill(CGRect(x: 0, y: 0, width: size, height: size))
    let s = CGFloat(size) / 500
    ctx.scaleBy(x: s, y: -s); ctx.translateBy(x: 0, y: -500)     // y down, as on a screen
    ctx.setStrokeColor(teal); ctx.setFillColor(teal)
    ctx.setLineWidth(20); ctx.setLineCap(.round); ctx.setLineJoin(.round)
    ctx.addPath(CGPath(roundedRect: CGRect(x: 22, y: 22, width: 456, height: 456), cornerWidth: 70, cornerHeight: 70, transform: nil)); ctx.strokePath()
    draw(ctx)
    let url = URL(fileURLWithPath: name) as CFURL
    let dest = CGImageDestinationCreateWithURL(url, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(dest, ctx.makeImage()!, nil); CGImageDestinationFinalize(dest)
}
func line(_ c: CGContext, _ pts: [(CGFloat, CGFloat)]){ c.move(to: CGPoint(x: pts[0].0, y: pts[0].1)); for p in pts.dropFirst(){ c.addLine(to: CGPoint(x: p.0, y: p.1)) }; c.strokePath() }
func circle(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, fill: Bool = false){ let rc = CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r); if fill { c.fillEllipse(in: rc) } else { c.strokeEllipse(in: rc) } }
func arc(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ a0: CGFloat, _ a1: CGFloat){ c.addArc(center: CGPoint(x: x, y: y), radius: r, startAngle: a0 * .pi / 180, endAngle: a1 * .pi / 180, clockwise: false); c.strokePath() }

let icons: [String: (CGContext) -> Void] = [
  "iss": { c in   // the space station: a truss, two pairs of solar wings and the modules
    line(c, [(150, 250), (350, 250)])
    for x: CGFloat in [70, 340] { for y: CGFloat in [160, 270] { c.addPath(CGPath(rect: CGRect(x: x, y: y, width: 90, height: 70), transform: nil)); c.strokePath(); line(c, [(x + 45, y), (x + 45, y + 70)]) } }
    c.addPath(CGPath(roundedRect: CGRect(x: 215, y: 215, width: 70, height: 70), cornerWidth: 12, cornerHeight: 12, transform: nil)); c.strokePath()
  },
  "onthisday": { c in   // a calendar page with a star: a day to remember
    c.addPath(CGPath(roundedRect: CGRect(x: 100, y: 110, width: 300, height: 290), cornerWidth: 26, cornerHeight: 26, transform: nil)); c.strokePath()
    line(c, [(100, 185), (400, 185)]); line(c, [(180, 80), (180, 140)]); line(c, [(320, 80), (320, 140)])
    var pts: [(CGFloat, CGFloat)] = []
    for k in 0..<10 { let a = -CGFloat.pi / 2 + CGFloat(k) * .pi / 5; let r: CGFloat = k % 2 == 0 ? 78 : 32; pts.append((250 + r * cos(a), 298 + r * sin(a))) }
    pts.append(pts[0]); line(c, pts)
  },
  "launches": { c in   // a rocket rising, with its flame
    c.move(to: CGPoint(x: 250, y: 70)); c.addCurve(to: CGPoint(x: 290, y: 320), control1: CGPoint(x: 320, y: 140), control2: CGPoint(x: 300, y: 250))
    c.addLine(to: CGPoint(x: 210, y: 320)); c.addCurve(to: CGPoint(x: 250, y: 70), control1: CGPoint(x: 200, y: 250), control2: CGPoint(x: 180, y: 140)); c.strokePath()
    circle(c, 250, 190, 22)
    line(c, [(212, 270), (165, 335), (210, 320)]); line(c, [(288, 270), (335, 335), (290, 320)])
    line(c, [(230, 345), (225, 395)]); line(c, [(250, 345), (250, 420)]); line(c, [(270, 345), (275, 395)])
  },
  "printer": { c in   // a 3D printer: the frame, the gantry and nozzle, a part growing layer by layer
    c.addPath(CGPath(roundedRect: CGRect(x: 100, y: 80, width: 300, height: 340), cornerWidth: 18, cornerHeight: 18, transform: nil)); c.strokePath()
    line(c, [(100, 160), (400, 160)])
    c.move(to: CGPoint(x: 225, y: 160)); c.addLine(to: CGPoint(x: 275, y: 160)); c.addLine(to: CGPoint(x: 250, y: 205)); c.closePath(); c.fillPath()
    line(c, [(170, 370), (330, 370)]); line(c, [(185, 335), (315, 335)]); line(c, [(205, 300), (295, 300)]); line(c, [(225, 265), (275, 265)])
  },
  "solar": { c in   // the sun above a solar panel
    circle(c, 170, 155, 50)
    for k in 0..<8 { let a = CGFloat(k) * .pi / 4; line(c, [(170 + 75 * cos(a), 155 + 75 * sin(a)), (170 + 100 * cos(a), 155 + 100 * sin(a))]) }
    c.move(to: CGPoint(x: 190, y: 300)); c.addLine(to: CGPoint(x: 400, y: 300)); c.addLine(to: CGPoint(x: 430, y: 410)); c.addLine(to: CGPoint(x: 160, y: 410)); c.closePath(); c.strokePath()
    line(c, [(175, 355), (415, 355)]); line(c, [(260, 300), (255, 410)]); line(c, [(330, 300), (345, 410)])
  },
  "followers": { c in   // a person, and a count going up
    circle(c, 200, 200, 58)
    c.addArc(center: CGPoint(x: 200, y: 400), radius: 110, startAngle: .pi, endAngle: 2 * .pi, clockwise: false); c.strokePath()
    line(c, [(340, 300), (340, 130)]); line(c, [(290, 180), (340, 130), (390, 180)])
  },
  "trivia": { c in   // a question mark in a speech bubble
    c.addPath(CGPath(roundedRect: CGRect(x: 90, y: 90, width: 320, height: 250), cornerWidth: 50, cornerHeight: 50, transform: nil)); c.strokePath()
    line(c, [(170, 340), (150, 410), (240, 340)])
    arc(c, 250, 175, 45, 180, 90)
    line(c, [(250, 220), (250, 255)])
    circle(c, 250, 295, 12, fill: true)
  },
  "life": { c in   // a glider of the Game of Life on its grid
    let cells: [(CGFloat, CGFloat)] = [(1, 0), (2, 1), (0, 2), (1, 2), (2, 2)]
    for (x, y) in cells { c.fill(CGRect(x: 130 + x * 85, y: 130 + y * 85, width: 70, height: 70)) }
    c.saveGState(); c.setLineWidth(6)
    for k in 0...3 { let v = 122 + CGFloat(k) * 85; line(c, [(v, 122), (v, 377)]); line(c, [(122, v), (377, v)]) }
    c.restoreGState()
  },
]
let out = CommandLine.arguments[1]
for (name, draw) in icons { save("\(out)/\(name)_128.png", size: 128, draw); save("\(out)/\(name)_500.png", size: 500, draw) }
print("ok")
