// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Draws the DMG window background for make-dmg-layout.py, using only
// CoreGraphics, CoreText and ImageIO.
//
//   swift dmg-background.swift <out.png> <width> <height> <scale> <app-x> <app-y>
//       <apps-x> <apps-y> <title> <caption>
//
// Sizes and positions are in points, measured from the top left of the window
// content, which is where Finder places both the background image and the icon
// positions. The image is <scale> times larger in pixels and tagged with the
// matching DPI, so Finder shows it at <width> x <height> points.

import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

let args = CommandLine.arguments
guard args.count == 11,
      let width = Double(args[2]), let height = Double(args[3]), let scale = Double(args[4]),
      let appX = Double(args[5]), let appY = Double(args[6]),
      let appsX = Double(args[7]), let appsY = Double(args[8])
else { fail("usage: see the comment at the top of dmg-background.swift") }
let outPath = args[1], title = args[9], caption = args[10]

let space = CGColorSpace(name: CGColorSpace.sRGB)!
func color(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: space, components: [
        CGFloat((hex >> 16) & 255) / 255, CGFloat((hex >> 8) & 255) / 255, CGFloat(hex & 255) / 255, alpha,
    ])!
}

guard let ctx = CGContext(
    data: nil, width: Int(width * scale), height: Int(height * scale), bitsPerComponent: 8,
    bytesPerRow: 0, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
else { fail("cannot create the bitmap") }
ctx.interpolationQuality = .high
// Work in points with the origin at the top left.
ctx.translateBy(x: 0, y: height * scale)
ctx.scaleBy(x: scale, y: -scale)

let gradient = CGGradient(
    colorsSpace: space, colors: [color(0xFFFFFF), color(0xEBEEF3)] as CFArray, locations: [0, 1])!
ctx.drawLinearGradient(
    gradient, start: CGPoint(x: 0, y: 0), end: CGPoint(x: 0, y: height), options: [])

// Text is drawn with a flipped matrix, because CoreText expects y to point up.
func drawCentered(_ text: String, _ font: CTFont, _ textColor: CGColor, x: Double, baseline: Double) {
    let attributes: [CFString: Any] = [kCTFontAttributeName: font, kCTForegroundColorAttributeName: textColor]
    let line = CTLineCreateWithAttributedString(CFAttributedStringCreate(nil, text as CFString, attributes as CFDictionary)!)
    let textWidth = CTLineGetTypographicBounds(line, nil, nil, nil)
    ctx.saveGState()
    ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    ctx.textPosition = CGPoint(x: x - textWidth / 2, y: baseline)
    CTLineDraw(line, ctx)
    ctx.restoreGState()
}

drawCentered(title, CTFontCreateUIFontForLanguage(.emphasizedSystem, 28, nil)!, color(0x163436),
             x: width / 2, baseline: 66)
drawCentered(caption, CTFontCreateUIFontForLanguage(.system, 15, nil)!, color(0x6E6E73),
             x: width / 2, baseline: 96)

// An arrow in the green of the icon's canopy, between the app and Applications.
let midX = (appX + appsX) / 2, midY = (appY + appsY) / 2
let arrow = CGMutablePath()
arrow.move(to: CGPoint(x: midX - 44, y: midY))
arrow.addLine(to: CGPoint(x: midX + 44, y: midY))
arrow.move(to: CGPoint(x: midX + 28, y: midY - 16))
arrow.addLine(to: CGPoint(x: midX + 44, y: midY))
arrow.addLine(to: CGPoint(x: midX + 28, y: midY + 16))
ctx.setStrokeColor(color(0x1A9559))
ctx.setLineWidth(6)
ctx.setLineCap(.round)
ctx.setLineJoin(.round)
ctx.addPath(arrow)
ctx.strokePath()

guard let image = ctx.makeImage(),
      let dest = CGImageDestinationCreateWithURL(
          URL(fileURLWithPath: outPath) as CFURL, UTType.png.identifier as CFString, 1, nil)
else { fail("cannot write \(outPath)") }
let properties: [CFString: Any] = [kCGImagePropertyDPIWidth: 72 * scale, kCGImagePropertyDPIHeight: 72 * scale]
CGImageDestinationAddImage(dest, image, properties as CFDictionary)
if !CGImageDestinationFinalize(dest) { fail("cannot write \(outPath)") }
