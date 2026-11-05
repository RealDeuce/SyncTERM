// Common base for every recoverable-failure value type
// (FileError, SFTPError, WONError, ConnError, …).  Lets a script
// catch any of them with a single `if (v is Error)` check instead of
// listing each subclass.  Every concrete subclass guarantees the
// shape `{ code: Num?, message: String?, toString: String }`; MenuError
// leaves code null when C supplies no portable reason. The
// type-specific extras (errno, offset, partial, serverStatus,
// isTransient, …) live on the subclass.
//
// Empty by design: foreign Error subclasses (FileError etc.) need
// the parent to be field-free or Wren rejects the inheritance at
// module-load time (wren_vm.c:559-563).
class Error {}

class Text {
  foreign static graphemes(text)
  foreign static normalizedByteCount(text)
  foreign static caselessKey(text)
  foreign static caselessEqual(left, right)
  foreign static caselessStartsWith(text, prefix)
  foreign static caselessContains(text, needle)
  foreign static compare(left, right, caseSensitive)
}

// Concrete Wren-side base for script-defined error types.  Use this
// when a script wants to surface its own typed error without writing
// C bindings:
//
//   class MyConfigError is ScriptError {}
//   ...
//   return MyConfigError.new(42, "missing required field 'host'")
//
// Provides the same `code` / `message` / `toString` getters every
// foreign Error subclass exposes, so callers can demux uniformly
// with `is Error` and read either the foreign or script-built
// instance the same way.  Subclasses may override `toString` for a
// nicer prefix; the default uses the runtime class name.
class ScriptError is Error {
  construct new(code, message) {
    _code    = code
    _message = message
  }
  code     { _code }
  message  { _message }
  toString { "%(this.type.name)(%(_code)): %(_message)" }
}

class ScreenSupports {
  foreign static loadableFonts
  foreign static altBlinkFont
  foreign static altBoldFont
  foreign static brightBackground
  foreign static paletteChange
  foreign static pixels
  foreign static customCursor
  foreign static fontSelection
  foreign static windowTitle
  foreign static clipboard
  foreign static openUrl
  foreign static windowName
  foreign static windowIcon
  foreign static extendedPalette
  foreign static blockyScaling
  foreign static externalScaling
  foreign static closeLock
}
class ScreenFonts {
  foreign static [i]
  foreign static [i]=(n)
}
class ScreenWindow {
  foreign static bounds
  foreign static setBounds(box)
  foreign static position
  foreign static setPosition(coord)
  foreign static putChar(c)
  foreign static print(s)
  foreign static clear()
  foreign static clearToLineEnd()
  foreign static deleteLine()
  foreign static insertLine()
  foreign static scroll()
}
class Palette {
  foreign static [i]
  foreign static set(i, c)
}
// Opaque, host-created owner of a saved console state. The native snapshot
// is released when this value is collected; Screen remains a namespace.
foreign class ScreenSnapshot {}

foreign class ScreenError is Error {
  foreign code
  foreign message
  foreign toString
}

class Screen {
  // Internal bundled-caller boundary; the public native operations return values.
  static checked_(value) {
    if (value is Error) Fiber.abort(value)
    return value
  }
  static optional_(value) {
    if (value is ScreenError && value.code == IOErr.notSupported) return null
    return checked_(value)
  }
  // Retain the first cleanup error while attempting independent restoration.
  static cleanup_(steps) {
    var first = null
    for (step in steps) {
      var worker = Fiber.new { step.call() }
      var result = worker.try()
      var error = worker.error == null ? (result is Error ? result : null) : worker.error
      if (first == null) first = error
    }
    if (first != null) Fiber.abort(first)
  }
  static scope_(fn, cleanup) { Cleanup_.runResult_(fn, cleanup) }

  // Synchronous composition: cell, pixel and cursor changes become eligible
  // for presentation together. The guard also closes on VM teardown.
  static update(fn) {
    if (!(fn is Fn)) Fiber.abort("Screen.update expects a function")
    var guard = ScreenUpdate_.new()
    if (guard is Error) return guard
    var body = Fiber.new { fn.call() }
    var result = body.try()
    var finish = guard.finish_()
    if (body.error != null) Fiber.abort(body.error)
    if (!body.isDone) {
      Cleanup_.abort_(body, "Screen.update callback must not yield")
      Fiber.abort("Screen.update callback must not yield")
    }
    if (result is Error) return result
    return finish is Error ? finish : result
  }

	foreign static size
	foreign static textWidth(text, font)
  foreign static save()
  foreign static restore(handle)
  foreign static readRect(sx, sy, ex, ey)         // returns a Surface
  // Blit a Surface to the screen.  The 4-arg form takes a sub-rect of
  // the source; it unpacks the Rect into the 7-arg foreign primitive
  // `putRect_`.
  foreign static putRect(src, dstX, dstY)
  foreign static putRect_(src, srcX, srcY, srcW, srcH, dstX, dstY)
  static putRect(src, srcRect, dstX, dstY) {
    return putRect_(src, srcRect.x, srcRect.y, srcRect.w, srcRect.h, dstX, dstY)
  }

  // Run `fn` with the screen and the wire-byte pump under our own
  // control: snapshot the screen + suspend CTerm before the call,
  // unsuspend + restore the screen after.  The previous
  // `CTerm.suspended` value is saved and restored, so nested
  // modalRun calls compose correctly.  Returns whatever `fn`
  // returned.
  //
  //   Screen.modalRun(Fn.new {
  //     var app = App.new()
  //     // ... configure ...
  //     app.runSync()
  //   })
  //
  // Cleanup survives callback errors, cooperative yields, and VM shutdown.
  static modalRun(fn) {
    if (!(fn is Fn)) Fiber.abort("Screen.modalRun expects a function")
    var saved = Screen.save()
    if (saved is Error) return saved
    var wasSuspended = CTerm.suspended
    return scope_(Fn.new {
      CTerm.suspended = true
      for (i in 0..4) Screen.font[i] = 0
      return fn.call()
    }, Fn.new {
      cleanup_([
        Fn.new { CTerm.suspended = wasSuspended },
        Fn.new { checked_(Screen.restore(saved)) }
      ])
    })
  }
  foreign static pixelSize
  foreign static cellPixelSize
  foreign static readPixels(x, y, width, height)
  foreign static readPixels(x, y, width, height, force)
  foreign static setPixel(x, y, color)
  foreign static blitPixels(buffer, descriptor)
  foreign static blitPixels(buffer, mask, descriptor)
  foreign static attr
  foreign static setAttr(a)
  foreign static hyperlinkId
  foreign static setHyperlinkId(id)
  static window     { ScreenWindow   }
  static supports   { ScreenSupports }
  static font       { ScreenFonts    }
  static palette    { Palette        }
  static cursor     { CustomCursor   }
  static videoFlags { VideoFlags     }
  static color      { Color          }
}

// Internal lifetime guard shared by UI state scopes. Native registration owns
// cleanup while a callback is parked, and runs it without resuming the body
// during VM shutdown. Ordinary exits retain the original error object.
foreign class Cleanup_ {
  construct new(fn, body) {}
  foreign take_()
  foreign static isCurrentScope_(fiber)
  foreign static takeChildren_(fiber)
  // Each resume uses ordinary Wren try. The host routes completions to this
  // driver only while it owns a registered callback; Fiber itself is unchanged.
  static drive_(body) {
    var result = body.try()
    while (!body.isDone) {
      var value = Fiber.yield(result)
      if (!body.isDone) result = body.try(value)
    }
    return result
  }
  static try_(body) {
    var guard = new(Fn.new {}, body)
    var result = drive_(body)
    guard.take_()
    return result
  }
  static abort_(body, error) {
    var cleanups = takeChildren_(body)
    for (cleanup in cleanups) {
      var closer = Fiber.new { cleanup.call() }
      closer.try()
    }
    return error
  }
  static run(fn, cleanup) { run_(fn, cleanup, false, false) }
  // Remote resource cleanup awaits native close during normal execution.
  // Shutdown still runs cleanup synchronously after native cancellation.
  static runAsync_(fn, cleanup) { run_(fn, cleanup, true, false) }
  static runResult_(fn, cleanup) { run_(fn, cleanup, false, true) }
  static run_(fn, cleanup, asynchronous, returnError) {
    if (!(fn is Fn) || !(cleanup is Fn)) Fiber.abort("cleanup scope expects functions")
    var body = Fiber.new { fn.call() }
    var guard = new(cleanup, body)
    var result = drive_(body)
    var after = guard.take_()
    var cleanupError = null
    // Shutdown may have already taken this function before cancelling an
    // enclosing child. The parked scope must not restore twice.
    if (after != null) {
      var closer = Fiber.new { after.call() }
      if (asynchronous) try_(closer) else closer.try()
      cleanupError = closer.error
      if (!closer.isDone && cleanupError == null) cleanupError = "cleanup must not yield"
    }
    if (body.error != null) Fiber.abort(body.error)
    if (returnError && result is Error) return result
    if (returnError && cleanupError is ScreenError) return cleanupError
    if (cleanupError != null) Fiber.abort(cleanupError)
    return result
  }
  static runCleanup_(fn) {
    var closer = Fiber.new { fn.call() }
    closer.try()
    if (closer.error != null) Fiber.abort(closer.error)
    if (!closer.isDone) Fiber.abort("shutdown cleanup must not yield")
  }
}

// Internal lifetime guard for Screen.update; do not retain across callbacks.
foreign class ScreenUpdate_ {
  foreign static new()
  foreign finish_()
}

// Native pixel colours retain ciolib's RGB/palette discriminator so a
// captured buffer can be changed and written back without losing palette
// references.  `toRgb` resolves either form through the active palette.
class PixelColor {
  foreign static rgb(rgb)
  foreign static palette(index)
  foreign static isRgb(value)
  foreign static rgbValue(value)
  foreign static paletteIndex(value)
  foreign static toRgb(value)
}

foreign class PixelError is Error {
  foreign code
  foreign message
  foreign toString
}

foreign class PixelBuffer is Sequence {
  construct new(width, height) {}
  construct new(width, height, fill) {}
  foreign static fromRgb24(width, height, bytes)
  foreign static fromRgb24(width, height, bytes, alternateBytes)
  foreign static fromPpm(source)
  foreign static fromJxl(source)
  foreign static jxlSupported
  foreign count
  foreign [i]
  foreign [i]=(color)
  foreign iterate(iterator)
  foreign iteratorValue(iterator)
  foreign width
  foreign height
  foreign pixelAt(x, y)
  foreign setPixel(x, y, color)
  foreign fill(color)
  foreign clone()
  foreign blit(source, descriptor)
  foreign blit(source, mask, descriptor)
  foreign rgb24
  foreign hasAlternate
  foreign alternateAt(index)
  foreign setAlternate(x, y, color)
  foreign fillAlternate(color)
  foreign alternateRgb24
  foreign setAlternateRgb24(bytes)
  foreign clearAlternate()
}

foreign class PixelMask is Sequence {
  construct new(width, height) {}
  construct new(width, height, fill) {}
  foreign static fromPbm(source)
  foreign static fromBits(width, height, bits)
  foreign bits
  foreign count
  foreign [i]
  foreign [i]=(value)
  foreign iterate(iterator)
  foreign iteratorValue(iterator)
  foreign width
  foreign height
  foreign bitAt(x, y)
  foreign setBit(x, y, value)
  foreign fill(value)
}

