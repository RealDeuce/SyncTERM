// Pixel API contract shared by the menu and picker VM harnesses.

import "syncterm" for Cell, PixelBlit, PixelBuffer,
    PixelColor, PixelMask, Screen

class PixelVmTest {
  static run() {
    var pass = 0
    var fail = 0
    var check = Fn.new {|condition|
      if (condition) {
        pass = pass + 1
      } else {
        fail = fail + 1
      }
    }

    var colour = PixelColor.rgb(0x123456)
    var palette = PixelColor.palette(7)
    check.call(PixelColor.isRgb(colour) && !PixelColor.isRgb(palette))
    check.call(PixelColor.rgbValue(colour) == 0x123456 &&
        PixelColor.paletteIndex(palette) == 7 &&
        PixelColor.toRgb(palette) is Num)

    var pixels = PixelBuffer.new(2, 1, colour)
    pixels[1] = palette
    var blit = PixelBlit.new()
    check.call(pixels.count == 2 && pixels.width == 2 && pixels.height == 1 &&
        pixels[0] == colour && pixels[1] == palette && pixels.toList.count == 2)
    var rgb = String.fromByte(1) + String.fromByte(2) + String.fromByte(3) +
        String.fromByte(4) + String.fromByte(5) + String.fromByte(6)
    var imported = PixelBuffer.fromRgb24(2, 1, rgb)
    check.call(imported.rgb24 == rgb && !imported.hasAlternate)
    imported.setAlternate(1, 0, PixelColor.rgb(0x102030))
    check.call(imported.hasAlternate && imported.alternateAt(0) == imported[0] &&
        imported.clone().alternateRgb24 == imported.alternateRgb24)
    imported.clearAlternate()
    check.call(!imported.hasAlternate && imported.alternateRgb24 == null)
    var bad = Fiber.new { imported.setAlternateRgb24("short") }
    bad.try()
    check.call(bad.error != null && !imported.hasAlternate)
    check.call(PixelBuffer.new(0, 0).count == 0)

    var mask = PixelMask.fromBits(3, 2, String.fromByte(0xa8))
    check.call(mask.count == 6 && mask[0] && !mask[1] && mask.bitAt(1, 1))
    mask.setBit(1, 0, true)
    check.call(mask.toList.join("") == "truetruetruefalsetruefalse")
    check.call(PixelMask.new(0, 0).count == 0)
    check.call(blit.scaleX == 1 && blit.scaleY == 1)
    blit.destinationX = -2
    blit.scaleX = 3
    blit.flipY = true
    check.call(blit.destinationX == -2 && blit.scaleX == 3 && blit.flipY)
    bad = Fiber.new { blit.scaleX = 0 }
    bad.try()
    check.call(bad.error != null && blit.scaleX == 3)
    bad = Fiber.new { blit.scaleY = 0 }
    bad.try()
    check.call(bad.error != null && blit.scaleY == 1)
    blit.sourceX = 0
    blit.sourceY = 0
    blit.sourceWidth = 0
    blit.sourceHeight = 0
    blit.maskX = 0
    blit.maskY = 0
    check.call(blit.sourceX == 0 && blit.sourceY == 0 &&
        blit.sourceWidth == 0 && blit.sourceHeight == 0 &&
        blit.maskX == 0 && blit.maskY == 0)
    blit.scaleX = 4294967295
    blit.scaleY = 4294967295
    check.call(blit.scaleX == 4294967295 && blit.scaleY == 4294967295)
    bad = Fiber.new { blit.scaleX = 4294967296 }
    bad.try()
    check.call(bad.error != null && blit.scaleX == 4294967295)
    bad = Fiber.new { blit.sourceX = -1 }
    bad.try()
    check.call(bad.error != null && blit.sourceX == 0)
    bad = Fiber.new { blit.scaleY = 1.5 }
    bad.try()
    check.call(bad.error != null && blit.scaleY == 4294967295)

    var ppm = PixelBuffer.fromPpm("P3\n2 1\n255\n0 0 0 255 255 255\n")
    check.call(ppm is PixelBuffer && PixelColor.toRgb(ppm[0]) == 0 &&
        PixelColor.toRgb(ppm[1]) == 0xffffff)
    var p6 = "P6\n1 1\n255\n" + String.fromByte(0x12) +
        String.fromByte(0x34) + String.fromByte(0x56)
    check.call(PixelBuffer.fromPpm(p6) is PixelBuffer)
    var pbm = PixelMask.fromPbm("P1\n3 1\n0 1 0\n")
    check.call(pbm is PixelMask && !pbm[0] && pbm[1] && !pbm[2])
    var p4 = "P4\n9 1\n" + String.fromByte(0x80) + String.fromByte(0x80)
    var rawMask = PixelMask.fromPbm(p4)
    check.call(rawMask is PixelMask && rawMask[0] && rawMask[8])
    check.call(PixelBuffer.fromPpm("bad") == null &&
        PixelMask.fromPbm("bad") == null && PixelBuffer.jxlSupported is Bool)

    var size = Screen.pixelSize
    var cellSize = Screen.cellPixelSize
    check.call((!Screen.supports.pixels && size == null && cellSize == null) ||
        (size.count == 2 && cellSize.count == 2))
    if (Screen.supports.pixels) {
      var saved = Screen.readPixels(0, 0, 4, 4, true)
      var replacement = PixelColor.rgb(0x010203)
      check.call(saved is PixelBuffer && Screen.setPixel(0, 0, replacement))
      check.call(Screen.readRect(1, 1, 1, 1)[0].pixelGraphics)
      var captured = Screen.readPixels(0, 0, 1, 1)
      check.call(captured[0] == replacement)
      var source = PixelBuffer.new(2, 1)
      source[0] = PixelColor.rgb(0x112233)
      source[1] = PixelColor.rgb(0x445566)
      source.setAlternate(0, 0, PixelColor.rgb(0xaabbcc))
      var operation = PixelBlit.new()
      operation.destinationX = 0
      operation.destinationY = 0
      operation.scaleX = 2
      operation.flipX = true
      check.call(Screen.blitPixels(source, operation))
      var result = Screen.readPixels(0, 0, 4, 1)
      check.call(PixelColor.toRgb(result[0]) == 0x445566 &&
          PixelColor.toRgb(result[2]) == 0x112233 && result.hasAlternate &&
          PixelColor.toRgb(result.alternateAt(2)) == 0xaabbcc)
      var beforeMask = Screen.readPixels(2, 2, 2, 1)
      var drawMask = PixelMask.fromBits(2, 1, String.fromByte(0x80))
      operation = PixelBlit.new()
      operation.destinationX = 2
      operation.destinationY = 2
      check.call(Screen.blitPixels(source, drawMask, operation))
      var afterMask = Screen.readPixels(2, 2, 2, 1)
      check.call(PixelColor.toRgb(afterMask[0]) == 0x112233 &&
          afterMask[1] == beforeMask[1])
      operation = PixelBlit.new()
      operation.destinationX = -1
      operation.scaleX = 2
      check.call(Screen.blitPixels(source, operation))
      var clipped = Screen.readPixels(0, 0, 3, 1)
      check.call(PixelColor.toRgb(clipped[0]) == 0x112233 &&
          PixelColor.toRgb(clipped[1]) == 0x445566 &&
          PixelColor.toRgb(clipped[2]) == 0x445566)
      operation = PixelBlit.new()
      check.call(Screen.blitPixels(saved, operation))
      operation.destinationX = size[0]
      check.call(Screen.blitPixels(saved, operation))
    }
    check.call(!Cell.new().pixelGraphics)
    return [pass, fail]
  }
}

class MenuTest {
  static run() { PixelVmTest.run() }
}
