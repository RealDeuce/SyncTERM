// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, Screen, PixelBuffer, PixelColor, PixelBlit, PixelMask, MouseEvent, Mouse
import "ui_style" for Theme, Style
import "ui_widget" for Rect, Widget, Container
import "ui_draw" for Painter
import "ui_button" for Button
import "ui_app" for App
import "ui_list" for ListView

class ModernStyle {
  static background { 0xf1f5f9 }
  static panel { 0xffffff }
  static ink { 0x0f172a }
  static muted { 0x64748b }
  static accent { 0x2563eb }
  static dark { 0x0f172a }
  static text(fg, bg) { Style.new(0, 7, fg, bg) }
  static theme {
    var roles = {}
    for (key in Theme.current.roles.keys) roles[key] = Theme.current.roles[key]
    for (key in roles.keys.toList) {
      if (key.endsWith(".inactive")) roles[key] = text(muted, panel)
    }
    roles["default"] = text(ink, panel)
    roles["default.inactive"] = text(muted, panel)
    roles["frame"] = text(0xcbd5e1, panel)
    roles["title"] = text(accent, panel)
    roles["title.inactive"] = text(muted, panel)
    roles["input"] = text(ink, 0xe2e8f0)
    roles["input.focused"] = text(ink, 0xdbeafe)
    roles["input.selection"] = text(panel, accent)
    roles["list.item"] = text(ink, panel)
    roles["list.item.focused"] = text(accent, 0xdbeafe)
    roles["list.item.selected"] = text(accent, 0xdbeafe)
    roles["button"] = text(accent, 0xdbeafe)
    roles["button.focused"] = text(panel, accent)
    roles["button.hotkey"] = roles["button"]
    roles["button.hotkey.focused"] = roles["button.focused"]
    roles["scrollbar"] = text(0xcbd5e1, panel)
    roles["scrollbar.track"] = text(0xcbd5e1, panel)
    roles["scrollbar.separator"] = text(0xcbd5e1, panel)
    roles["scrollbar.thumb"] = text(muted, panel)
    var glyphs = {}
    var lines = {"╔": "┌", "╗": "┐", "╚": "└", "╝": "┘", "═": "─", "║": "│",
        "╠": "├", "╣": "┤", "╦": "┬", "╩": "┴", "╬": "┼"}
    for (row in Host.defaultThemeData[1]) {
      var symbol = row[1]
      if (row[0].startsWith("frame.control.") && lines.containsKey(symbol)) symbol = lines[symbol]
      glyphs[row[0]] = [symbol, row[2]]
    }
    // Shared editors keep their tested behavior, with a lighter frame style.
    for (name in ["topLeft", "topRight", "bottomLeft", "bottomRight"]) {
      var symbol = "┌"
      if (name == "topRight") symbol = "┐"
      if (name == "bottomLeft") symbol = "└"
      if (name == "bottomRight") symbol = "┘"
      glyphs["frame.control." + name] = [symbol, "+"]
    }
    for (name in ["top", "bottom"]) glyphs["frame.control." + name] = ["─", "-"]
    for (name in ["left", "right"]) glyphs["frame.control." + name] = ["│", "|"]
    glyphs["frame.control.title.left"] = " "
    glyphs["frame.control.title.right"] = " "
    return Theme.new(roles, glyphs)
  }
}