foreign class PixelBlit {
  construct new() {}
  foreign sourceX
  foreign sourceX=(value)
  foreign sourceY
  foreign sourceY=(value)
  foreign sourceWidth
  foreign sourceWidth=(value)
  foreign sourceHeight
  foreign sourceHeight=(value)
  foreign destinationX
  foreign destinationX=(value)
  foreign destinationY
  foreign destinationY=(value)
  foreign scaleX
  foreign scaleX=(value)
  foreign scaleY
  foreign scaleY=(value)
  foreign maskX
  foreign maskX=(value)
  foreign maskY
  foreign maskY=(value)
  foreign flipX
  foreign flipX=(value)
  foreign flipY
  foreign flipY=(value)
}

// Cursor shape (start/end scanlines, range, blink, visibility).  Two
// access modes:
//
//   * Static (`CustomCursor.X` / `CustomCursor.setX(v)`): each read
//     fetches the live state, each write does a full read-modify-
//     write back to the renderer.  Cheap and ergonomic for ONE
//     field at a time, but chained writes are NOT atomic - the
//     renderer can paint between them.  For multi-field changes,
//     use the instance path for one locked state change:
//
//       var c = CustomCursor.current
//       c.startLine = 0
//       c.endLine = 7
//       c.blink = false
//       c.apply()       // one locked renderer-state update
//
//   * Instance (via `.current`, `.normal`, `.solid`, `.none`):
//     in-memory shadow.  Reads/writes touch local fields only;
//     `apply()` commits.  Required for atomic multi-field changes,
//     for snapshot/restore, and as the carrier for preset values.
foreign class CustomCursor {
  foreign static new()
  static current { new() }
  static normal  { presetLegacy_(2) }
  static solid   { presetLegacy_(1) }
  static none    { presetLegacy_(0) }
  // Restore on return, abort, or shutdown; cooperative yields are allowed.
  static preserve(fn) {
    var saved = current
    if (saved is Error) return saved
    return Screen.scope_(fn, Fn.new { Screen.checked_(saved.apply()) })
  }
  foreign static presetLegacy_(legacyType)
  foreign static startLine
  foreign static setStartLine(n)
  foreign static endLine
  foreign static setEndLine(n)
  foreign static range
  foreign static setRange(n)
  foreign static blink
  foreign static setBlink(b)
  foreign static visible
  foreign static setVisible(b)
  foreign startLine
  foreign startLine=(n)
  foreign endLine
  foreign endLine=(n)
  foreign range
  foreign range=(n)
  foreign blink
  foreign blink=(b)
  foreign visible
  foreign visible=(b)
  foreign apply()
}
// Video-mode boolean flags (alt chars, no-bright, bg-bright, etc.).
// Same two-mode shape as CustomCursor (static = live, instance =
// shadow + apply()) - see CustomCursor's docstring for the full
// rules, including the non-atomicity of chained static writes.
foreign class VideoFlags {
  foreign static new()
  static current { new() }
  // Snapshot, run `fn`, re-apply - same semantics as
  // CustomCursor.preserve.
  static preserve(fn) {
    var saved = current
    if (saved is Error) return saved
    return Screen.scope_(fn, Fn.new { Screen.checked_(saved.apply()) })
  }
  foreign static altChars
  foreign static setAltChars(b)
  foreign static noBright
  foreign static setNoBright(b)
  foreign static bgBright
  foreign static setBgBright(b)
  foreign static blinkAltChars
  foreign static setBlinkAltChars(b)
  foreign static noBlink
  foreign static setNoBlink(b)
  foreign static expand
  foreign static lineGraphicsExpand
  foreign static setLineGraphicsExpand(b)
  foreign altChars
  foreign altChars=(b)
  foreign noBright
  foreign noBright=(b)
  foreign bgBright
  foreign bgBright=(b)
  foreign blinkAltChars
  foreign blinkAltChars=(b)
  foreign noBlink
  foreign noBlink=(b)
  foreign expand
  foreign lineGraphicsExpand
  foreign lineGraphicsExpand=(b)
  foreign apply()
}
class Color {
  foreign static fromRgb(r, g, b)
  foreign static fromAttr(attr)
  foreign static toLegacyAttr(fg, bg)
}
class Input {
  // Synchronous reads - block the entire VM via getch.  Useful when
  // a script has fully claimed the screen (e.g. the Wren console)
  // and wants the main loop completely idle.
  foreign static next()
  foreign static next(ms)
  foreign static poll()
  // UI implementation detail. App passes the active Widget's atExit
  // property so a save-on-exit widget can remain interactive while the
  // process quit request stays latched.
  foreign static nextForWidget_(atExit)
  foreign static nextForWidget_(atExit, ms)
  foreign static mousedrag()
  foreign static mousedrag(forceRect)
  foreign static setMouseVisible(b)

  // Bitmask of `Mouse.*` events that get delivered.  The terminal
  // mode sets a default (typically scroll-up only); a UI App should
  // save the current mask, add what it needs, and restore on exit so
  // the surrounding terminal's mouse-mode survives.  Use the
  // {enable,disable}MouseEvent helpers for single events.
  foreign static mouseEvents
  foreign static mouseEvents=(mask)
  foreign static enableMouseEvent(ev)
  foreign static disableMouseEvent(ev)

  // Async event delivery - push a claim onto the input claim stack.
  // Each claim is a `Fn` taking a single event arg (KeyEvent,
  // PhysicalKeyEvent, or MouseEvent foreign) and returning a Bool (consumed).  The C
  // dispatcher walks claims top-down (newest fiber first); the
  // first claim to return `true` consumes the event.  Below all
  // claims, registered `Hook.onKey` / `Hook.onPhysicalKey` /
  // `Hook.onMouse` handlers fire.
  //
  // Per-fiber slot: each fiber has at most one claim on the stack.
  // A same-fiber re-push replaces the existing entry in place; a
  // different-fiber push adds a new entry on top.  Auto-prune at
  // dispatch time drops claims whose owning fiber is `isDone`.
  //
  // Returns a `ClaimHandle`.  Call `.pop()` on it to remove the
  // claim explicitly; the handle's foreign finalizer also pops on
  // GC if the script forgets.  pop() is idempotent.
  //
  // Common pattern (App.run):
  //   var claim = Input.pushClaim {|ev|
  //     // dispatch ev into widget tree, return consumed bool
  //   }
  //   ...                              // run loop, parks on Fiber.yield
  //   claim.pop()
  //
  // The two-arg `pushClaim_` is the foreign; the public one-arg
  // `pushClaim` captures `Fiber.current` automatically.
  foreign static pushClaim_(fiber, fn)
  static pushClaim(fn) {
    if (!(fn is Fn)) Fiber.abort("Input.pushClaim expects a Fn")
    return pushClaim_(Fiber.current, fn)
  }

  static unget(ev) {
    if (ev is KeyEvent) {
      Input.ungetKey_(ev)
    } else if (ev is PhysicalKeyEvent) {
      Input.ungetPhysicalKey_(ev)
    } else if (ev is MouseEvent) {
      Input.ungetMouse_(ev)
    } else {
      Fiber.abort("Input.unget expects an input event")
    }
  }
  foreign static ungetKey_(ev)
  foreign static ungetPhysicalKey_(ev)
  foreign static ungetMouse_(ev)
  foreign static synthesizePhysicalKey(evdev, pressed)
}
// Returned from Input.pushClaim.  Drop the claim by calling `.pop()`;
// idempotent (stale handles after a same-fiber re-push are no-ops).
// The C-side finalizer also pops on GC, so a forgotten handle still
// cleans up - but explicit pop is cheaper and more predictable.
foreign class ClaimHandle {
  foreign pop()
}

// Fiber-resume primitive.  `Wake.post(fiber, value)` queues a
// synthetic resume of `fiber`, delivering `value` as the return of
// its next `Fiber.yield()`.  The wake is enqueued in this VM's FIFO
// and delivered when its owner loop next drains that FIFO - never in
// the middle of the current foreign call.  Safe to call on the VM's
// owner thread from hooks
// (Hook.onInput / Hook.onMouse / Hook.onStatus / claim handlers /
// worker fibers): remote bytes or any other state change a hook
// observes can wake a UI fiber that's parked on `Fiber.yield()`.
//
// `value` may be any Wren object - the deliverer pins it as a
// WrenHandle until delivery, then loads it into the fiber's
// resumed value slot.  Convention: pass a discriminated value
// your fiber's main-loop demuxer can recognise (a foreign
// KeyEvent / MouseEvent class, or your own sentinel).
//
// Multiple posts can queue for the same fiber; each becomes one
// resumption in arrival order.  `post` does NOT wait for delivery
// - the caller's foreign returns immediately and the surrounding
// hook chain completes synchronously, satisfying the hook contract.
//
// Do not call from inside another foreign method that itself
// re-enters wrenCall (same rule as wrenCall in §7 of wren.md);
// hooks are the canonical safe site.
class Wake {
  foreign static post_(fiber, value)
  static post(fiber, value) {
    if (!(fiber is Fiber)) Fiber.abort("Wake.post expects a Fiber")
    return post_(fiber, value)
  }
}

