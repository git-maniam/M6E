// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Image steps for make-icons.sh, using only CoreGraphics and ImageIO.
//
//   swift icon-tool.swift master <source.png> <out.png>
//       Find the white rounded tile in the source artwork, make everything
//       outside it transparent, and fit it onto a 1024x1024 canvas on Apple's
//       icon grid (an 824 px body) without changing its aspect ratio.
//   swift icon-tool.swift document <master.png> <out.png>
//       Draw a 1024x1024 document icon: a page with the app icon on it.
//   swift icon-tool.swift dmg-background <out.png> <width> <height>
//       Draw the DMG window background with an arrow towards Applications.
//   swift icon-tool.swift fit <icon.png> <target>
//       Replace an existing PNG or GIF image with the icon, centred at the
//       largest size that fits, on a transparent canvas of the target's pixel
//       size. Used by replace-fox-art.sh.

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

func loadImage(_ path: String) -> CGImage {
    guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
    else { fail("cannot read \(path)") }
    return image
}

func writePNG(_ image: CGImage, _ path: String) {
    guard let dest = CGImageDestinationCreateWithURL(
        URL(fileURLWithPath: path) as CFURL, UTType.png.identifier as CFString, 1, nil)
    else { fail("cannot write \(path)") }
    CGImageDestinationAddImage(dest, image, nil)
    if !CGImageDestinationFinalize(dest) { fail("cannot write \(path)") }
}

func makeContext(_ width: Int, _ height: Int) -> CGContext {
    guard let ctx = CGContext(
        data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
        space: CGColorSpace(name: CGColorSpace.sRGB)!,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { fail("cannot create a \(width)x\(height) bitmap") }
    ctx.interpolationQuality = .high
    return ctx
}

/// RGBA pixels of an image, top row first.
struct Pixels {
    let width: Int, height: Int
    var data: [UInt8]

    init(_ image: CGImage) {
        width = image.width
        height = image.height
        let ctx = makeContext(width, height)
        ctx.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        let buffer = ctx.data!.bindMemory(to: UInt8.self, capacity: width * height * 4)
        data = Array(UnsafeBufferPointer(start: buffer, count: width * height * 4))
    }

    func minChannel(_ x: Int, _ y: Int) -> Int {
        let i = (y * width + x) * 4
        return Int(min(data[i], data[i + 1], data[i + 2]))
    }
}

func master(_ sourcePath: String, _ outPath: String) {
    var px = Pixels(loadImage(sourcePath))
    let w = px.width, h = px.height
    let isWhite = { (x: Int, y: Int) in px.minChannel(x, y) >= 248 }

    // The tile is the only large white area. Rows and columns that are mostly
    // white bound it; the small watermark never reaches the threshold.
    let rows = (0..<h).filter { y in (0..<w).reduce(0) { $0 + (isWhite($1, y) ? 1 : 0) } > w / 6 }
    guard let top = rows.first, let bottom = rows.last else { fail("no tile found") }
    let cols = (0..<w).filter { x in (top...bottom).reduce(0) { $0 + (isWhite(x, $1) ? 1 : 0) } > (bottom - top) / 2 }
    guard let left = cols.first, let right = cols.last else { fail("no tile found") }

    // Everything reachable from the crop corners without crossing white is
    // background, shadow or watermark, so it becomes transparent.
    let cw = right - left + 1, ch = bottom - top + 1
    var outside = [Bool](repeating: false, count: cw * ch)
    var stack = [(left, top), (right, top), (left, bottom), (right, bottom)]
    while let (x, y) = stack.popLast() {
        if x < left || x > right || y < top || y > bottom { continue }
        let k = (y - top) * cw + (x - left)
        if outside[k] || px.minChannel(x, y) >= 245 { continue }
        outside[k] = true
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    }

    // Soften the edge: tile pixels next to the outside get an alpha from how
    // close they are to white, which keeps the antialiasing of the corners.
    let background = Double(px.minChannel(2, 2))
    for y in top...bottom {
        for x in left...right {
            let k = (y - top) * cw + (x - left)
            let i = (y * w + x) * 4
            if outside[k] {
                px.data[i + 3] = 0
                continue
            }
            var edge = false
            for (dx, dy) in [(1, 0), (-1, 0), (0, 1), (0, -1)] {
                let nx = x + dx - left, ny = y + dy - top
                if nx >= 0, nx < cw, ny >= 0, ny < ch, outside[ny * cw + nx] { edge = true }
            }
            if edge {
                let value = Double(px.minChannel(x, y))
                let alpha = max(0, min(1, (value - background) / (255 - background)))
                px.data[i + 3] = UInt8(alpha * 255)
            }
        }
    }

    // Unpremultiplied pixels are copied into a premultiplied bitmap.
    let tileCtx = makeContext(cw, ch)
    let tileBuffer = tileCtx.data!.bindMemory(to: UInt8.self, capacity: cw * ch * 4)
    for y in 0..<ch {
        for x in 0..<cw {
            let s = ((y + top) * w + (x + left)) * 4
            let d = (y * cw + x) * 4
            let a = Double(px.data[s + 3]) / 255
            for c in 0..<3 { tileBuffer[d + c] = UInt8(Double(px.data[s + c]) * a) }
            tileBuffer[d + 3] = px.data[s + 3]
        }
    }
    let tile = tileCtx.makeImage()!

    // Apple's macOS icon grid: an 824 px body centred on a 1024 px canvas,
    // with a soft drop shadow. The tile keeps its own aspect ratio.
    let body = 824.0
    let scale = body / Double(max(cw, ch))
    let tw = Double(cw) * scale, th = Double(ch) * scale
    let ctx = makeContext(1024, 1024)
    ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 28,
                  color: CGColor(srgbRed: 0, green: 0, blue: 0, alpha: 0.3))
    ctx.draw(tile, in: CGRect(x: (1024 - tw) / 2, y: (1024 - th) / 2 + 6, width: tw, height: th))
    writePNG(ctx.makeImage()!, outPath)
    print("tile \(cw)x\(ch) at (\(left),\(top)) placed at \(Int(tw))x\(Int(th))")
}