class ModernButton is Button {
  construct new(label) {
    super(label)
    hotkeyIdx = -1
    _caption = null
    _blit = PixelBlit.new()
  }
  onPaint_() {
    _caption = null
    var st = focused ? style("button.focused") : style("button")
    Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ", st)
    var available = (bounds.w - 2).max(1)
    var lines = []
    var line = ""
    for (word in label.split(" ")) {
      var candidate = line.count == 0 ? word : line + " " + word
      if (line.count > 0 && Screen.textWidth(candidate, st.font) > available) {
        lines.add(line)
        line = word
      } else {
        line = candidate
      }
    }
    lines.add(line)
    var rows = lines.count.min(bounds.h)
    var top = ((bounds.h - rows) / 2).floor
    _textRows = rows
    _textTop = top
    for (y in 0...rows) {
      var w = Screen.textWidth(lines[y], st.font).min(available)
      Painter.text(surface, ((bounds.w - w) / 2).floor, top + y,
          lines[y], st, available)
    }
  }
  chrome(app, outside) {
    var st = focused ? style("button.focused") : style("button")
    var size = Screen.cellPixelSize
    var width = bounds.w * size[0]
    var height = bounds.h * size[1]
    if (_caption == null) {
      // App has just composed our own cell surface. Capture its raster once
      // per repaint so font lookup stays with the host, then place the ink
      // at pixel precision instead of rounding padding to whole cells.
      var source = Screen.readPixels((bounds.x - 1) * size[0], (bounds.y - 1) * size[1], width, height)
      if (source != null) {
        _caption = PixelBuffer.new(width, height, PixelColor.rgb(st.bgRgb))
        var boxes = []
        var minY = height
        var maxY = -1
        for (line in 0..._textRows) {
          var box = [width, -1, height, -1]
          for (y in ((_textTop + line) * size[1])...((_textTop + line + 1) * size[1])) {
            for (x in 0...width) {
              if ((source.pixelAt(x, y) & 0xffffff) == st.bgRgb) continue
              box[0] = box[0].min(x)
              box[1] = box[1].max(x)
              box[2] = box[2].min(y)
              box[3] = box[3].max(y)
            }
          }
          boxes.add(box)
          minY = minY.min(box[2])
          maxY = maxY.max(box[3])
        }
        var dy = ((height - (maxY - minY + 1)) / 2).floor - minY
        for (box in boxes) {
          if (box[1] < box[0]) continue
          var dx = ((width - (box[1] - box[0] + 1)) / 2).floor - box[0]
          for (y in box[2]..box[3]) {
            for (x in box[0]..box[1]) _caption.setPixel(x + dx, y + dy, source.pixelAt(x, y))
          }
        }
      }
    }
    if (_caption != null) {
      _blit.destinationX = (bounds.x - 1) * size[0]
      _blit.destinationY = (bounds.y - 1) * size[1]
      Screen.blitPixels(_caption, _blit)
    }
    app.round(bounds, st.bgRgb, outside)
  }
}