foreign class KeyEvent {
  construct new(key) {}
  construct new(key, modifiers) {}
  foreign static text(text, modifiers)
  static text(text) { KeyEvent.text(text, 0) }
  foreign static byte(value)
  foreign static doorway(scanCode, enhanced)
  foreign key
  foreign modifiers
  foreign codepoint
  foreign text
  foreign byte
  foreign scanCode
  foreign enhanced
  foreign toString

  foreign matches(key, modifiers)

  // UI lookup token, private to the bundled dispatcher. Committed Shift
  // text already carries its case; all other modifiers remain significant.
  shortcut_ {
    var mods = modifiers & ~48
    if (codepoint != null && mods == Mod.shift) mods = 0
    return key == null ? -1 : Key.chord_(key, mods)
  }
}
foreign class PhysicalKeyEvent {
  construct new(evdev, pressed) {}
  construct new(evdev, pressed, modifiers, repeat) {}
  foreign evdev
  foreign pressed
  foreign modifiers
  foreign repeat
  foreign toString
}
// Five constructor overloads for synthesising mouse events.  All
// funnel into the same C allocator, which dispatches on
// wrenGetSlotCount.  Defaults when an arg is omitted:
//   endX, endY → copies of startX, startY (zero-extent point event)
//   modifiers  → 0
//   bstate     → derived from event (bit for the button it's about,
//                or 0 for MOUSE_MOVE)
foreign class MouseEvent {
  construct new(event, startX, startY) {}
  construct new(event, startX, startY, modifiers) {}
  construct new(event, startX, startY, endX, endY) {}
  construct new(event, startX, startY, endX, endY, modifiers) {}
  construct new(event, startX, startY, endX, endY, modifiers, bstate) {}
  foreign static withPixels(event, startX, startY, endX, endY, modifiers,
      bstate, startPixelX, startPixelY, endPixelX, endPixelY)
  foreign static withPixelsAndWheel(event, startX, startY, endX, endY,
      modifiers, bstate, startPixelX, startPixelY, endPixelX, endPixelY,
      wheelX, wheelY)
  foreign event
  foreign bstate
  foreign modifiers
  foreign startX
  foreign startY
  foreign endX
  foreign endY
  foreign startPixelX
  foreign startPixelY
  foreign endPixelX
  foreign endPixelY
  foreign wheelX
  foreign wheelY
  foreign toString
}
// MouseEvent.event matches one of these stable values.  Each button's slot is
// press/release/click/dbl/trpl/quad/dragStart/dragMove/dragEnd in
// that order, offset by 9 per button.  Buttons 1/2/3 are left,
// middle, and right; buttons 4/5 are X1 and X2.
class Mouse {
  static move           { 0  }
  static button1Press   { 1  }
  static button1Release { 2  }
  static button1Click   { 3  }
  static button1DblClick{ 4  }
  static button1TrplClick{ 5 }
  static button1QuadClick{ 6 }
  static button1DragStart{ 7 }
  static button1DragMove { 8 }
  static button1DragEnd  { 9 }
  static button2Press    { 10 }
  static button2Release  { 11 }
  static button2Click    { 12 }
  static button2DblClick { 13 }
  static button2TrplClick{ 14 }
  static button2QuadClick{ 15 }
  static button2DragStart{ 16 }
  static button2DragMove { 17 }
  static button2DragEnd  { 18 }
  static button3Press    { 19 }
  static button3Release  { 20 }
  static button3Click    { 21 }
  static button3DblClick { 22 }
  static button3TrplClick{ 23 }
  static button3QuadClick{ 24 }
  static button3DragStart{ 25 }
  static button3DragMove { 26 }
  static button3DragEnd  { 27 }
  static button4Press    { 28 }
  static button4Release  { 29 }
  static button4Click    { 30 }
  static button4DblClick { 31 }
  static button4TrplClick{ 32 }
  static button4QuadClick{ 33 }
  static button4DragStart{ 34 }
  static button4DragMove { 35 }
  static button4DragEnd  { 36 }
  static button5Press    { 37 }
  static button5Release  { 38 }
  static button5Click    { 39 }
  static button5DblClick { 40 }
  static button5TrplClick{ 41 }
  static button5QuadClick{ 42 }
  static button5DragStart{ 43 }
  static button5DragMove { 44 }
  static button5DragEnd  { 45 }
  static wheelUp         { 46 }
  static wheelDown       { 47 }
  static wheelLeft       { 48 }
  static wheelRight      { 49 }
  static wheelUpPress    { wheelUp }
  static wheelUpClick    { wheelUp }
  static wheelDownPress  { wheelDown }
  static wheelDownClick  { wheelDown }
}
class Mod {
  static shift { 1 }
  static control { 2 }
  static alt { 4 }
  static gui { 8 }
  static capsLock { 16 }
  static numLock { 32 }
  static command { Host.altKeyShort == "Cmd" ? gui : alt }
}
class Key {
  static escape { 0x11001B }
  static enter { 0x11000D }
  static backspace { 0x110008 }
  static tab { 0x110009 }
  static delChar { 0x11007F }
  static up { 0x110100 }
  static down { 0x110101 }
  static left { 0x110102 }
  static right { 0x110103 }
  static insert { 0x110104 }
  static delete { 0x110105 }
  static home { 0x110106 }
  static end { 0x110107 }
  static pageUp { 0x110108 }
  static pageDown { 0x110109 }
  static f1 { 0x11010A }
  static f2 { 0x11010B }
  static f3 { 0x11010C }
  static f4 { 0x11010D }
  static f5 { 0x11010E }
  static f6 { 0x11010F }
  static f7 { 0x110110 }
  static f8 { 0x110111 }
  static f9 { 0x110112 }
  static f10 { 0x110113 }
  static f11 { 0x110114 }
  static f12 { 0x110115 }
  static quit { 0x120000 }

  // Exact integer token for the UI's private maps. 21 key bits plus the
  // complete 32-bit modifier mask fit Wren's 53-bit integer precision.
  static chord_(key, modifiers) { key + (modifiers & ~48) * 0x200000 }

  // Desktop command shortcuts refer to physical letter positions. ANSI
  // terminals instead supply logical modified text. Never synthesize a
  // second KeyEvent from a physical edge.
  static physicalShortcut_(event) {
    if (!event.pressed) return -1
    var mods = event.modifiers & ~48
    if (event.evdev == 41 && (mods & Mod.control) != 0) return chord_(96, mods)
    if ((mods & (Mod.alt | Mod.gui)) == 0) return -1
    var letters = [30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                   49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44]
    var letter = letters.indexOf(event.evdev)
    if (letter >= 0) return chord_(97 + letter, mods)
    if (event.evdev >= 2 && event.evdev <= 11) {
      return chord_(event.evdev == 11 ? 48 : 47 + event.evdev, mods)
    }
    return -1
  }
}
class Clipboard {
  foreign static text
  foreign static setText(s)
}
class Codepage {
  static cp437        { 0 }
  static cp1251       { 1 }
  static cp1251_b     { 2 }
  static koi8r        { 3 }
  static iso8859_2    { 4 }
  static iso8859_4    { 5 }
  static cp866m       { 6 }
  static iso8859_9    { 7 }
  static iso8859_8    { 8 }
  static koi8u        { 9 }
  static iso8859_15   { 10 }
  static iso8859_5    { 11 }
  static cp850        { 12 }
  static cp850_b      { 13 }
  static cp865        { 14 }
  static cp865_b      { 15 }
  static iso8859_7    { 16 }
  static iso8859_1    { 17 }
  static cp866m2      { 18 }
  static cp866u       { 19 }
  static cp1131       { 20 }
  static armscii8     { 21 }
  static haik8        { 22 }
  static atascii      { 23 }
  static petsciiUpper { 24 }
  static petsciiLower { 25 }
  static prestel      { 26 }
  static prestelSep   { 27 }
  static atariSt      { 28 }

  foreign static encodes_(text)
  foreign static decodeByte(codepage, byte)
}
class REPL {
  static eval(src) { eval("syncterm", src) }
  static eval(module, src) {
    // Pre-classify by the leading non-whitespace token so we don't
    // fight with expression-mode wrapping when the source is clearly
    // a statement (var/class/import/etc.).  An expression-form
    // compile failure on something that LOOKS like an expression
    // therefore preserves the original compile error in the log
    // instead of being shadowed by a statement-form retry.
    captureStart_()
    var asStatement = isStatementForm_(src)
    var fn          = compile_(module, src, !asStatement, true)
    var wasExpr     = !asStatement && fn != null
    captureCommit_()
    if (fn == null) return null
    var v = fn.call()
    return wasExpr ? [v] : null
  }

  // True if the first non-whitespace token in `src` is a Wren
  // statement keyword (one that can't begin an expression).  Used by
  // eval to route the source to compile_ in the right mode without
  // a fallback dance - preserving expression-mode compile errors
  // for actual expression-form sources.
  static isStatementForm_(src) {
    var i = 0
    while (i < src.count) {
      var c = src[i]
      if (c != " " && c != "\t" && c != "\n" && c != "\r") break
      i = i + 1
    }
    var start = i
    while (i < src.count) {
      var c = src[i]
      var ok = c == "_" ||
               (c.bytes[0] >= 0x41 && c.bytes[0] <= 0x5A) ||
               (c.bytes[0] >= 0x61 && c.bytes[0] <= 0x7A) ||
               (c.bytes[0] >= 0x30 && c.bytes[0] <= 0x39)
      if (!ok) break
      i = i + 1
    }
    if (i == start) return false
    var first = src[start...i]
    return [
      "var", "class", "foreign", "import",
      "return", "break", "continue",
      "if", "while", "for"
    ].contains(first)
  }

  foreign static compile_(module, src, isExpression, printErrors)
  foreign static printTrace_(fiber)
  foreign static hasModule(name)
  foreign static modules
  foreign static captureStart_()
  foreign static captureContains_(needle)
  foreign static captureClear_()
  foreign static captureCommit_()
}
foreign class Cell {
  construct new() {}
  foreign ch
  foreign ch=(s)
  foreign font
  foreign font=(n)
  foreign columns
  foreign legacyAttr
  foreign legacyAttr=(n)
  foreign bright
  foreign bright=(b)
  foreign blink
  foreign blink=(b)
  foreign pixelGraphics
  foreign fgPalette
  foreign fgPalette=(n)
  foreign fgRgb
  foreign fgRgb=(n)
  foreign bgPalette
  foreign bgPalette=(n)
  foreign bgRgb
  foreign bgRgb=(n)
  foreign hyperlinkId
  foreign hyperlinkId=(n)
  // Structural equality of every content field - explicitly NOT a
  // `==` override (Cell stays foreign-identity-equal like every
  // other foreign).  Returns false for non-Cell `other`, false if
  // either cell flies the pixel-graphics flag (the actual pixel
  // data lives outside the vmem_cell), and ignores the BG dirty bit
  // (render bookkeeping, not content).
  foreign eqContent(other)
  foreign toString
}
// Surface owns a w×h grid of vmem_cells.  Inherits Sequence so the
// linear `[i]` subscript, `count`, and the standard iteration helpers
// (each, where, map, all, any, reduce, join, take, skip, toList, …)
// all work off the iterate/iteratorValue protocol below - handy for
// "tell me where any 'X' lives" without having to switch to 2D
// indexing.
//
// Linear iteration order is ROW-MAJOR: cell at (x, y) sits at linear
// index `y * width + x`.  Equivalent to `for (y in 0...h) for (x in
// 0...w) yield cellAt(x, y)`.  This matches the underlying C vmem
// layout (which is also row-major) and matches the order that
// Screen.putRect blits cells to the display.
//
// Surface adds rectangular operations:
//   surface.cellAt(x, y)              - new 2D-indexed Cell view (null if oob).
//   surface.cellAt(x, y, cell)        - retarget cell; false if oob.
//   surface.rows / surface.cols       - Sequence-of-Sequence views.
//   surface.putRect(src, dstX, dstY)  - paste full src.
//   surface.putRect(src, srcRect, dstX, dstY) - paste sub-rect of src.
//   surface.fill(rect, cell)          - bulk fill with a Cell template.
// Overlapping self-copies preserve the original clipped source.  A clipped
// half of a two-cell glyph becomes a styled blank.  Fill intersects `rect`
// with the Surface; empty and disjoint rectangles do nothing.
//
// `Screen.readRect` returns a Surface (sized to the requested rect);
// `Screen.putRect` blits a Surface to the screen as a single atomic
// puttext.
foreign class Surface is Sequence {
  construct new(width, height) {}
  foreign count
  foreign [i]
  foreign iterate(it)
  foreign iteratorValue(it)
  foreign width
  foreign height
  foreign cellAt(x, y)
  foreign cellAt(x, y, cell)
  foreign urlAt(x, y)
  foreign putRect(src, dstX, dstY)
  foreign putRect_(src, srcX, srcY, srcW, srcH, dstX, dstY)
  foreign fill_(x, y, w, h, cell)
  foreign toString
  // Wren-side wrappers that unpack a Rect into the primitive form.
  putRect(src, srcRect, dstX, dstY) {
    return putRect_(src, srcRect.x, srcRect.y, srcRect.w, srcRect.h, dstX, dstY)
  }
  fill(rect, cell) {
    fill_(rect.x, rect.y, rect.w, rect.h, cell)
  }
  // Sequence-of-Sequence views.  `rows` is outer-by-y, `cols` is
  // outer-by-x; both delegate cell access to cellAt(x, y), so they
  // stay live against the underlying Surface (no copy).  Use these
  // when 2D iteration reads cleaner than indexing into the linear
  // Sequence:
  //   for (row in surf.rows) for (cell in row) ...   // row-major
  //   for (col in surf.cols) for (cell in col) ...   // col-major
  //   surf.rows[y][x]                                // single cell
  rows { SurfaceRows.new(this) }
  cols { SurfaceCols.new(this) }
}

