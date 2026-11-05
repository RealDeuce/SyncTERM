import "syncterm" for Error, IOErr, PixelError
// Pixel API contract shared by the menu and picker VM harnesses.

import "syncterm" for Cell, PixelBlit, PixelBuffer,
    PixelColor, PixelMask, Screen

class PixelVmTest {
  static runOffscreen() {
    var pass = 0
    var check = Fn.new {|ok, label|
      if (!ok) Fiber.abort("offscreen blit: " + label)
      pass = pass + 1
    }
    var rejects = Fn.new {|fn|
      var attempt = Fiber.new { fn.call() }
      attempt.try()
      check.call(attempt.error is String, "argument misuse")
    }
    var same = Fn.new {|buffer, expected|
      check.call(buffer.count == expected.count && buffer.toList.join(",") == expected.join(","), "primary pixels")
    }
    var source = PixelBuffer.new(3, 2)
    for (i in 0...6) source[i] = i + 1
    var dest = PixelBuffer.new(4, 3, 9)
    var op = PixelBlit.new()
    op.destinationX = 1
    op.destinationY = 1
    check.call(dest.blit(source, op) == null, "full copy")
    same.call(dest, [9,9,9,9, 9,1,2,3, 9,4,5,6])
    check.call(op.sourceWidth == 0 && op.sourceHeight == 0, "descriptor unchanged")
    op.sourceX = 1
    op.sourceY = 1
    op.destinationX = 0
    op.destinationY = 0
    dest.fill(9)
    check.call(dest.blit(source, op) == null, "remainder subregion")
    same.call(dest, [5,6,9,9, 9,9,9,9, 9,9,9,9])
    op = PixelBlit.new()
    var edge = PixelBuffer.new(2, 1, 9)
    check.call(edge.blit(source, op) == null, "right/bottom clipping")
    same.call(edge, [1,2])
    op.destinationY = -1
    op.scaleY = 2
    edge = PixelBuffer.new(3, 3, 9)
    check.call(edge.blit(source, op) == null, "top clipping and vertical scaling")
    same.call(edge, [1,2,3, 4,5,6, 4,5,6])
    op = PixelBlit.new()
    op.sourceWidth = 2
    op.sourceHeight = 2
    op.scaleX = 2
    op.destinationX = -1
    op.flipX = true
    op.flipY = true
    dest.fill(9)
    var mask = PixelMask.new(4, 3)
    mask.setBit(2, 1, true)
    mask.setBit(1, 2, true)
    op.maskX = 1
    op.maskY = 1
    check.call(dest.blit(source, mask, op) == null, "mask, flips, asymmetric scale and clip")
    same.call(dest, [9,4,4,9, 2,9,9,9, 9,9,9,9])
    same.call(source, [1,2,3, 4,5,6])
    check.call(mask.bits.bytes[0] == 2 && mask.bits.bytes[1] == 64 &&
        op.maskX == 1 && op.maskY == 1 && op.sourceWidth == 2 &&
        op.sourceHeight == 2 && op.flipX && op.flipY, "mask and descriptor unchanged")
    source.setAlternate(0, 0, 11)
    op = PixelBlit.new()
    dest.fill(9)
    var none = PixelMask.new(3, 2)
    check.call(dest.blit(source, none, op) == null && dest.hasAlternate &&
        dest.alternateAt(0) == 9, "masked operation creates alternate plane")
    check.call(dest.blit(source, op) == null && dest.alternateAt(0) == 11 &&
        dest.alternateAt(1) == 2 && dest.alternateAt(11) == 9, "alternate creation and preservation")
    source.clearAlternate()
    source[0] = PixelColor.rgb(0x80ff00)
    source[1] = PixelColor.palette(65535)
    check.call(dest.blit(source, op) == null && dest[0] == source[0] &&
        dest[1] == 65535 && dest.alternateAt(0) == source[0] &&
        dest.alternateAt(1) == 65535, "primary supplies both planes without palette conversion")
    var clipped = PixelBuffer.new(1, 1, 8)
    source.setAlternate(0, 0, 12)
    op.destinationX = 2147483647
    op.destinationY = -2147483648
    op.scaleX = 4294967295
    check.call(clipped.blit(source, op) == null && clipped[0] == 8 &&
        clipped.hasAlternate && clipped.alternateAt(0) == 8, "fully clipped allocation")
    var self = PixelBuffer.new(4, 1)
    for (i in 0...4) self[i] = i + 1
    self.fillAlternate(7)
    self.setAlternate(1, 0, 8)
    op = PixelBlit.new()
    op.sourceWidth = 3
    op.destinationX = 1
    check.call(self.blit(self, op) == null, "overlapping self copy")
    same.call(self, [1,1,2,3])
    check.call(self.alternateAt(0) == 7 && self.alternateAt(1) == 7 &&
        self.alternateAt(2) == 8 && self.alternateAt(3) == 7, "self alternate snapshot")
    op = PixelBlit.new()
    op.flipX = true
    check.call(self.blit(self, op) == null, "mirrored self copy")
    same.call(self, [3,2,1,1])
    op = PixelBlit.new()
    var failed = Fn.new {|code, fn|
      var result = fn.call()
      check.call(result is PixelError && result is Error && result.code == code &&
          result.message is String && result.toString.startsWith("PixelError"), "failure value")
      return result
    }
    var empty = PixelBuffer.new(0, 0)
    var error = failed.call(IOErr.outOfRange, Fn.new { dest.blit(empty, op) })
    failed.call(IOErr.invalidArgument, Fn.new { empty.blit(source, op) })
    for (coordinate in [3, 4294967295]) {
      op.sourceX = coordinate
      failed.call(IOErr.outOfRange, Fn.new { dest.blit(source, op) })
    }
    op = PixelBlit.new()
    op.sourceWidth = 4
    failed.call(IOErr.outOfRange, Fn.new { dest.blit(source, op) })
    op = PixelBlit.new()
    failed.call(IOErr.outOfRange, Fn.new { dest.blit(source, PixelMask.new(1, 1), op) })
    op.maskY = 3
    failed.call(IOErr.outOfRange, Fn.new { dest.blit(source, mask, op) })
    check.call(dest[0] == source[0] && dest.alternateAt(0) == source[0], "invalid regions leave pixels unchanged")
    rejects.call(Fn.new { dest.blit(null, op) })
    rejects.call(Fn.new { dest.blit(mask, op) })
    rejects.call(Fn.new { dest.blit(source, source) })
    rejects.call(Fn.new { dest.blit(source, null, op) })
    rejects.call(Fn.new { dest.blit(source, source, op) })
    rejects.call(Fn.new { error.code = 0 })
    rejects.call(Fn.new { error.message = "changed" })
    rejects.call(Fn.new { error.toString = "changed" })
    rejects.call(Fn.new { PixelError.new() })
    var created = Fn.new {
      var temporary = PixelBuffer.new(1, 1, 42)
      var result = PixelBuffer.new(1, 1)
      result.blit(temporary, PixelBlit.new())
      return result
    }.call()
    source = null
    mask = null
    op = null
    System.gc()
    check.call(created[0] == 42 && error.code == IOErr.outOfRange &&
        error.message.contains("PixelBuffer.blit"), "owned results survive GC")
    return [pass, 0]
  }