// Keep ListView's keyboard/search contract, with a separate viewport so
// wheel scrolling can leave the selected connection outside the viewport.
class ModernList is ListView {
  construct new() {
    super()
    _rowTop = 0
    _addresses = []
    _emptyMessage = ""
    _blit = PixelBlit.new()
    _indicator = null
    _scrollbar = null
    _scrollbarKey = null
    _separator = null
    _separatorMask = null
  }
  addresses=(value) {
    _addresses = value
    markDirty()
  }
  emptyMessage=(value) {
    _emptyMessage = value
    markDirty()
  }
  items=(value) {
    super.items = value
    scrollTop = scrollTop
  }
  viewportRows_ { bounds == null ? 1 : bounds.h.max(1) }
  scrollTop { _rowTop }
  scrollTop=(value) {
    _rowTop = value.max(0).min((count - viewportRows_).max(0))
    markDirty()
  }
  ensureVisible_() {
    if (bounds == null || selected == null) return
    if (selected < scrollTop) scrollTop = selected
    if (selected >= scrollTop + viewportRows_) scrollTop = selected - viewportRows_ + 1
  }
  centerSelectedIfHidden_() { ensureVisible_() }
  cursorVisible { false }
  scrollbarVisible_ { count > viewportRows_ }
  rowWidth { bounds.w - (scrollbarVisible_ ? 2 : 0) }
  columnX { ((rowWidth - 4) / 2).floor + 2 }
  onPaint_() {
    var base = ModernStyle.text(ModernStyle.ink, ModernStyle.panel)
    Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ", base)
    if (count == 0) {
      Painter.text(surface, 1, 1, _emptyMessage, base, (bounds.w - 2).max(0))
      return
    }
    for (slot in 0...viewportRows_) {
      var index = scrollTop + slot
      if (index >= count) break
      var bg = index == selected ? 0xdbeafe : ModernStyle.panel
      var title = ModernStyle.text(index == selected ? ModernStyle.accent : ModernStyle.ink, bg)
      var caption = ModernStyle.text(ModernStyle.muted, bg)
      Painter.fill(surface, Rect.new(0, slot, rowWidth, 1), " ", title)
      Painter.text(surface, 2, slot, items[index], title, (columnX - 4).max(0))
      if (index < _addresses.count) {
        Painter.text(surface, columnX, slot, _addresses[index], caption, (rowWidth - columnX - 2).max(0))
      }
    }
    if (scrollbarVisible_) {
      Painter.fill(surface, Rect.new(rowWidth, 0, bounds.w - rowWidth, bounds.h), " ",
          ModernStyle.text(ModernStyle.muted, ModernStyle.background))
      Painter.scrollbar(surface, bounds.w - 1, 0, bounds.h, scrollTop,
          count, viewportRows_, effectiveTheme.glyphs, style("scrollbar.track"), style("scrollbar.thumb"))
    }
  }
  chrome(app) {
    var size = Screen.cellPixelSize
    var width = rowWidth * size[0]
    if (_separator == null || _separator.width != width) {
      _separator = PixelBuffer.new(width, 1, PixelColor.rgb(0xe2e8f0))
      _separatorMask = PixelMask.new(width, 1)
    }
    for (slot in 0...viewportRows_) {
      var index = scrollTop + slot
      if (index >= count) break
      var rect = Rect.new(bounds.x, bounds.y + slot, rowWidth, 1)
      // A boundary line must not overwrite descenders in short bitmap fonts.
      // Inspect our freshly composed row before drawing any decorations.
      var bottom = (rect.y - 1) * size[1] + size[1] - 1
      var pixels = Screen.readPixels((rect.x - 1) * size[0], bottom, width, 1)
      if (pixels != null && slot < viewportRows_ - 1) {
        var bg = index == selected ? 0xdbeafe : ModernStyle.panel
        for (x in 0...width) _separatorMask[x] = (pixels[x] & 0xffffff) == bg
        _blit.destinationX = (rect.x - 1) * size[0]
        _blit.destinationY = bottom
        Screen.blitPixels(_separator, _separatorMask, _blit)
      }
      if (index == selected) {
        var height = size[1] - 4
        if (_indicator == null || _indicator.height != height) {
          _indicator = PixelBuffer.new(3, height, PixelColor.rgb(ModernStyle.accent))
        }
        _blit.destinationX = (rect.x - 1) * size[0] + 6
        _blit.destinationY = (rect.y - 1) * size[1] + 2
        Screen.blitPixels(_indicator, _blit)
      }
    }
    var first = selected == scrollTop ? 0xdbeafe : ModernStyle.panel
    var last = selected == scrollTop + viewportRows_ - 1 ? 0xdbeafe : ModernStyle.panel
    app.round(Rect.new(bounds.x, bounds.y, rowWidth, bounds.h), first, last, ModernStyle.background)
    if (!scrollbarVisible_) return
    var key = "%(bounds.h):%(size):%(count):%(scrollTop)"
    if (_scrollbarKey != key) {
      var height = bounds.h * size[1]
      _scrollbar = PixelBuffer.new(size[0], height, PixelColor.rgb(ModernStyle.background))
      var thumbHeight = (height * viewportRows_ / count).floor.max(12)
      var top = ((height - thumbHeight) * scrollTop / (count - viewportRows_)).round
      var center = (size[0] / 2).floor
      for (y in 0...height) {
        for (x in (center - 1)...(center + 1)) _scrollbar.setPixel(x, y, PixelColor.rgb(0xcbd5e1))
      }
      for (y in top...(top + thumbHeight)) {
        for (x in (center - 2)...(center + 2)) _scrollbar.setPixel(x, y, PixelColor.rgb(ModernStyle.muted))
      }
      _scrollbarKey = key
    }
    _blit.destinationX = (bounds.x + bounds.w - 2) * size[0]
    _blit.destinationY = (bounds.y - 1) * size[1]
    Screen.blitPixels(_scrollbar, _blit)
  }
  handle(event) {
    if (!(event is MouseEvent)) return super.handle(event)
    if (!hit(event.startX, event.startY)) return false
    if (event.event == Mouse.wheelUp || event.event == Mouse.wheelDown) {
      var direction = event.event == Mouse.wheelUp ? -1 : 1
      scrollTop = scrollTop + direction * event.wheelY.abs.max(1)
      return true
    }
    if (event.event != Mouse.button1Click) return false
    if (scrollbarVisible_ && event.startX >= bounds.x + bounds.w - 2) {
      scrollTop = ((event.startY - bounds.y) * (count - viewportRows_) / (bounds.h - 1).max(1)).round
      return true
    }
    var row = event.startY - bounds.y
    var index = scrollTop + row
    if (parent is Container) parent.focusedIndex = parent.children.indexOf(this)
    if (index >= 0 && index < count) selected = index
    return true
  }
}