// `surf.rows[y]` - one horizontal slice as a Sequence<Cell> of length
// `surf.width`.  Lazy view: holds a reference to the Surface and the
// row index, fetches cells via cellAt on demand.
class SurfaceRow is Sequence {
  construct new(surface, y) {
    _surface = surface
    _y       = y
  }
  count             { _surface.width }
  [x]               { _surface.cellAt(x, _y) }
  iterate(it) {
    var nx = (it == null) ? 0 : it + 1
    return nx < _surface.width ? nx : false
  }
  iteratorValue(it) { _surface.cellAt(it, _y) }
}

// `surf.cols[x]` - one vertical slice as a Sequence<Cell> of length
// `surf.height`.  Same lazy-view shape as SurfaceRow.
class SurfaceCol is Sequence {
  construct new(surface, x) {
    _surface = surface
    _x       = x
  }
  count             { _surface.height }
  [y]               { _surface.cellAt(_x, y) }
  iterate(it) {
    var ny = (it == null) ? 0 : it + 1
    return ny < _surface.height ? ny : false
  }
  iteratorValue(it) { _surface.cellAt(_x, it) }
}

// `surf.rows` - Sequence of SurfaceRow, length = surf.height.
class SurfaceRows is Sequence {
  construct new(surface) { _surface = surface }
  count             { _surface.height }
  [y]               { SurfaceRow.new(_surface, y) }
  iterate(it) {
    var ny = (it == null) ? 0 : it + 1
    return ny < _surface.height ? ny : false
  }
  iteratorValue(it) { SurfaceRow.new(_surface, it) }
}

// `surf.cols` - Sequence of SurfaceCol, length = surf.width.
class SurfaceCols is Sequence {
  construct new(surface) { _surface = surface }
  count             { _surface.width }
  [x]               { SurfaceCol.new(_surface, x) }
  iterate(it) {
    var nx = (it == null) ? 0 : it + 1
    return nx < _surface.width ? nx : false
  }
  iteratorValue(it) { SurfaceCol.new(_surface, it) }
}

// Scrollback - read-only logical access to cterm's process-wide
// scrollback ring.  copyRowsTo copies only the requested logical rows
// into an ordinary owned Surface, translating across the physical ring
// wrap without rotating or otherwise changing the ring.
class Scrollback {
  foreign static width
  foreign static height
  foreign static copyRowsTo(destination, sourceRow, rowCount, destRow)
}

class Font {
  static cp437English      { 0 }
  static commodore64Upper  { 32 }
  static commodore64Lower  { 33 }
  static commodore128Upper { 34 }
  static commodore128Lower { 35 }
  static atari             { 36 }
  static potNoodle         { 37 }
  static mosOul            { 38 }
  static microKnightPlus   { 39 }
  static topazPlus         { 40 }
  static microKnight       { 41 }
  static topaz             { 42 }
  static prestel           { 43 }
  static atariSt           { 44 }
  static ripterm           { 45 }
  foreign static name(i)
  foreign static count
  foreign static available(i)
  foreign static codepage
  foreign static codepageOf(i)
  foreign static codepageFor(selector)
  // Registers without selecting. Returns a selector, FileError or FontError.
  // The default is the terminal's current codepage, independent of Screen.font.
  foreign static load(file)
  foreign static load(file, codepage)
}
// Typed-ID lookup table over the active hyperlink registry.  Despite
// the `[id]` / `containsKey(id)` shape, this is NOT a Map - no
// `keys` / `values` / `count` / iteration.  IDs flow IN from `add()`
// returns or `Cell.hyperlinkId`; scripts can't enumerate the ID space.
// IDs remain valid for the application's lifetime.  A non-empty idParam
// deduplicates an exact URI/parameter pair; null or empty idParam allocates a
// fresh ID.  This family is not available in the file-picker VM.
class Hyperlinks {
  foreign static [id]
  foreign static containsKey(id)
  foreign static add(uri, idParam)
  foreign static params(id)
}
// View on Console that participates in the Sequence protocol -
// supports map/where/toList/count/etc. via Wren's stdlib mixin.
// `Console` itself stays an all-static namespace (so `Console.clear()`,
// `Console.markSeen()`, `for (e in Console)` etc. read naturally);
// the iteration view lives behind `Console.entries` because Sequence's
// methods are defined as INSTANCE methods on `this`, which a static-
// only class can't satisfy.
class ConsoleEntries is Sequence {
  construct new() {}
  count             { Console.count }
  [seq]             { Console[seq] }
  iterate(it)       { Console.iterate(it) }
  iteratorValue(it) { Console.iteratorValue(it) }
}
class Console {
  foreign static count
  foreign static total
  foreign static [seq]
  foreign static clear()
  foreign static markSeen()
  foreign static iterate(it)
  foreign static iteratorValue(it)
  // Sequence-protocol view - use for `.map`, `.where`, `.toList`, etc.
  // `for (e in Console)` works on the class directly via the static
  // iterate above; `entries` is only needed for the Sequence helpers.
  static entries { ConsoleEntries.new() }
}
class LogSource {
  static print { 0 }
  static compileError { 1 }
  static runtimeError { 2 }
  static stackFrame { 3 }
}
// VM-scoped view of the connected session. Menu and picker VMs have no
// current session. Leases keep shell EOF from ending active auxiliary work.
foreign class Session {
  foreign static current
  foreign hold()
  foreign holdCount
  foreign profile
  foreign name
  foreign entryId
  foreign entrySource
  foreign added
  foreign lastConnected
  foreign calls
  foreign claimWire()
  hold(fn) {
    if (!(fn is Fn)) Fiber.abort("Session.hold expects a Fn")
    var lease = hold()
    return Cleanup_.run(fn, Fn.new { lease.release() })
  }
}
foreign class SessionLease {
  foreign released
  foreign release()
}

// Exclusive terminal-channel input and protocol output ownership.
foreign class WireLease {
  foreign released
  foreign release()
  foreign available
  foreign readBytes(count)
  foreign peekBytes(count)
  foreign sendBytes(bytes)
  foreign sendWire(bytes)
  use(fn) {
    if (!(fn is Fn)) Fiber.abort("WireLease.use expects a Fn")
    return Cleanup_.run(Fn.new { fn.call(this) }, Fn.new { release() })
  }
}

// sendText / sendBytes own queued output and return Operation. Await for null
// success or ConnError. Queue pressure is handled by the host; callers
// never retry a UTF-8 slice using a transformed wire-byte offset.
class Conn {
  foreign static sendText(s)
  foreign static sendBytes(s)
  foreign static close()
  // End the BBS session.  Shows the "Disconnect... Are you sure?"
  // confirm; on yes, hangs up the connection.  When `exitApp` is
  // true (Alt-X semantics), syncterm exits after
  // hangup; when false (Alt-H / Ctrl-Q semantics), control returns
  // to the main menu.  The confirm runs on doterm()'s next
  // iteration, so this returns immediately - handlers don't block.
  foreign static endSession(exitApp)
  // Paste from system clipboard onto the wire - codepage-aware,
  // bracketed-paste-aware.  No-op when clipboard is empty.
  foreign static paste()
  // Open the Wren scrollback browser for the active terminal.
  foreign static scrollback()
  foreign static connected
  foreign static elapsedSeconds
  foreign static type
  foreign static sessionLog
  foreign static queued
}

// Conn.sendText / sendBytes Operation result: null on success, ConnError on failure.
foreign class ConnError is Error {
  foreign code        // ConnErr.* enum
  foreign partial     // some input was accepted before failure; no retry offset
  foreign message
  foreign toString
}

class ConnErr {
  static notConnected { 1 }
  static io           { 2 }
  static queueFull    { 3 }
  static invalidText  { 4 }
  static busy         { 5 }
}

// Streaming-log control for the active session. CaptureMenu drives the
// user-facing flow on top of these primitives. All methods are no-ops
// when no terminal is active.
//
// `start(file, raw)` consumes the write consent on `file` (must be
// from Host.pickSavePath).  Returns null on success or a FileError
// on open failure.  raw=true preserves ANSI escapes; false strips
// to plain text.  Pause / resume toggle the PAUSED bit; data
// continues to flow through the terminal but isn't written while
// paused.
class Capture {
  foreign static active
  foreign static paused
  foreign static start(file, raw)
  foreign static stop()
  foreign static pause()
  foreign static resume()
}

class TerminalEncoding {
  static utf8 { 0 }
  static codepage { 1 }
}

foreign class FontError is Error {
  foreign code
  foreign nativeCode
  foreign message
  foreign toString
}
class FontErr {
  static unavailable         { 1 }
  static invalidBitmap       { 2 }
  static unsupportedCodepage { 3 }
  static noSlots             { 4 }
  static notLoaded           { 5 }
  static nativeFailure       { 6 }
}
class CTermFontMask {
  static normal      { 1 }
  static bright      { 2 }
  static blink       { 4 }
  static brightBlink { 8 }
  static all         { 15 }
}
class CTermFonts {
  foreign static [slot]
  foreign static [slot]=(selector)
}

class CTerm {
  foreign static x
  foreign static y
  foreign static originX
  foreign static originY
  foreign static width
  foreign static height
  foreign static topMargin
  foreign static bottomMargin
  foreign static leftMargin
  foreign static rightMargin
  foreign static attr
  foreign static fgColor
  foreign static bgColor
  foreign static hasPaletteOverride
  foreign static paletteOverride
  foreign static fontSlot
  static font { CTermFonts }
  foreign static setFont(selector, mask)
  foreign static scrollbackLines
  foreign static scrollbackWidth
  foreign static scrollbackPos
  foreign static scrollbackStart
  foreign static emulation
  foreign static encoding
  foreign static codepage
  // Send a local key through cterm's active emulation mapping.  Returns
  // Bool on success (native acceptance, not delivery), CTermError on failure.
  foreign static sendKey(key)
  foreign static doorwayMode
  foreign static doorwayMode=(b)
  foreign static music
  foreign static music=(b)
  foreign static started
  foreign static graphicsMode
  foreign static skypix
  // Save the cterm area (status bar excluded) as IBM-CGA / BinaryText
  // to the given write-consent File, optionally with a SAUCE block.
  // Consumes the File's write consent.  Returns null on success or a
  // FileError on write failure.
  foreign static saveScreenshot(file, withSauce)
  foreign static atasciiInverse
  foreign static ooiiMode
  // Select 0 (off) through Host.maxOOIIMode. Integers are clamped, and
  // changing modes discards a partial command. Audio opens lazily for SFX.
  foreign static ooiiMode=(n)
  foreign static mouseMode
  foreign static mouseSgrPixels
  foreign static mouseDisabled
  foreign static mouseDisabled=(b)
  // Network character-pacing rate in BPS (Alt-Up / Alt-Down walk it
  // up and down the rates ladder).  0 = unthrottled.  Always 0 on
  // serial connections; the configured port speed lives in Session.current.profile.bpsRate.
  foreign static throttleSpeed
  // Set the throttle to a specific BPS value (0 = unthrottled).
  // No-op on serial connections.
  foreign static throttleSpeed=(bps)
  foreign static throttleSpeedUp()
  foreign static throttleSpeedDown()
  foreign static statusDisplay
  foreign static extAttr
  foreign static lastColumnFlag
  // Strict UTF-8/NFC local rendering: null or CTermError. Counts on errors
  // refer to normalized input bytes, not an original-String retry offset.
  foreign static write(s)
  // Direct native reset: null or CTermError; failures can have partial effects.
  foreign static reset()
  foreign static suspended
  foreign static suspended=(b)
}
// Synchronous terminal failures preserve native outputs without claiming
// rollback, delivery or a safe retry offset into the original String.
foreign class CTermError is Error {
  foreign code
  foreign message
  foreign consumedBytes
  foreign handled
  foreign toString
}