  static run() {
    var offscreen = runOffscreen()
    var pass = offscreen[0]
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
    check.call((!Screen.supports.pixels && size is Error && cellSize is Error) ||
        (size.count == 2 && cellSize.count == 2))
    if (Screen.supports.pixels) {
      var saved = Screen.readPixels(0, 0, 4, 4, true)
      var replacement = PixelColor.rgb(0x010203)
      check.call(saved is PixelBuffer && Screen.setPixel(0, 0, replacement) == null)
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
      check.call(Screen.blitPixels(source, operation) == null)
      var result = Screen.readPixels(0, 0, 4, 1)
      check.call(PixelColor.toRgb(result[0]) == 0x445566 &&
          PixelColor.toRgb(result[2]) == 0x112233 && result.hasAlternate &&
          PixelColor.toRgb(result.alternateAt(2)) == 0xaabbcc)
      var beforeMask = Screen.readPixels(2, 2, 2, 1)
      var drawMask = PixelMask.fromBits(2, 1, String.fromByte(0x80))
      operation = PixelBlit.new()
      operation.destinationX = 2
      operation.destinationY = 2
      check.call(Screen.blitPixels(source, drawMask, operation) == null)
      var afterMask = Screen.readPixels(2, 2, 2, 1)
      check.call(PixelColor.toRgb(afterMask[0]) == 0x112233 &&
          afterMask[1] == beforeMask[1])
      operation = PixelBlit.new()
      operation.destinationX = -1
      operation.scaleX = 2
      check.call(Screen.blitPixels(source, operation) == null)
      var clipped = Screen.readPixels(0, 0, 3, 1)
      check.call(PixelColor.toRgb(clipped[0]) == 0x112233 &&
          PixelColor.toRgb(clipped[1]) == 0x445566 &&
          PixelColor.toRgb(clipped[2]) == 0x445566)
      var canvas = PixelBuffer.new(2, 1, PixelColor.rgb(0))
      check.call(canvas.blit(source, drawMask, PixelBlit.new()) == null)
      operation = PixelBlit.new()
      operation.destinationY = 3
      check.call(Screen.blitPixels(canvas, operation) == null)
      var composed = Screen.readPixels(0, 3, 2, 1)
      check.call(composed[0] == PixelColor.rgb(0x112233) && composed[1] == PixelColor.rgb(0) &&
          composed.alternateAt(0) == PixelColor.rgb(0xaabbcc))
      operation = PixelBlit.new()
      check.call(Screen.blitPixels(saved, operation) == null)
      operation.destinationX = size[0]
      check.call(Screen.blitPixels(saved, operation) == null)
    }
    check.call(!Cell.new().pixelGraphics)
    return [pass, fail]
  }
}

class MenuTest {
  static run() { PixelVmTest.run() }
}
