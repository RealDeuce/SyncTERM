// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, Screen, PixelBuffer, PixelColor, PixelBlit, MouseEvent, Mouse
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

// Directory cards use ListView's keyboard/search contract, but own their
// viewport in items rather than treating a terminal row as one item.
class ModernList is ListView {
  construct new() {
    super()
    _cardTop = 0
    _subtitles = []
    _emptyMessage = ""
    _blit = PixelBlit.new()
    _indicator = null
    _scrollbar = null
    _scrollbarKey = null
  }
  subtitles=(value) {
    _subtitles = value
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
  viewportRows_ { bounds == null ? 1 : (bounds.h / 3).floor.max(1) }
  scrollTop { _cardTop }
  scrollTop=(value) {
    _cardTop = value.max(0).min((count - viewportRows_).max(0))
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
  cardWidth { bounds.w - (scrollbarVisible_ ? 2 : 0) }
  onPaint_() {
    var base = ModernStyle.text(ModernStyle.ink, ModernStyle.background)
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
      Painter.fill(surface, Rect.new(0, slot * 3, cardWidth, 2), " ", title)
      Painter.text(surface, 2, slot * 3, items[index], title, (cardWidth - 4).max(0))
      if (index < _subtitles.count) {
        Painter.text(surface, 2, slot * 3 + 1, _subtitles[index], caption, (cardWidth - 4).max(0))
      }
    }
    if (scrollbarVisible_) {
      Painter.scrollbar(surface, bounds.w - 1, 0, bounds.h, scrollTop,
          count, viewportRows_, effectiveTheme.glyphs, style("scrollbar.track"), style("scrollbar.thumb"))
    }
  }
  chrome(app) {
    var size = Screen.cellPixelSize
    for (slot in 0...viewportRows_) {
      var index = scrollTop + slot
      if (index >= count) break
      var rect = Rect.new(bounds.x, bounds.y + slot * 3, cardWidth, 2)
      app.round(rect, index == selected ? 0xdbeafe : ModernStyle.panel, ModernStyle.background)
      if (index == selected) {
        var height = 2 * size[1] - 12
        if (_indicator == null || _indicator.height != height) {
          _indicator = PixelBuffer.new(3, height, PixelColor.rgb(ModernStyle.accent))
        }
        _blit.destinationX = (rect.x - 1) * size[0] + 6
        _blit.destinationY = (rect.y - 1) * size[1] + 6
        Screen.blitPixels(_indicator, _blit)
      }
    }
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
    if (row % 3 == 2) return true
    var index = scrollTop + (row / 3).floor
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
    super.drawAll_()
    if (modalStack.count == 0 && _chrome != null && Screen.supports.pixels) {
      _chrome.call()
    }
  }
  round(rect, colour, outside) {
    if (!Screen.supports.pixels || rect == null || rect.w < 2 || rect.h < 1) return
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
    var size = Screen.cellPixelSize
    for (y in 0...2) {
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