foreign class ExtAttr {
  foreign autoWrap
  foreign originMode
  foreign sxScroll
  foreign decLrmm
  foreign bracketPaste
  foreign decBkm
  foreign prestelMosaic
  foreign prestelDoubleHeight
  foreign prestelConceal
  foreign prestelSeparated
  foreign prestelHold
  foreign alternateKeypad
}
foreign class LastColumnFlag {
  foreign set
  foreign enabled
  foreign forced
}
class LogMode {
  static none  { 0 }
  static ascii { 1 }
  static raw   { 2 }
}
class StatusDisplay {
  static none      { 0 }
  static indicator { 1 }
  static host      { 2 }
}
// Configuration has one field contract in menu and connected VMs. Views
// retain their source entry; copy() creates an independent editable value.
foreign class Profile {
  foreign readOnly
  foreign copy()
  foreign sshFingerprint
  foreign sshFingerprint=(value)
  foreign connTypeName
  foreign addr
  foreign addr=(value)
  foreign port
  foreign port=(value)
  foreign connType
  foreign connType=(value)
  foreign user
  foreign user=(value)
  foreign password
  foreign password=(value)
  foreign syspass
  foreign syspass=(value)
  foreign music
  foreign music=(value)
  foreign graphics
  foreign graphics=(value)
  foreign comment
  foreign comment=(value)
  foreign addressFamily
  foreign addressFamily=(value)
  foreign termName
  foreign termName=(value)
  foreign screenMode
  foreign screenMode=(value)
  foreign bpsRate
  foreign bpsRate=(value)
  foreign amigaFont
  foreign amigaFont=(value)
  foreign amigaFontSize
  foreign amigaFontSize=(value)
  foreign font
  foreign encoding
  foreign encoding=(value)
  foreign font=(value)
  foreign noStatus
  foreign noStatus=(value)
  foreign hidePopups
  foreign hidePopups=(value)
  foreign yellowIsYellow
  foreign yellowIsYellow=(value)
  foreign forceLcf
  foreign forceLcf=(value)
  foreign dlDir
  foreign dlDir=(value)
  foreign ulDir
  foreign ulDir=(value)
  foreign logFile
  foreign logFile=(value)
  foreign appendLogFile
  foreign appendLogFile=(value)
  foreign xferLogLevel
  foreign xferLogLevel=(value)
  foreign protocolLogLevel
  foreign protocolLogLevel=(value)
  foreign ctermLogLevel
  foreign ctermLogLevel=(value)
  foreign stopBits
  foreign stopBits=(value)
  foreign dataBits
  foreign dataBits=(value)
  foreign parity
  foreign parity=(value)
  foreign flowControl
  foreign flowControl=(value)
  foreign telnetNoBinary
  foreign telnetNoBinary=(value)
  foreign deferTelnetNegotiation
  foreign deferTelnetNegotiation=(value)
  foreign tlsTrustWebPki
  foreign tlsTrustWebPki=(value)
  foreign tlsTrustedCert
  foreign tlsTrustedCert=(value)
  foreign tlsClientCert
  foreign tlsClientCert=(value)
  foreign tlsClientKey
  foreign tlsClientKey=(value)
  foreign tlsPskIdentity
  foreign tlsPskIdentity=(value)
  foreign tlsPsk
  foreign tlsPsk=(value)
  foreign tlsPskVersion
  foreign tlsPskVersion=(value)
  foreign tlsVersionFloor
  foreign tlsVersionFloor=(value)
  foreign ghostProgram
  foreign ghostProgram=(value)
  foreign sftpPublicKey
  foreign sftpPublicKey=(value)
  foreign sshAllowAes128Cbc
  foreign sshAllowAes128Cbc=(value)
  foreign sshAcceptEarlyData
  foreign sshAcceptEarlyData=(value)
  foreign palette
  foreign palette=(value)
  foreign wrenScripts
  foreign wrenScripts=(value)
  foreign lfExpand
  foreign lfExpand=(value)
}

class TLSVersion {
  static unknown { 0 }
  static tls12   { 1 }
  static tls13   { 2 }
}
class ConnType {
  static unknown        { 0 }
  static rlogin         { 1 }
  static rloginReversed { 2 }
  static telnet         { 3 }
  static raw            { 4 }
  static ssh            { 5 }
  static sshNoAuth      { 6 }
  static modem          { 7 }
  static serial         { 8 }
  static serialNoRts    { 9 }
  static shell          { 10 }
  static mbbsGhost      { 11 }
  static telnets        { 12 }
  static mqtt           { 13 }
}
class Emulation {
  static ansiBbs   { 0 }
  static petascii  { 1 }
  static atascii   { 2 }
  static prestel   { 3 }
  static beeb      { 4 }
  static atariVt52 { 5 }
}
class BBSListType {
  static user   { 0 }
  static system { 1 }
}
class ScreenMode {
  static current        { 0 }
  static c80x25         { 1 }
  static lcd80x25       { 2 }
  static c80x28         { 3 }
  static c80x30         { 4 }
  static c80x43         { 5 }
  static c80x50         { 6 }
  static c80x60         { 7 }
  static c132x37        { 8 }
  static c132x52        { 9 }
  static c132x25        { 10 }
  static c132x28        { 11 }
  static c132x30        { 12 }
  static c132x34        { 13 }
  static c132x43        { 14 }
  static c132x50        { 15 }
  static c132x60        { 16 }
  static c64            { 17 }
  static c128_40        { 18 }
  static c128_80        { 19 }
  static atari          { 20 }
  static atariXep80     { 21 }
  static custom         { 22 }
  static ega80x25       { 23 }
  static vga80x25       { 24 }
  static prestel        { 25 }
  static beeb           { 26 }
  static atariSt40x25   { 27 }
  static atariSt80x25   { 28 }
  static atariSt80x25Mono { 29 }
  static amiga640x200    { 30 }
}
class AddressFamily {
  static unspec { 0 }
  static inet   { 1 }
  static inet6  { 2 }
  static happyEyeballs { 3 }
}
class MusicMode {
  static syncterm { 0 }
  static bansi    { 1 }
  static enabled  { 2 }
}
class GraphicsMode {
  static none { 0 }
  static v1   { 1 }
  static v3   { 2 }
  static skypix13 { 3 }
  static skypix31 { 4 }
}
class Parity {
  static none { 0 }
  static even { 1 }
  static odd  { 2 }
}
class FlowControl {
  static rtsCts { 0 }
  static xonXoff { 1 }
  static both { 2 }
  static none { 3 }
}
class LogLevel {
  static emergency { 0 }
  static alert     { 1 }
  static critical  { 2 }
  static error     { 3 }
  static warning   { 4 }
  static notice    { 5 }
  static info      { 6 }
  static debug     { 7 }
}
foreign class Directory {
  foreign contains(name)
  foreign list
  foreign create(name)
  foreign createDir(name)
  foreign delete(name)
  // Atomically replace a file through a complete sibling temporary file.
  // The old file remains intact on write failure.
  foreign replace(name, bytes)
  foreign toString
}
// Pure byte hashing, available in every script VM. Size is independent of availability.
class Digest {
  static sha256 { 1 }
  static sha384 { 2 }
  static sha512 { 3 }
  static md5    { 4 }
  static sha1   { 5 }
  foreign static size(algorithm)
  foreign static hash(algorithm, bytes)
}
foreign class DigestError is Error {
  foreign code
  foreign message
  foreign toString
}
class DigestErr {
  static failed   { -1 }
  static disabled { -5 }
}

// File and Directory values grant operations on a stored pathname.  They do
// not identify the particular filesystem object present when the value was
// created, and deletion does not revoke the pathname grant.  Closed File
// operations and Directory operations retain their method-specific missing
// path behavior; the same value can be used again after the path is recreated.
foreign class File {
  foreign path
  foreign open()
  foreign close()
  foreign readBytes(count)
  foreign readBytes(count, offset)
  foreign read()
  foreign writeBytes(s)
  foreign writeBytes(s, offset)
  foreign write(s)
  foreign readLine()
  foreign writeLine(s)
  foreign offset
  foreign offset=(o)
  foreign size
  foreign mtime
  foreign mtime=(t)
  foreign isOpen
  // Opaque consent token for picker-sourced Files; null for Files
  // obtained via Cache / Download Directory listings.
  // Persist this in WON next to the path string and pass back to
  // Host.openLocalFile on resume to re-construct a File foreign at
  // the same absolute path.  See the picker-tokens section in
  // Wren.adoc for the threat model.
  foreign token
  // Hash the complete file through a native mapping; return raw bytes,
  // FileError for filesystem failures, or DigestError for hashing failures.
  foreign digest(algorithm)
  foreign toString
}

// Recoverable I/O failures (disk full, mmap failed, a closed File's
// pathname unavailable, etc.) are returned IN PLACE of
// the typed result foreign - mirrors SFTPError.  Compare with `is
// FileError`, or just ignore the value (most scripts will: a
// failed write returns a FileError that the caller can drop on
// the floor and continue, rather than aborting the fiber).
//
// Programmer errors - calling read on an unopened file or passing a
// negative offset - still abort the
// fiber loudly.  Only the recoverable I/O bucket flows through
// FileError.
foreign class FileError is Error {
  foreign code      // IOErr.* portable negative I/O reason
  foreign errno     // captured C errno at failure point, 0 if N/A
  foreign message   // human-readable diagnostic, may be null
  foreign toString
}

// Portable I/O reasons shared with the native filesystem API. Success is null.
class IOErr {
  static invalidArgument    { -1 }
  static invalidEncoding    { -2 }
  static notFound           { -3 }
  static permissionDenied   { -4 }
  static alreadyExists      { -5 }
  static notDirectory       { -6 }
  static isDirectory        { -7 }
  static directoryNotEmpty  { -8 }
  static outOfMemory        { -9 }
  static noSpace            { -10 }
  static nameTooLong        { -11 }
  static outOfRange         { -12 }
  static busy               { -13 }
  static readOnly           { -14 }
  static crossDevice        { -15 }
  static tooManyLinks       { -16 }
  static interrupted        { -17 }
  static notSupported       { -18 }
  static io                 { -19 }
  static timedOut           { -20 }
  static cancelled          { -21 }
  static wouldBlock         { -22 }
  static inProgress          { -23 }
  static nameResolution      { -24 }
  static connectionRefused   { -25 }
  static connectionReset     { -26 }
  static connectionAborted   { -27 }
  static notConnected        { -28 }
  static networkUnreachable  { -29 }
  static hostUnreachable     { -30 }
  static addressInUse        { -31 }
  static addressNotAvailable { -32 }
  static messageTooLarge     { -33 }
  static protocol            { -34 }
  static passwordRequired    { -35 }
  static decryptionFailed    { -36 }
  static invalidFormat       { -37 }
  static conflict            { -38 }
}