func document(_ masterPath: String, _ outPath: String) {
    let icon = loadImage(masterPath)
    let ctx = makeContext(1024, 1024)
    let page = CGRect(x: 192, y: 64, width: 640, height: 896)
    let fold = 176.0
    let path = CGMutablePath()
    path.move(to: CGPoint(x: page.minX, y: page.minY))
    path.addLine(to: CGPoint(x: page.maxX, y: page.minY))
    path.addLine(to: CGPoint(x: page.maxX, y: page.maxY - fold))
    path.addLine(to: CGPoint(x: page.maxX - fold, y: page.maxY))
    path.addLine(to: CGPoint(x: page.minX, y: page.maxY))
    path.closeSubpath()
    ctx.setShadow(offset: CGSize(width: 0, height: -8), blur: 20,
                  color: CGColor(srgbRed: 0, green: 0, blue: 0, alpha: 0.25))
    ctx.setFillColor(CGColor(srgbRed: 1, green: 1, blue: 1, alpha: 1))
    ctx.addPath(path)
    ctx.fillPath()
    ctx.setShadow(offset: .zero, blur: 0, color: nil)
    let corner = CGMutablePath()
    corner.move(to: CGPoint(x: page.maxX - fold, y: page.maxY))
    corner.addLine(to: CGPoint(x: page.maxX - fold, y: page.maxY - fold))
    corner.addLine(to: CGPoint(x: page.maxX, y: page.maxY - fold))
    corner.closeSubpath()
    ctx.setFillColor(CGColor(srgbRed: 0.86, green: 0.87, blue: 0.89, alpha: 1))
    ctx.addPath(corner)
    ctx.fillPath()
    ctx.draw(icon, in: CGRect(x: 272, y: 220, width: 480, height: 480))
    writePNG(ctx.makeImage()!, outPath)
}

func dmgBackground(_ outPath: String, _ width: Int, _ height: Int) {
    let ctx = makeContext(width, height)
    ctx.setFillColor(CGColor(srgbRed: 0.965, green: 0.969, blue: 0.976, alpha: 1))
    ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
    // A chevron between the app (left) and Applications (right) positions.
    let cx = Double(width) / 2, cy = Double(height) / 2
    let arrow = CGMutablePath()
    arrow.move(to: CGPoint(x: cx - 90, y: cy))
    arrow.addLine(to: CGPoint(x: cx + 70, y: cy))
    arrow.move(to: CGPoint(x: cx + 20, y: cy + 50))
    arrow.addLine(to: CGPoint(x: cx + 70, y: cy))
    arrow.addLine(to: CGPoint(x: cx + 20, y: cy - 50))
    ctx.setStrokeColor(CGColor(srgbRed: 0.55, green: 0.57, blue: 0.62, alpha: 1))
    ctx.setLineWidth(14)
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)
    ctx.addPath(arrow)
    ctx.strokePath()
    writePNG(ctx.makeImage()!, outPath)
}

func fit(_ iconPath: String, _ targetPath: String) {
    let target = loadImage(targetPath)
    let icon = loadImage(iconPath)
    let width = target.width, height = target.height
    let side = CGFloat(min(width, height))
    let ctx = makeContext(width, height)
    ctx.draw(icon, in: CGRect(
        x: (CGFloat(width) - side) / 2, y: (CGFloat(height) - side) / 2,
        width: side, height: side))
    let type = targetPath.lowercased().hasSuffix(".gif") ? UTType.gif : UTType.png
    guard let dest = CGImageDestinationCreateWithURL(
        URL(fileURLWithPath: targetPath) as CFURL, type.identifier as CFString, 1, nil)
    else { fail("cannot write \(targetPath)") }
    CGImageDestinationAddImage(dest, ctx.makeImage()!, nil)
    if !CGImageDestinationFinalize(dest) { fail("cannot write \(targetPath)") }
}

let args = CommandLine.arguments
switch args.count > 1 ? args[1] : "" {
case "master" where args.count == 4: master(args[2], args[3])
case "document" where args.count == 4: document(args[2], args[3])
case "dmg-background" where args.count == 5:
    dmgBackground(args[2], Int(args[3]) ?? 1440, Int(args[4]) ?? 880)
case "fit" where args.count == 4: fit(args[2], args[3])
default: fail("usage: see the comment at the top of icon-tool.swift")
}
