// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, Screen, PixelBuffer, PixelColor, PixelBlit, PixelMask, MouseEvent, Mouse, Text
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
      if (line.count > 0 && Screen.checked_(Screen.textWidth(candidate, st.font)) > available) {
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
      var w = Screen.checked_(Screen.textWidth(lines[y], st.font)).min(available)
      Painter.text(surface, ((bounds.w - w) / 2).floor, top + y,
          lines[y], st, available)
    }
  }
  chrome(app, outside) {
    var st = focused ? style("button.focused") : style("button")
    var size = Screen.checked_(Screen.cellPixelSize)
    var width = bounds.w * size[0]
    var height = bounds.h * size[1]
    if (_caption == null) {
      // App has just composed our own cell surface. Capture its raster once
      // per repaint so font lookup stays with the host, then place the ink
      // at pixel precision instead of rounding padding to whole cells.
      var source = Screen.checked_(Screen.readPixels((bounds.x - 1) * size[0], (bounds.y - 1) * size[1], width, height))
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
      Screen.checked_(Screen.blitPixels(_caption, _blit))
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
    _visualRows = []
    _starts = [0]
    _layoutKey = null
  }
  setEntries(names, addresses) {
    _addresses = addresses
    _layoutKey = null
    super.items = names
    scrollTop = _rowTop
  }
  emptyMessage=(value) {
    _emptyMessage = value
    markDirty()
  }
  items=(value) { setEntries(value, []) }
  bounds=(rect) {
    _layoutKey = null
    super.bounds = rect
  }
  markInheritedThemeDirty_() {
    _layoutKey = null
    super.markInheritedThemeDirty_()
  }
  viewportRows_ { bounds == null ? 1 : bounds.h.max(1) }
  selected=(value) {
    super.selected = value
    // Home or a click on an already-selected continuation must still
    // reveal the entry after independent wheel scrolling hid its start.
    ensureVisible_()
  }
  scrollTop {
    layoutRows_()
    return _rowTop
  }
  scrollTop=(value) {
    layoutRows_()
    _rowTop = value.max(0).min((_visualRows.count - viewportRows_).max(0))
    markDirty()
  }
  ensureVisible_() {
    if (bounds == null || selected == null) return
    layoutRows_()
    var start = _starts[selected]
    var end = _starts[selected + 1]
    if (start < _rowTop || end - start > viewportRows_) {
      scrollTop = start
    } else if (end > _rowTop + viewportRows_) {
      scrollTop = end - viewportRows_
    }
  }
  centerSelectedIfHidden_() { ensureVisible_() }
  cursorVisible { false }
  cursorPos {
    if (bounds == null || selected == null) return null
    layoutRows_()
    var row = _starts[selected] - _rowTop
    if (row < 0 || row >= viewportRows_) return null
    return [bounds.x + 2, bounds.y + row]
  }
  scrollbarVisible_ {
    layoutRows_()
    return _visualRows.count > viewportRows_
  }
  rowWidth {
    layoutRows_()
    return _rowWidth
  }
  columnX { ((rowWidth - 4) / 2).floor + 2 }
  // Cached physical rows and prefix offsets are shared by painting, hit
  // testing, paging, selection visibility and the scrollbar. Reserve its
  // gutter only after measuring the full width; narrowing cannot reduce
  // the number of rows, so the second pass has a stable scrollbar decision.
  layoutRows_() {
    if (bounds == null) return
    var key = "%(bounds.w):%(bounds.h):%(Screen.font[0])"
    if (_layoutKey == key) return
    _rowWidth = bounds.w
    buildRows_()
    if (_visualRows.count > viewportRows_) {
      _rowWidth = bounds.w - 2
      buildRows_()
    }
    _rowTop = _rowTop.max(0).min((_visualRows.count - viewportRows_).max(0))
    _layoutKey = key
  }
  buildRows_() {
    _visualRows = []
    _starts = []
    var col = ((_rowWidth - 4) / 2).floor + 2
    for (index in 0...count) {
      _starts.add(_visualRows.count)
      var name = items[index]
      var address = index < _addresses.count ? _addresses[index] : ""
      if (Screen.checked_(Screen.textWidth(name, 0)) <= col - 4 &&
          Screen.checked_(Screen.textWidth(address, 0)) <= _rowWidth - col - 2) {
        _visualRows.add([index, name, address])
      } else {
        for (line in wrap_(name, _rowWidth - 4)) _visualRows.add([index, line, null])
        for (line in wrap_(address, _rowWidth - 4)) _visualRows.add([index, null, line])
      }
    }
    _starts.add(_visualRows.count)
  }
  wrap_(text, width) {
    var lines = []
    var line = ""
    var used = 0
    for (cluster in Text.graphemes(text)) {
      var cells = Screen.checked_(Screen.textWidth(cluster, 0))
      if (used + cells > width && line.count > 0) {
        lines.add(line)
        line = ""
        used = 0
      }
      line = line + cluster
      used = used + cells
    }
    lines.add(line)
    return lines
  }
  entryAt_(row) {
    layoutRows_()
    if (_visualRows.count == 0) return null
    return _visualRows[row.max(0).min(_visualRows.count - 1)][0]
  }
  pageUp() {
    if (selected == null) return
    layoutRows_()
    selected = entryAt_(_starts[selected] - viewportRows_ + 1)
  }
  pageDown() {
    if (selected == null) return
    layoutRows_()
    selected = entryAt_(_starts[selected] + viewportRows_ - 1)
  }
  onPaint_() {
    var base = ModernStyle.text(ModernStyle.ink, ModernStyle.panel)
    Painter.fill(surface, Rect.new(0, 0, bounds.w, bounds.h), " ", base)
    if (count == 0) {
      Painter.text(surface, 1, 1, _emptyMessage, base, (bounds.w - 2).max(0))
      return
    }
    for (slot in 0...viewportRows_) {
      var physical = scrollTop + slot
      if (physical >= _visualRows.count) break
      var row = _visualRows[physical]
      var index = row[0]
      var bg = index == selected ? 0xdbeafe : ModernStyle.panel
      var title = ModernStyle.text(index == selected ? ModernStyle.accent : ModernStyle.ink, bg)
      var caption = ModernStyle.text(ModernStyle.muted, bg)
      Painter.fill(surface, Rect.new(0, slot, rowWidth, 1), " ", title)
      if (row[1] != null && row[2] != null) {
        Painter.text(surface, 2, slot, row[1], title, (columnX - 4).max(0))
        Painter.text(surface, columnX, slot, row[2], caption, (rowWidth - columnX - 2).max(0))
      } else {
        Painter.text(surface, 2, slot, row[1] == null ? row[2] : row[1],
            row[1] == null ? caption : title, (rowWidth - 4).max(0))
      }
    }
    if (scrollbarVisible_) {
      Painter.fill(surface, Rect.new(rowWidth, 0, bounds.w - rowWidth, bounds.h), " ",
          ModernStyle.text(ModernStyle.muted, ModernStyle.background))
      Painter.scrollbar(surface, bounds.w - 1, 0, bounds.h, scrollTop,
          _visualRows.count, viewportRows_, effectiveTheme.glyphs, style("scrollbar.track"), style("scrollbar.thumb"))
    }
  }
  chrome(app) {
    var size = Screen.checked_(Screen.cellPixelSize)
    var width = rowWidth * size[0]
    if (_separator == null || _separator.width != width) {
      _separator = PixelBuffer.new(width, 1, PixelColor.rgb(0xe2e8f0))
      _separatorMask = PixelMask.new(width, 1)
    }
    for (slot in 0...viewportRows_) {
      var physical = scrollTop + slot
      if (physical >= _visualRows.count) break
      var index = _visualRows[physical][0]
      var rect = Rect.new(bounds.x, bounds.y + slot, rowWidth, 1)
      // A boundary line must not overwrite descenders in short bitmap fonts.
      // Inspect our freshly composed row before drawing any decorations.
      var bottom = (rect.y - 1) * size[1] + size[1] - 1
      var pixels = Screen.checked_(Screen.readPixels((rect.x - 1) * size[0], bottom, width, 1))
      var boundary = physical + 1 == _visualRows.count || _visualRows[physical + 1][0] != index
      if (pixels != null && boundary && slot < viewportRows_ - 1) {
        var bg = index == selected ? 0xdbeafe : ModernStyle.panel
        for (x in 0...width) _separatorMask[x] = (pixels[x] & 0xffffff) == bg
        _blit.destinationX = (rect.x - 1) * size[0]
        _blit.destinationY = bottom
        Screen.checked_(Screen.blitPixels(_separator, _separatorMask, _blit))
      }
      if (index == selected && (slot == 0 || _visualRows[physical - 1][0] != index)) {
        var lines = (_starts[index + 1] - scrollTop).min(viewportRows_) - slot
        var height = lines * size[1] - 4
        if (_indicator == null || _indicator.height != height) {
          _indicator = PixelBuffer.new(3, height, PixelColor.rgb(ModernStyle.accent))
        }
        _blit.destinationX = (rect.x - 1) * size[0] + 6
        _blit.destinationY = (rect.y - 1) * size[1] + 2
        Screen.checked_(Screen.blitPixels(_indicator, _blit))
      }
    }
    var first = count > 0 && entryAt_(scrollTop) == selected ? 0xdbeafe : ModernStyle.panel
    var end = scrollTop + viewportRows_ - 1
    var last = end < _visualRows.count && entryAt_(end) == selected ? 0xdbeafe : ModernStyle.panel
    app.round(Rect.new(bounds.x, bounds.y, rowWidth, bounds.h), first, last, ModernStyle.background)
    if (!scrollbarVisible_) return
    var key = "%(bounds.h):%(size):%(_visualRows.count):%(scrollTop)"
    if (_scrollbarKey != key) {
      var height = bounds.h * size[1]
      _scrollbar = PixelBuffer.new(size[0], height, PixelColor.rgb(ModernStyle.background))
      var thumbHeight = (height * viewportRows_ / _visualRows.count).floor.max(12)
      var top = ((height - thumbHeight) * scrollTop / (_visualRows.count - viewportRows_)).round
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
    Screen.checked_(Screen.blitPixels(_scrollbar, _blit))
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
      scrollTop = ((event.startY - bounds.y) * (_visualRows.count - viewportRows_) / (bounds.h - 1).max(1)).round
      return true
    }
    var row = event.startY - bounds.y
    var physical = scrollTop + row
    if (physical >= _visualRows.count) return true
    var index = entryAt_(physical)
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
  dispatchPhysical_(event) {
    if (modalStack.count == 0 && _shortcut != null && _shortcut.call(event)) return true
    return super.dispatchPhysical_(event)
  }
  dispatchKey_(event) {
    if (modalStack.count == 0 && _shortcut != null && _shortcut.call(event)) return true
    return super.dispatchKey_(event)
  }
  drawAll_() {
    Screen.update { drawFrame_() }
  }
  drawFrame_() {
    super.drawAll_()
    if (modalStack.count == 0 && _chrome != null && Screen.checked_(Screen.supports.pixels)) {
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
    if (!Screen.checked_(Screen.supports.pixels) || rect == null || rect.w < 2 || rect.h < 1) return
    var size = Screen.checked_(Screen.cellPixelSize)
    for (y in 0...2) {
      var corner = corner_(y == 0 ? top : bottom, outside)
      for (x in 0...2) {
        _blit.destinationX = (rect.x - 1) * size[0] + (x == 0 ? 0 : rect.w * size[0] - 6)
        _blit.destinationY = (rect.y - 1) * size[1] + (y == 0 ? 0 : rect.h * size[1] - 6)
        _blit.flipX = x != 0
        _blit.flipY = y != 0
        Screen.checked_(Screen.blitPixels(corner, _blit))
      }
    }
  }
}