class Hook {
  foreign static onKey(fn)
  static onKey(key, fn) { onKey(key, 0, fn) }
  foreign static onKey(key, modifiers, fn)
  // Bundled command policy: physical positions on desktop consoles,
  // logical modified input on ANSI/curses consoles. Never register both
  // paths for one command, since a physical edge may precede text.
  static onCommand_(key, fn) {
    if (Host.textTerminal && Host.localControlKeys) return onKey(key, Mod.command, fn)
    return onPhysicalShortcut_(key, Mod.command, fn)
  }
  static onPhysicalShortcut_(key, modifiers, fn) {
    var expected = Key.chord_(key, modifiers)
    return onPhysicalKey {|event|
      if (Key.physicalShortcut_(event) == expected) return fn.call(event)
      return false
    }
  }
  foreign static onPhysicalKey(fn)
  foreign static onPhysicalKey(evdev, fn)
  // Exact post-transport bytes before CTerm classifies controls or text.
  // Use this only for wire protocols; onInput is Unicode text.
  foreign static onWire(fn)
  foreign static onWire(byte, fn)
  foreign static onInput(fn)
  foreign static onInput(byte, fn)
  foreign static onMatch(pattern, fn)

  // onMatchClean is the escape-aware variant: inbound bytes are
  // pre-filtered through SyncTERM's shared ANSI parser (the same
  // state machine ripper.c uses) before reaching the regex VM, so
  // colour codes, cursor moves, and DCS / OSC strings never split
  // a literal sequence.  A pattern like "Welcome" matches even
  // when the BBS sends "We" + ESC[1;33m + "lcome" inline.
  //
  // The callback's return value is IGNORED - onMatchClean is
  // passthrough-only.  Mapping a cleaned-byte match span back to
  // the raw byte stream (escapes interleaved through it) has no
  // unambiguous answer for "consume", so v1 doesn't try; the
  // matched text always reaches the terminal.  Use onInput to drop
  // decoded scalars, or onWire to drop wire bytes.
  foreign static onMatchClean(pattern, fn)

  foreign static onMouse(fn)
  foreign static onMouse(event, fn)
  // Lifecycle hooks: onShellClose fires when the SSH shell channel
  // closes but the session is still up (SFTP transfers may still be
  // in flight); onDisconnect fires after the main loop has exited,
  // during teardown but before the VM is torn down (no SFTP at this
  // point - sftpc_finish has already run).  Handlers take no args,
  // return value is ignored.
  foreign static onShellClose(fn)
  foreign static onDisconnect(fn)
  foreign static every(ms, fn)

  // Wraps every hook fire so a handler that yields directly raises
  // a clear error instead of corrupting the dispatch chain - yielded
  // fibers can't return the bool/string the dispatcher needs to
  // proceed.  Hooks that need to wait on async ops should fire them
  // against a Fiber.new {|r| ... } callback (so the calling fiber
  // returns normally) or wrap the work in `Fiber.new { ... }.call()`
  // (so the child fiber's yield returns to the hook, not the
  // dispatcher's wrenCall).
  //
  // C dispatchers swap their cached call(_) / call() handles for
  // these and pass `Hook` itself as the receiver; the return value
  // lands in slot 0 exactly as if the user's fn had been called
  // directly.  On yield or abort, the return is null - non-bool /
  // non-string slot types fall through the dispatchers' type
  // checks the same way a hook returning the wrong type already
  // does, so the input passes through untouched.
  static dispatch_(fn) {
    var r = null
    var f = Fiber.new { r = fn.call() }
    f.try()
    return finishDispatch_(f, r)
  }
  static dispatch_(fn, arg) {
    var r = null
    var f = Fiber.new { r = fn.call(arg) }
    f.try()
    return finishDispatch_(f, r)
  }
  static finishDispatch_(f, r) {
    if (!f.isDone) {
      System.print("hook handler must not yield directly; " +
                   "wrap parking work in Fiber.new { ... }.call()")
      return null
    }
    if (f.error != null) {
      System.print("hook handler aborted")
      REPL.printTrace_(f)
      return null
    }
    return r
  }
}

// Returned by every Hook.on*/Hook.every registration.  No Wren-side
// constructor - the only path to a HookHandle is via a successful
// registration, so scripts can't fabricate one to remove arbitrary
// hooks.  `remove()` tombstones the underlying entry (a second call
// returns false; metric getters retain their final values once removed).
foreign class HookHandle {
  foreign remove()
  foreign callCount       // total invocations
  foreign totalRuntime    // seconds (sum of every wrenCall in this hook)
  foreign minRuntime      // seconds (smallest single invocation)
  foreign maxRuntime      // seconds (largest single invocation)
  foreign toString
}

// Module-private bridge for state the host owns but Wren shouldn't
// be able to construct directly.  `cacheDirectory` returns a fresh
// Directory for the active session cache pathname; no Wren-visible
// Directory constructor exists. The menu also grants saved personal entry
// caches through DirectoryEntry.cache.
//
// `downloadDir` returns a fresh Directory rooted at the BBS's
// configured DownloadPath, with the relaxed filename predicate
// enabled (1..255 bytes, no path separators / NUL / control bytes /
// `.` / `..` / Windows reserved devices, but spaces / leading dots /
// parens etc. allowed).  Returns null when the path is empty OR
// equal to the user's $HOME - the "unfortunate default" guard for
// old configs.  The Cache strict policy is unchanged; only
// Directories rooted at `downloadDir` (and its subdirectories) take
// the relaxed path.
//
// `uploadPath` returns the configured UploadPath as a String (or
// null if unconfigured).  Pass it as `initialDir` to pickFile /
// pickFiles - uploads MUST go through the picker (which mints a
// per-file consent token); there is no upload-side Directory on
// purpose, so scripts can't enumerate UploadPath or open files
// from it directly.
//
// File-picker option bits.  The numeric assignments are stable because
// scripts may persist or combine them, but the names describe picker
// behavior without exposing the implementation that supplies the UI.
class FilePickerOptions {
  static none { 0 }
  static maskLocked { 1 << 0 }
  static maskCaseSensitive { 1 << 1 }
  static caseSensitiveSort { 1 << 2 }
  static selectDirectory { 1 << 3 }
  static pathMustExist { 1 << 4 }
  static fileMustExist { 1 << 5 }
  static showHidden { 1 << 6 }
  static allowEntry { 1 << 8 }
  static confirmOverwrite { 1 << 9 }
  static confirmCreate { 1 << 10 }
  // Save picker only: offer append as well as overwrite when the
  // selected destination already exists.
  static allowAppend { 1 << 11 }
}

// `pickFile(initialDir, mask, opts)` is the user-consent escape
// hatch for "upload from anywhere": invokes the isolated file picker and
// returns a File foreign whose path is the absolute path the user
// picked, or null on cancel.  initialDir / mask may be null
// (defaults to Session.current.profile.ulDir / `*`).  opts combines FilePickerOptions values.
// The returned File bypasses
// the relaxed-name policy - the picker UI is the sandbox boundary,
// since the user explicitly chose the path.
class Host {
  // Private UI-host state used to select atExit widgets during shutdown.
  foreign static exitRequested_
  // C-owned program theme. Theme caches the decoded data in each VM and
  // uses the generation to refresh only after the active theme changes.
  foreign static themeGeneration
  foreign static themeData
  foreign static defaultThemeData
  foreign static cacheDirectory
  foreign static downloadDir
  foreign static uploadPath
  // initialDir may be a String path, a Directory foreign (uses its
  // path; handles Cache lazy-resolve), or null (defaults to
  // Session.current.profile.ulDir).  Returns a File on OK with a non-null .token
  // (when the picker-token signing key is loaded), or null on
  // cancel.  See pickFiles for the multi-select counterpart.
  foreign static pickFile(initialDir, mask, opts)
  // As above, with a caller-supplied picker title.
  foreign static pickFile(initialDir, mask, opts, title)
  // Multi-select counterpart of pickFile.  Returns a non-empty
  // List<File> on OK (each File has a .token), or null on cancel /
  // empty selection. FilePickerOptions.allowEntry,
  // confirmOverwrite, confirmCreate, and allowAppend cannot be
  // combined with multi-select.
  foreign static pickFiles(initialDir, mask, opts)
  // Save-mode picker. The path field permits a new name, while an
  // existing file requires overwrite confirmation. The user can
  // pick an existing file
  // or type a new filename.  Returns a write-consent File whose
  // .open() succeeds exactly once (with `wbx` for new paths, `wb`
  // when overwrite was confirmed); the File becomes inert after
  // close.  Returns null on cancel.  See Wren.adoc on consent.
  foreign static pickSavePath(initialDir, mask)
  // As above, with save-picker option bits. FilePickerOptions.allowAppend
  // adds an Append choice for an existing destination.
  foreign static pickSavePath(initialDir, mask, opts)
  // Re-open a File from an opaque token previously returned by
  // pickFile / pickFiles via the .token getter.  Returns null when
  // the token's HMAC doesn't verify (signing key rotated / corrupt
  // token), or when the file no longer exists or its content has
  // changed since the user consented (SHA-256 mismatch).  This is
  // the only path by which Wren can construct a File foreign for a
  // path outside the sandboxed Cache / Download roots.  Write-
  // consent Files do NOT receive a token (single-shot doesn't
  // replay across sessions).
  foreign static openLocalFile(token)
  // Status-bar transfer-indicator arrows.  Generic - any Wren
  // script that knows it's transferring something can light them
  // (the SFTP queue is just the first user; future Zmodem / HTTP
  // fetcher / ftp client / etc. can too).  Pure Wren state: both
  // reset to null/false when the VM is torn down on disconnect.
  // The session pump renders the current state on its next pass.
  static uploadArrow { (__upArrow ? true : false) || (Transfer.active && !Transfer.done && Transfer.uploading) }
  static uploadArrow=(b) {
    __upArrow = b
  }
  static downloadArrow { (__downArrow ? true : false) || (Transfer.active && !Transfer.done && !Transfer.uploading) }
  static downloadArrow=(b) {
    __downArrow = b
  }
  // First locally-configured SSH public key, as a Map
  // {"algo": String, "blob": String} (e.g. algo: "ssh-ed25519", blob:
  // base64-encoded), or null when none is available.  Used by
  // sftp_pubkey.wren to append the user's authorized_keys entry on
  // connect.  Returns the same key on every call within a session.
  foreign static sshPublicKey
  // True when SyncTERM was started with -s (safe mode): no
  // auto-connect, no SFTP key writes, no script-driven external
  // commands.  Default status bar surfaces "(SAFE)" when this is set.
  foreign static safeMode

  // True when the active console backend displays character cells through
  // ANSI, ncurses, or the native Win32 console rather than a bitmap window.
  foreign static textTerminal

