// SyncTERM Wren UI library — LogView.
//
// Append-only colored-line scroller with a fixed viewport and a ring-
// buffered backing store.  Designed for the transfer-window log
// channel (zmodem / xmodem / CET status messages) but reusable for
// any "splat lines as they happen, color by severity" surface.
//
//   var lv = LogView.new()
//   lv.bounds = Rect.new(2, 4, 60, 8)
//   lv.append(LogView.LEVEL_NOTICE,  "Receiving foo.bin (12345 bytes)")
//   lv.append(LogView.LEVEL_ERROR,   "Bad CRC on frame 42, retrying")
//
// Severity levels mirror syslog priorities — the same numeric scale
// used by the C-side `lputs(level, str)` callback, which is what the
// transfer mailbox will hand us per line.  Only four bins are
// distinguished visually:
//
//   level <= 3 (emerg / alert / crit / err)   — light red on blue
//   level == 4 (warning)                       — light magenta on blue
//   level == 5 (notice)                        — yellow on blue
//   level >= 6 (info / debug / etc.)           — white on blue
//
// Scroll-back is keyboard-driven (PgUp / PgDn / Up / Down / Home /
// End).  The live-tail mode is `scrollTop == null`; entering scroll
// mode anchors the view at the current bottom and `append` no longer
// auto-tails until the user returns to live with End.
//
// A vertical scrollbar (track + arrow caps + thumb) appears on the
// right edge whenever the ring's contents exceed the viewport height,
// matching ListView's default layout.  Mouse wheel scrolls the viewport
// by one page over the scrollbar and by the normal step over content;
// click + drag on the scrollbar column jumps the view.
// One column of frame separator sits between the scrollbar and the
// log text so they don't run together visually.
//
// Theme roles:
//   logview.error     — severity <= 3
//   logview.warning   — severity == 4
//   logview.notice    — severity == 5
//   logview.info      — severity >= 6 (default cascade)

import "ui_widget" for Widget, Rect
import "ui_draw"   for Painter
import "syncterm"  for Key, KeyEvent, Mouse, MouseEvent, Screen

class LogView is Widget {
  // Severity constants — mirror the C-side LOG_* macros so callers
  // that proxy lputs() into LogView.append() pass the level through
  // unchanged.
  static LEVEL_EMERG   { 0 }
  static LEVEL_ALERT   { 1 }
  static LEVEL_CRIT    { 2 }
  static LEVEL_ERR     { 3 }
  static LEVEL_WARNING { 4 }
  static LEVEL_NOTICE  { 5 }
  static LEVEL_INFO    { 6 }
  static LEVEL_DEBUG   { 7 }

  construct new() {
    super()
    focusable    = true
    _capacity    = 256
    _ring        = []     // entries are [level, text]
    _scrollAnchor = null   // null = live tail; otherwise [entry, codepoint]
  }

  // Maximum lines retained in the ring.  Setting smaller than the
  // current count drops the oldest lines.
  capacity     { _capacity }
  capacity=(n) {
    _capacity = n
    trimToCapacity_()
    markDirty()
  }

  // Read-only view of how many lines are currently buffered.
  count { _ring.count }

  // Public scroll position; null when live-tailing.
  scrollTop {
    if (_scrollAnchor == null) return null
    var layout = layout_
    return effectiveScrollTop_(layout[0])
  }

  cursorVisible { false }

  // Append a new line.  When in live-tail mode the view auto-follows
  // the latest entry; in scroll mode the anchor stays put (so the
  // user can read older entries without the view jumping out from
  // under them).  Anchors identify the entry and codepoint at the top
  // of the view, so dropping or rewrapping preceding content does not
  // change the surviving text being read.
  append(level, text) {
    _ring.add([level, text])
    trimToCapacity_()
    markDirty()
  }

  trimToCapacity_() {
    var dropped = 0
    while (_ring.count > _capacity) {
      _ring.removeAt(0)
      dropped = dropped + 1
    }
    if (_scrollAnchor == null || dropped == 0) return
    if (_ring.count == 0) {
      _scrollAnchor = null
    } else if (_scrollAnchor[0] < dropped) {
      _scrollAnchor = [0, 0]
    } else {
      _scrollAnchor = [_scrollAnchor[0] - dropped, _scrollAnchor[1]]
    }
  }

  // Drop everything and return to live mode.
  clear() {
    _ring.clear()
    _scrollAnchor = null
    markDirty()
  }

  // Force live-tail mode (no anchor).
  goLive() {
    _scrollAnchor = null
    markDirty()
  }

