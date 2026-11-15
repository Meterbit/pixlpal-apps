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
  "standup": { c in   // three people, a stopwatch above them
    circle(c, 250, 150, 52); line(c, [(250, 150), (250, 118)]); line(c, [(250, 150), (274, 166)]); line(c, [(232, 86), (268, 86)])
    for x: CGFloat in [140, 250, 360] { circle(c, x, 280, 30); arc(c, x, 395, 58, 180, 360) }
  },
  "roomsign": { c in   // a sign hanging on its strings: a status dot and two lines
    line(c, [(150, 200), (250, 100), (350, 200)]); circle(c, 250, 100, 12, fill: true)
    c.addPath(CGPath(roundedRect: CGRect(x: 95, y: 200, width: 310, height: 190), cornerWidth: 26, cornerHeight: 26, transform: nil)); c.strokePath()
    circle(c, 165, 295, 34, fill: true); line(c, [(230, 265), (350, 265)]); line(c, [(230, 325), (320, 325)])
  },
  "build": { c in   // a branch, and a tick
    line(c, [(160, 160), (160, 340)]); circle(c, 160, 125, 32); circle(c, 160, 375, 32); circle(c, 300, 175, 32)
    c.move(to: CGPoint(x: 300, y: 207)); c.addCurve(to: CGPoint(x: 160, y: 330), control1: CGPoint(x: 300, y: 280), control2: CGPoint(x: 170, y: 260)); c.strokePath()
    line(c, [(255, 330), (300, 375), (385, 285)])
  },
  "gong": { c in   // a gong in its frame
    line(c, [(110, 400), (110, 110), (390, 110), (390, 400)]); line(c, [(80, 400), (140, 400)]); line(c, [(360, 400), (420, 400)])
    line(c, [(205, 110), (220, 160)]); line(c, [(295, 110), (280, 160)])
    circle(c, 250, 260, 100); circle(c, 250, 260, 40, fill: true)
  },
  "team": { c in   // a globe
    circle(c, 250, 250, 150)
    c.strokeEllipse(in: CGRect(x: 190, y: 100, width: 120, height: 300)); line(c, [(250, 100), (250, 400)])
    line(c, [(100, 250), (400, 250)]); arc(c, 250, -40, 250, 62, 118); arc(c, 250, 540, 250, 242, 298)
  },
  "quiet": { c in   // a level gauge: green, amber, red, and the needle
    arc(c, 250, 320, 150, 180, 360)
    for a in stride(from: 180.0, through: 360.0, by: 30.0){ let r = a * .pi / 180; line(c, [(250 + 118 * cos(r), 320 + 118 * sin(r)), (250 + 96 * cos(r), 320 + 96 * sin(r))]) }
    line(c, [(250, 320), (314, 256)]); circle(c, 250, 320, 22, fill: true); line(c, [(130, 390), (370, 390)])
  },
]
let out = CommandLine.arguments[1]
for (name, draw) in icons { save("\(out)/\(name)_128.png", size: 128, draw); save("\(out)/\(name)_500.png", size: 500, draw) }
print("ok")
