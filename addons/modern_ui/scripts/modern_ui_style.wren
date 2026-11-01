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
  }
  onPaint_() {
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
    for (y in 0...rows) {
      var w = Screen.textWidth(lines[y], st.font).min(available)
      Painter.text(surface, ((bounds.w - w) / 2).floor, top + y,
          lines[y], st, available)
    }
  }
}

// A directory row click selects; the explicit Connect action or Enter dials.
class ModernList is ListView {
  construct new() { super() }
  handle(event) {
    if (event is MouseEvent && event.event == Mouse.button1Click &&
        hit(event.startX, event.startY) &&
        (!scrollbarVisible_ || event.startX != bounds.x + scrollbarColumn_)) {
      var index = scrollTop + event.startY - bounds.y
      if (parent is Container) parent.focusedIndex = parent.children.indexOf(this)
      if (index >= 0 && index < count) selected = index
      return true
    }
    return super.handle(event)
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
          if ((5 - x) * (5 - x) + (5 - y) * (5 - y) <= 25) {
            corner.setPixel(x, y, PixelColor.rgb(colour))
          }
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