  // Scroll by `delta` visual rows (negative = up = older content).
  // Going up from live mode anchors at the top of the currently-visible
  // window (so the next PgDn returns to live cleanly).  Going down
  // from the last anchor that would still fit in viewport snaps back
  // to live.  Counts are in WRAPPED visual rows, not logical entries.
  scrollBy(delta) {
    if (bounds == null) return
    var rows = layout_[0]
    var n  = rows.count
    var vh = bounds.h
    if (n <= vh) return
    var maxTop = n - vh
    var top = effectiveScrollTop_(rows)
    var target = (top + delta).max(0).min(maxTop)
    if (target == maxTop && delta > 0) {
      _scrollAnchor = null
    } else {
      _scrollAnchor = anchorForRow_(rows, target)
    }
    markDirty()
  }

  // Effective top-of-viewport visual-row index given the current scroll
  // mode.  In live mode this is `visualRowCount - viewport` (or 0
  // when content fits); in scroll mode it's the explicit anchor.
  effectiveScrollTop_(rows) {
    var n = rows.count
    if (n == 0) return 0
    var vh = bounds == null ? 0 : bounds.h
    var maxTop = (n - vh).max(0)
    if (_scrollAnchor == null) return maxTop
    return rowForAnchor_(rows, _scrollAnchor).min(maxTop)
  }

  // Split one entry into visual rows.  Each row is
  // [entry index, starting codepoint, text].  Screen.textWidth uses
  // the same font lookup and fallback-width rules as Painter.text.
  layoutEntry_(entryIndex, width) {
    var entry = _ring[entryIndex]
    var text = entry[1]
    var st = styleForLevel_(entry[0])
    var font = st.font == null ? 0 : st.font
    var rows = []
    if (width <= 0) {
      rows.add([entryIndex, 0, text])
      return rows
    }
    var chunk = ""
    var columns = 0
    var offset = 0
    var start = 0
    for (cp in text) {
      var cpWidth = Screen.textWidth(cp, font)
      if (chunk.count > 0 && columns + cpWidth > width) {
        rows.add([entryIndex, start, chunk])
        chunk = ""
        columns = 0
        start = offset
      }
      chunk = chunk + cp
      columns = columns + cpWidth
      offset = offset + 1
      if (columns >= width) {
        rows.add([entryIndex, start, chunk])
        chunk = ""
        columns = 0
        start = offset
      }
    }
    if (chunk.count > 0 || rows.count == 0) {
      rows.add([entryIndex, start, chunk])
    }
    return rows
  }

  layoutAtWidth_(width) {
    var rows = []
    for (i in 0..._ring.count) {
      for (row in layoutEntry_(i, width)) rows.add(row)
    }
    return rows
  }

  // Return [visual rows, content width, scrollbar visible].  Probe at
  // the scrollbar width first so showing the bar cannot oscillate with
  // the wider no-scrollbar layout.
  layout_ {
    if (bounds == null) return [[], 0, false]
    var contentWidth = (bounds.w - 2).max(0)
    var probe = layoutAtWidth_(contentWidth.max(1))
    var showScrollbar = bounds.h > 0 && probe.count > bounds.h
    if (showScrollbar) {
      if (contentWidth == 0) probe = layoutAtWidth_(0)
      return [probe, contentWidth, true]
    }
    return [layoutAtWidth_(bounds.w.max(0)), bounds.w.max(0), false]
  }

  anchorForRow_(rows, row) {
    if (rows.count == 0) return null
    var clamped = row.max(0).min(rows.count - 1)
    return [rows[clamped][0], rows[clamped][1]]
  }

  rowForAnchor_(rows, anchor) {
    if (rows.count == 0) return 0
    var result = 0
    for (i in 0...rows.count) {
      var row = rows[i]
      if (row[0] > anchor[0]) return result
      if (row[0] == anchor[0]) {
        if (row[1] > anchor[1]) return result
        result = i
      }
    }
    return result
  }

  // Wheel-step (rows) for mouse-wheel scrolling.  Matches the
  // ListView default of three rows per click.
  static wheelStep { 3 }

  // Layout columns when the scrollbar is visible.
  //   col 0 .. w-3    — log text content
  //   col w-2         — frame separator (single glyph column)
  //   col w-1         — scrollbar track + arrow caps + thumb
  // When the scrollbar is hidden, content spans cols 0 .. w-1.
  scrollbarColumn_ { bounds.w - 1 }
  separatorColumn_ { bounds.w - 2 }
  contentColumn_   { 0 }

  styleForLevel_(level) {
    if (level <= LogView.LEVEL_ERR)     return style("logview.error")
    if (level == LogView.LEVEL_WARNING) return style("logview.warning")
    if (level == LogView.LEVEL_NOTICE)  return style("logview.notice")
    return style("logview.info")
  }