class ModernCanvas is Container {
  construct new(paint) {
    super()
    _paint = paint
  }
  onPaint_() {
    _paint.call(surface)
    compositeChildren_()
  }
}

class ModernLabel is Widget {
  construct new(paint) {
    super()
    _paint = paint
    focusable = false
  }
  onPaint_() { _paint.call(surface) }
}

class ModernApp is App {
  construct new() {
    super()
    _chrome = null
    _shortcut = null
    _corners = {}
    _blit = PixelBlit.new()
    theme = ModernStyle.theme
  }
  chrome=(fn) { _chrome = fn }
  shortcut=(fn) { _shortcut = fn }
  dispatchKey_(event) {
    if (modalStack.count == 0 && _shortcut != null && _shortcut.call(event)) return true
    return super.dispatchKey_(event)
  }
  drawAll_() {
    Screen.update { drawFrame_() }
  }
  drawFrame_() {
    super.drawAll_()
    if (modalStack.count == 0 && _chrome != null && Screen.supports.pixels) {
      _chrome.call()
    }
  }
  round(rect, colour, outside) {
    round(rect, colour, colour, outside)
  }
  corner_(colour, outside) {
    var key = "%(colour):%(outside)"
    var corner = _corners[key]
    if (corner == null) {
      corner = PixelBuffer.new(6, 6, PixelColor.rgb(outside))
      for (y in 0...6) {
        for (x in 0...6) {
          // Integrate coverage inside each pixel, then composite against
          // the actual surrounding colour. Buffers are cached per pair.
          var coverage = 0
          for (sy in 0...4) {
            for (sx in 0...4) {
              var dx = 6 - x - (sx + 0.5) / 4
              var dy = 6 - y - (sy + 0.5) / 4
              if (dx * dx + dy * dy <= 36) coverage = coverage + 1
            }
          }
          var rgb = 0
          for (shift in [16, 8, 0]) {
            var fg = (colour >> shift) & 255
            var bg = (outside >> shift) & 255
            rgb = rgb | (((fg * coverage + bg * (16 - coverage)) / 16).round << shift)
          }
          corner.setPixel(x, y, PixelColor.rgb(rgb))
        }
      }
      _corners[key] = corner
    }
    return corner
  }
  round(rect, top, bottom, outside) {
    if (!Screen.supports.pixels || rect == null || rect.w < 2 || rect.h < 1) return
    var size = Screen.cellPixelSize
    for (y in 0...2) {
      var corner = corner_(y == 0 ? top : bottom, outside)
      for (x in 0...2) {
        _blit.destinationX = (rect.x - 1) * size[0] + (x == 0 ? 0 : rect.w * size[0] - 6)
        _blit.destinationY = (rect.y - 1) * size[1] + (y == 0 ? 0 : rect.h * size[1] - 6)
        _blit.flipX = x != 0
        _blit.flipY = y != 0
        Screen.blitPixels(corner, _blit)
      }
    }
  }
}