  // True when SyncTERM reserves Ctrl-Q and Ctrl-S for local UI commands.
  // This is currently limited to ANSI and ncurses; the native Win32 console
  // is a text terminal but has independent keyboard events.
  foreign static localControlKeys

  // Display name of the modifier that produces Alt keycodes on this
  // platform.  "Alt" everywhere except macOS, where it's "Command"
  // because the Quartz backend maps Cmd to Alt (Option is reserved
  // for input-method composition).  Use altKeyName in inline labels
  // ("Disconnect (%(Host.altKeyName)-H)") and altKeyShort where
  // horizontal space is tight ("%(Host.altKeyShort)-Z menu").
  foreign static altKeyName
  foreign static altKeyShort

  // Write a string + newline to standard output and flush.  Distinct
  // from System.print(s), which is captured by the Wren console log;
  // Host.print bypasses the log and emits to the launching shell.
  // Intended for scripts run under the syncterm -W flag that need to
  // report results / progress to the harness.
  foreign static print(s)

  // Full path of the script supplied via the -W command-line flag,
  // or null when -W wasn't used.  Lets a script that is also embedded
  // for normal Alt+key dispatch detect command-line invocation and
  // run itself immediately:
  //
  //   if (Host.launchScript != null && Host.launchScript.endsWith("foo.wren")) {
  //     Foo.run()
  //   }
  foreign static launchScript

  // Operation Overkill ][ terminal support is part of the base build.
  foreign static haveOOII
  // Highest valid OOII mode value (3). The
  // online menu wraps the OOII increment back to 0 when it exceeds
  // this cap, matching the historical SM_OOII cycle.
  foreign static maxOOIIMode

  // Throttled-output ladder.  outputRates[i] is the BPS value, and
  // outputRateNames[i] is the human label (e.g. "115200" for index
  // 10, "Current" for the trailing 0 = no-cap entry).  Indexes line
  // up between the two lists; pass any rate value to
  // CTerm.throttleSpeed=.
  foreign static outputRates
  foreign static outputRateNames

  // Transfer log threshold (0 = Emergency, ..., 7 = Debug).  Set via
  // logLevel=; Host.logLevelNames returns the human labels indexed by
  // the same number.
  foreign static logLevel
  foreign static logLevel=(n)
  foreign static logLevelNames

  // Suspend the active terminal and enter the trusted persistent menu VM.
  // The connected VM receives no menu objects or capabilities.
  static editBBSList() {
    Screen.checked_(Screen.modalRun(Fn.new { editBBSList_() }))
  }
  foreign static editBBSList_()

  // Wren console activity since the user last visited the REPL.
  // logUnread covers any non-empty append (script print output, hook
  // metrics, etc.); logUnreadError is the subset for compile/runtime
  // errors and stack frames.  The connected status bar and menu's
  // top-right corner show a CP437 ‼ indicator -- red when
  // logUnreadError is set, yellow when only logUnread is set, blank
  // otherwise.  Each VM has its own counters; visiting its console
  // pane clears them through Console.markSeen().
  foreign static logUnread
  foreign static logUnreadError

  // ANSI music mode display strings and help blurb.  Same source as
  // the bbslist editor's music picker, so a Wren-side dialog and the
  // C-side dialog stay aligned.  `musicNames` is a List<String>
  // indexed by `MusicMode`; `musicHelp` is the multi-line help blurb
  // shown above the picker.
  foreign static musicNames
  foreign static musicHelp
}

// Wren-driven status bar.  Replaces the old Hook.onStatus.  Install a
// callable via `Status.callable = Fn.new { |surface| ... }` and the
// host will invoke it on every status-bar update with a width×1 Surface
// that has been pre-filled to the default attribute (yellow on blue,
// spaces).  The callable mutates cells in place; its return value is
// ignored.  Set `Status.callable = null` to detach.
//
// The default implementation lives in
// scripts/auto/connected/status_default.wren and runs on every
// connection.  To override, drop a same-named file in your auto-load
// dir or assign `Status.callable = ...` from any later script.
//
// `Status.enabled` is false while the SyncTERM-managed row is hidden
// (DECSSDT or startup config); the callable should early-return then.
class Status {
  foreign static callable
  foreign static callable=(fn)
  // Optional Fn{} returning a complete, human-readable status snapshot.
  // The disconnect F1 popup uses this instead of scraping the visual row,
  // whose narrow layouts deliberately omit fields.
  foreign static details
  foreign static details=(fn)
  foreign static enabled
  foreign static hoverUri
}

// Transfer-window mailbox bridge.  The Wren TransferApp drives the
// progress / log UI while a worker thread (spawned in beginSession)
// runs the C protocol code and produces events into a thread-safe
// ring (logs) + snapshot pair (tick state).
//
//   Transfer.beginSession("fake")   // null or TransferError
//   ...                             // App.run loop drains and renders
//   Transfer.endSession()           // ask worker to stop, join, reset
//
// Per-tick consumer pattern:
//   for (entry in Transfer.drainLog()) logView.append(entry[0], entry[1])
//   if (Transfer.tickDirty) progressBar.set(...Transfer.snapshot()...)
//   if (Transfer.done) break
//   if (escapePressed) Transfer.requestAbort()
//
// snapshot() returns a fixed-shape List (see TickStateView in the
// transfer_app module for a named-field wrapper): four text rows followed
// by bytes-current and bytes-total. Protocol dispatch starts a worker and
// returns null or TransferError immediately; the owner keeps pumping terminal
// and UI input. After done, result is null or TransferError until endSession.
class Transfer {
  foreign static active
  foreign static uploading
  foreign static inlineUpload
  foreign static inlineProgress
  foreign static autoPending_
  foreign static cancelAuto_()
  foreign static ripUploadName_
  foreign static acceptRipUpload_()
  foreign static beginSession(kind)
  foreign static endSession()
  foreign static drainLog()
  foreign static tickDirty
  foreign static snapshot()
  foreign static done
  // Requires an active completed worker: null or TransferError.
  foreign static result
  foreign static requestAbort()
  foreign static aborted

  // Worker → main-thread dialog channel.  When a worker thread needs
  // a UI decision (overwrite-duplicate-file etc.) it parks on a
  // condvar; dialogPending returns the pending kind code (>0) for the
  // TransferApp's tick body to surface as a popup, then dialogRespond
  // hands the user's choice back to wake the worker.
  //
  //   XFER_DLG_NONE      = 0
  //   XFER_DLG_DUPLICATE = 1
  //   XFER_DLG_FILENAME  = 2
  //
  //   XFER_DLG_OVERWRITE = 0   (duplicate response)
  //   XFER_DLG_SKIP      = 1
  //   XFER_DLG_RENAME    = 2
  foreign static dialogPending
  foreign static dialogFilename
  foreign static dialogRespond(resp, newName)

  // Upload sources are readable File grants. Path Strings are also accepted
  // inside the current BBS cache or configured upload directory; descendant
  // symlinks/reparse points are rejected. Other paths require picker consent.
  // `kind`: "zmodem", "ymodem", "xmodem-1k", "xmodem-128", "ascii", "raw".
  // `lastCh` seeds an already-consumed X/YMODEM handshake byte (normally 0).
  foreign static upload(kind, source, lastCh)

  // Batch "zmodem" / "ymodem" uploads. Every source must pass the same grant
  // check before any worker starts; a List may contain Files and exempt paths.
  foreign static uploadBatch(kind, sources, lastCh)

  // Download dispatch.  `kind` is one of:
  //   "zmodem", "ymodem-g", "ymodem", "xmodem-crc",
  //   "xmodem-chksum", "xmodem-g", "cet"
  // `path` must be a non-null filename for the three XMODEM receive modes
  // (plain XMODEM has no header so the user pre-names the file);
  // ignored otherwise.  CET writes its captured frame buffer back
  // into cterm internally on completion.
  foreign static download(kind, path)
}

// Startup/completion failures preserve the native IOErr code. Owned immutable
// values remain valid after Transfer.endSession() or a subsequent transfer.
foreign class TransferError is Error {
  foreign code
  foreign message
  foreign toString
}

// Platform identification.  `name` returns the uname(2) sysname on
// POSIX (e.g. "FreeBSD", "Linux", "Darwin"), "Windows" on Windows,
// and "Unknown" elsewhere.  Useful for branching on host OS in
// scripts; no further OS surface (no shell exec, no stdio, no
// process model) is exposed.
class Platform {
  foreign static name
  // Fresh ordered UI language preferences, e.g. ["fr-CA", "en"].
  // Native Windows/macOS UI settings; locale environment on other POSIX hosts.
  // Defaults to ["en"]. Does not alter the process locale or terminal encoding.
  foreign static languages
}

// Owned asynchronous completion. Native work retains the operation until it
// finishes or is cancelled. Awaiting defers unrelated Wake.post messages.
foreign class Operation {
  foreign isDone
  foreign cancelled
  foreign result
  foreign cancel()
  foreign await_(fiber)
  await() {
    while (!isDone) {
      await_(Fiber.current)
      Fiber.yield()
    }
    return result
  }
  then(fn) {
    if (!(fn is Fn)) Fiber.abort("Operation.then expects a Fn")
    Fiber.new { fn.call(await()) }.call()
    return this
  }
}
foreign class OperationError is Error {
  foreign code
  foreign message
  foreign toString
}

class Timer {
  // Returns an Operation completing with null. At most 32 pending per VM.
  foreign static after(ms)
  foreign static now
}

// Number-to-string formatters for human-readable display.
class Format {
  // bytes(n) - auto-picks K / M / G / T / P, 1 decimal precision.
  foreign static bytes(n)
  // duration(seconds) - auto-picks s / m / h / d / y, 1 decimal precision.
  foreign static duration(seconds)
}

// Every SFTP operation returns an Operation. Use .await() for its value or
// .then(fn) to observe completion without parking the caller. Recoverable
// failures, including immediate failures, are SFTPError result values.
class SFTP {
  // Opens the subsystem only on demand; queued completion is true or SFTPError.
  foreign static start()
  foreign static available
  foreign static pubdir
  foreign static lname

  foreign static realpath(path)
  foreign static stat(path)
  foreign static opendir(path)
  foreign static open(path, flags)
  foreign static mkdir(path)
  foreign static rmdir(path)
  foreign static remove(path)
  foreign static rename(oldpath, newpath)
  foreign static setMtime(path, mtime)
  foreign static descs(path)
}

// Open-flag bitmask for SFTP.open.  Values match the SSH_FXF_* wire
// constants and may be OR'd together, e.g.
//   SFTP.open(path, FileFlag.write | FileFlag.creat | FileFlag.trunc)
class FileFlag {
  static read   { 0x00000001 }
  static write  { 0x00000002 }
  static append { 0x00000004 }
  static creat  { 0x00000008 }
  static trunc  { 0x00000010 }
  static excl   { 0x00000020 }
}

// One entry in a RemoteDirectory.readEntries result.  `longname` is null when the
// server didn't negotiate the lname@syncterm.net extension; `hash` is
// null unless sha1s/md5s was advertised.
foreign class SFTPEntry {
  foreign name
  foreign longname
  foreign stat
  foreign isDir
  foreign hasLongDesc
  foreign hash
  foreign toString
}