  onPaint_() {
    var sf = surface
    var w  = bounds.w
    var h  = bounds.h
    var cx = contentColumn_
    var layout = layout_
    var rows = layout[0]
    var cw = layout[1]
    var showScrollbar = layout[2]

    // Default background: blank cells with the info style (cascades to
    // default if no role).  Severity-tinted lines paint over the
    // content area; the scrollbar column draws its own track / thumb.
    var bg = style("logview.info")
    Painter.fill(sf, Rect.new(0, 0, w, h), " ", bg)

    // Paint the shared visual-row layout so rendering, navigation and
    // scrollbar geometry cannot disagree about wrapping.
    var top = effectiveScrollTop_(rows)
    var painted = 0
    while (top + painted < rows.count && painted < h) {
      var row = rows[top + painted]
      var st = styleForLevel_(_ring[row[0]][0])
      Painter.fill(sf, Rect.new(cx, painted, cw, 1), " ", st)
      Painter.text(sf, cx, painted, row[2], st, cw)
      painted = painted + 1
    }

    if (showScrollbar) {
      var glyphs     = effectiveTheme.glyphs
      var trackStyle = style("scrollbar.track")
      var thumbStyle = style("scrollbar.thumb")
      Painter.scrollbar(sf, scrollbarColumn_, 0, h,
                        effectiveScrollTop_(rows), rows.count, h,
                        glyphs, trackStyle, thumbStyle)
      // Single-column separator between scrollbar and content.
      var sepStyle = style("default")
      var sepGlyph = glyph("scrollbar.separator")
      var sx       = separatorColumn_
      var j = 0
      while (j < h) {
        Painter.fill(sf, Rect.new(sx, j, 1, 1), sepGlyph, sepStyle)
        j = j + 1
      }
    }
  }

  // Move the absolute scroll anchor to `target`, clamping into the
  // valid range and snapping back to live mode when the target lands
  // at (or past) the natural tail.  Internal helper used by the
  // mouse-driven scroll path.
  setScrollTo_(target, rows) {
    var n  = rows.count
    var vh = bounds.h
    if (n <= vh) return
    var maxTop = n - vh
    var t      = target.max(0).min(maxTop)
    if (t == maxTop) {
      _scrollAnchor = null
    } else {
      _scrollAnchor = anchorForRow_(rows, t)
    }
    markDirty()
  }

  handleMouse_(me) {
    if (bounds == null) return false
    var e = me.event
    var layout = layout_
    var rows = layout[0]

    var overScrollbar = layout[2] &&
        me.startX == bounds.x + scrollbarColumn_

    // Wheel: use the normal row step over content and a full viewport
    // over the scrollbar.
    if (e == Mouse.wheelUp) {
      var step = (overScrollbar ? bounds.h.max(1) : LogView.wheelStep) *
          me.wheelY.abs.max(1)
      scrollBy(-step)
      return true
    }
    if (e == Mouse.wheelDown) {
      var step = (overScrollbar ? bounds.h.max(1) : LogView.wheelStep) *
          me.wheelY.abs.max(1)
      scrollBy(step)
      return true
    }

    // Beyond the wheel, only react to button-1 press / click / drag.
    if (e != Mouse.button1Press && e != Mouse.button1Click &&
        e != Mouse.button1DragStart && e != Mouse.button1DragMove) {
      return false
    }

    // Hit-test the GRAB by `start` so dragging off the scrollbar still
    // scrolls; resolve the new scrollTop from the CURRENT pointer Y
    // (`end`).  The scrollbar is at column scrollbarColumn_ in widget-
    // local coords; convert against bounds.x to get screen coords.
    if (overScrollbar) {
      var py = me.endY - bounds.y
      var t  = Painter.scrollbarClick(py, bounds.h, rows.count,
                                      bounds.h, effectiveScrollTop_(rows))
      setScrollTo_(t, rows)
      return true
    }

    return false
  }

  handle(ev) {
    if (ev is MouseEvent) return handleMouse_(ev)
    if (!(ev is KeyEvent)) return false
    var c = ev.shortcut_
    if (c == Key.pageUp) {
      scrollBy(-bounds.h)
      return true
    }
    if (c == Key.pageDown) {
      scrollBy(bounds.h)
      return true
    }
    if (c == Key.up) {
      scrollBy(-1)
      return true
    }
    if (c == Key.down) {
      scrollBy(1)
      return true
    }
    if (c == Key.home) {
      if (_ring.count > 0) {
        _scrollAnchor = [0, 0]
        markDirty()
      }
      return true
    }
    if (c == Key.end) {
      goLive()
      return true
    }
    return false
  }
}