// Immutable native metadata from SFTP.stat or on access to SFTPEntry.stat.
// Time fields are POSIX seconds; `mode` is the
// SFTP-wire permissions bits (Unix-style when the server is POSIX).
foreign class SFTPStat {
  foreign size
  foreign mtime
  foreign atime
  foreign mode
  foreign uid
  foreign gid
  foreign toString
}

// Owned resources: pending operations root their receiver until native completion.
foreign class RemoteFile {
  foreign readBytes(count, offset)
  foreign writeBytes(bytes, offset)
  foreign appendBytes(bytes)
  foreign readable
  foreign writable
  foreign append
  foreign close()
  foreign isClosed
  foreign isClosing
  use(fn) {
    if (!(fn is Fn)) Fiber.abort("RemoteFile.use expects a Fn")
    return Cleanup_.runAsync_(Fn.new { fn.call(this) }, Fn.new {
      var result = close().await()
      if (result is Error) Fiber.abort(result)
    })
  }
}
foreign class RemoteDirectory {
  foreign readEntries()
  foreign close()
  foreign isClosed
  foreign isClosing
  use(fn) {
    if (!(fn is Fn)) Fiber.abort("RemoteDirectory.use expects a Fn")
    return Cleanup_.runAsync_(Fn.new { fn.call(this) }, Fn.new {
      var result = close().await()
      if (result is Error) Fiber.abort(result)
    })
  }
}

// Error result, surfaced in place of the typed result foreign when an
// SFTP op fails.  Two error layers - distinguish via `code`:
//   code != 0   - library/transport-level failure (sftp_err_code_t).
//                 serverStatus is meaningless.
//   code == 0   - server returned a STATUS reply with an error code;
//                 read serverStatus for the SSH_FX_* value (e.g.
//                 SSH_FX_NO_SUCH_FILE, SSH_FX_PERMISSION_DENIED).
// `message` is human-readable diagnostic text accumulated by the
// library (may be null).  `isTransient` is true for failures that
// may succeed on retry (transport drops, aborts, OOM).
foreign class SFTPError is Error {
  foreign code
  foreign serverStatus
  foreign message
  foreign isTransient
  foreign toString
}

// Cache singleton.  Bound at module-load time so any script that
// `import "syncterm" for Cache`s sees a fully-formed Directory.
var Cache = Host.cacheDirectory
// Download root - null when the BBS's DownloadPath is empty, set to
// $HOME, or doesn't exist as a directory on disk (see
// Host.downloadDir docstring).  Bound at module-load time, like
// Cache; the Wren VM is per-session, so a reconnect to a different
// BBS reloads the module and re-binds it.  Scripts MUST null-check
// before use.  No `Upload` counterpart exists - uploads must go
// through `Host.pickFile`/`Host.pickFiles` (which mint a per-file
// consent token); use `Host.uploadPath` if you need the configured
// UploadPath as the picker's `initialDir`.
var Download = Host.downloadDir

// WON - Wren Object Notation serialization.
//
// Serializes / deserializes values built from Null, Bool, Num, String, List,
// Map, Range, and Sequence (the latter two coerce to List).  Output is valid
// Wren-literal syntax.  Cycles abort the fiber.
//
// Two serialize variants:
//   serialize(v)         - strict; aborts on unsupported types or NaN/Inf.
//   serializeLossy(v)    - silently omits unsupported items from Lists/Maps;
//                          a top-level unsupported value becomes "null".
// Each takes an optional indent String for pretty-printing:
//   WON.serialize(v)            // compact: {"a":1,"b":[1,2,3]}
//   WON.serialize(v, "  ")      // pretty, 2-space indent
//   WON.serialize(v, "\t")      // pretty, tab indent
//
// deserialize runs a hardened C parser - no eval - and accepts arbitrary
// whitespace and trailing commas between tokens.  Map keys must be a value
// Wren can hash (Bool, Num, String, Range, null); List/Map keys are rejected.
//
// Parse failures (truncated input, syntax errors, malformed escapes,
// nesting too deep, trailing data) return a WONError IN PLACE of the
// parsed value - they do NOT abort the fiber.  The script can demux:
//   var v = WON.deserialize(text)
//   if (v is WONError) { ... fall back to defaults ... }
// Programmer errors (passing a non-String) DO still abort.
class WON {
  static serialize(value)              { serialize_(value, null,   true)  }
  static serialize(value, indent)      { serialize_(value, indent, true)  }
  static serializeLossy(value)         { serialize_(value, null,   false) }
  static serializeLossy(value, indent) { serialize_(value, indent, false) }

  foreign static deserialize(text)

  // ---- internal ----

  static serialize_(value, indent, strict) {
    if (indent != null && !(indent is String)) {
      Fiber.abort("WON.serialize: indent must be a String or null")
    }
    if (!strict && !isWritable_(value)) return "null"
    return write_(value, indent, 0, [], strict)
  }

  static write_(value, indent, depth, stack, strict) {
    if (value == null)     return "null"
    if (value is Bool)     return value ? "true" : "false"
    if (value is Num)      return writeNum_(value, strict)
    if (value is String)   return writeString_(value)
    if (value is List)     return writeList_(value,        indent, depth, stack, strict)
    if (value is Map)      return writeMap_( value,        indent, depth, stack, strict)
    if (value is Range)    return writeList_(value.toList, indent, depth, stack, strict)
    if (value is Sequence) return writeList_(value.toList, indent, depth, stack, strict)
    if (strict) Fiber.abort("WON.serialize: unsupported type %(value.type)")
    return "null"
  }

  static writeNum_(n, strict) {
    if (n != n) {
      if (strict) Fiber.abort("WON.serialize: NaN has no Wren literal")
      return "null"
    }
    if (n == 1/0 || n == -1/0) {
      if (strict) Fiber.abort("WON.serialize: Infinity has no Wren literal")
      return "null"
    }
    return n.toString
  }

  // Escape every byte that could break a Wren string literal.  Notably
  // includes `%` (would otherwise start an interpolation) and all control
  // chars; UTF-8 high bytes pass through verbatim.
  static writeString_(s) {
    var parts = ["\""]
    for (b in s.bytes) parts.add(escapeByte_(b))
    parts.add("\"")
    return parts.join("")
  }

  static escapeByte_(b) {
    if (b == 0x22) return "\\\""           // "
    if (b == 0x5C) return "\\\\"           // \
    if (b == 0x25) return "\\\%"           // % (must escape both: \\ for the
                                            // backslash, \% for the %, since
                                            // an unescaped % starts interp)
    if (b == 0x0A) return "\\n"
    if (b == 0x0D) return "\\r"
    if (b == 0x09) return "\\t"
    if (b == 0x08) return "\\b"
    if (b == 0x0C) return "\\f"
    if (b == 0x0B) return "\\v"
    if (b == 0x07) return "\\a"
    if (b == 0x1B) return "\\e"
    if (b == 0x00) return "\\0"
    if (b < 0x20 || b == 0x7F) return "\\x" + hex2_(b)
    return String.fromByte(b)
  }

  static hex2_(b) { hexNibble_((b/16).floor) + hexNibble_(b % 16) }
  static hexNibble_(n) {
    // 0x30='0', 0x37+10=0x41='A', 0x37+15=0x46='F'
    return n < 10 ? String.fromByte(0x30 + n) : String.fromByte(0x37 + n)
  }

  static writeList_(list, indent, depth, stack, strict) {
    if (stack.contains(list)) Fiber.abort("WON.serialize: cycle in List")
    if (list.count == 0) return "[]"
    stack.add(list)
    var parts = []
    for (item in list) {
      if (!strict && !isWritable_(item)) continue
      parts.add(write_(item, indent, depth + 1, stack, strict))
    }
    stack.removeAt(-1)
    return container_("[", "]", parts, indent, depth)
  }

  static writeMap_(map, indent, depth, stack, strict) {
    if (stack.contains(map)) Fiber.abort("WON.serialize: cycle in Map")
    if (map.count == 0) return "{}"
    stack.add(map)
    var parts = []
    var colon = indent == null ? ":" : ": "
    for (key in map.keys) {
      var val = map[key]
      if (!strict && (!isWritable_(key) || !isWritable_(val))) continue
      var ks = write_(key, indent, depth + 1, stack, strict)
      var vs = write_(val, indent, depth + 1, stack, strict)
      parts.add(ks + colon + vs)
    }
    stack.removeAt(-1)
    return container_("{", "}", parts, indent, depth)
  }

  static container_(open, close, parts, indent, depth) {
    if (parts.count == 0) return open + close
    if (indent == null)   return open + parts.join(",") + close
    var inner = "\n" + repeat_(indent, depth + 1)
    var outer = "\n" + repeat_(indent, depth)
    return open + inner + parts.join("," + inner) + outer + close
  }

  static repeat_(s, n) {
    var r = ""
    var i = 0
    while (i < n) {
      r = r + s
      i = i + 1
    }
    return r
  }

  // True if write_ would handle [value] without aborting.  Used by lossy
  // mode to decide whether to skip an entry.
  static isWritable_(value) {
    if (value == null)     return true
    if (value is Bool)     return true
    if (value is Num)      return value == value && value != 1/0 && value != -1/0
    if (value is String)   return true
    if (value is List)     return true
    if (value is Map)      return true
    if (value is Range)    return true
    if (value is Sequence) return true
    return false
  }
}

// Returned IN PLACE of the parsed value when WON.deserialize hits a
// parse failure on user-supplied input.  The script demuxes via
// `is WONError` and reads `.code` (a `WONErr.*` value), `.offset`
// (byte offset where the error was detected), `.message` (human-
// readable diagnostic), or `.toString` for log lines.
foreign class WONError is Error {
  foreign code      // WONErr.* enum
  foreign offset    // byte offset where the error was detected
  foreign message   // diagnostic text
  foreign toString
}

// WONError code values; see Wren.adoc § WONError for the rule
// matching each code to a failure mode.
class WONErr {
  static ok            { 0 }
  static syntax        { 1 }
  static truncated     { 2 }
  static invalidEscape { 3 }
  static invalidKey    { 4 }
  static tooDeep       { 5 }
  static trailingData  { 6 }
}

// Canonical emoji metadata shared by menu, picker and connected VMs.
class EmojiEntry {
  construct from_(id) { _id = id }
  id_ { _id }
  text { Emoji.text_(_id) }
  name { Emoji.name_(_id) }
  keywords {
    var words = []
    for (field in Emoji.keywords_(_id)) {
      for (word in field.split("|")) {
        word = word.trim()
        if (word.count > 0 && !words.contains(word)) words.add(word)
      }
    }
    return words
  }
  variants { Emoji.variants_(_id).map {|id| EmojiEntry.from_(id) }.toList }
}

class Emoji {
  foreign static canSend
  foreign static entries_
  foreign static search_(query)
  foreign static text_(id)
  foreign static name_(id)
  foreign static keywords_(id)
  foreign static variants_(id)
  foreign static recent_
  foreign static recordUse_(id)
  static entries { entries_.map {|id| EmojiEntry.from_(id) }.toList }
  static search(query) {
    if (query.count == 0) return entries
    return search_(query).map {|id| EmojiEntry.from_(id) }.toList
  }
  static recent { recent_.map {|id| EmojiEntry.from_(id) }.toList }
  // Promote session recents, then persist: null/FileError. Safe mode skips writes.
  static recordUse(entry) { recordUse_(entry.id_) }
}
