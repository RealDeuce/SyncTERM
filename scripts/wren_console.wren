import "syncterm" for Cleanup_
// SyncTERM Wren console (REPL).  Saves the screen, presents the
// current VM's always-on print/error scrollback, accepts immediate-mode
// Wren input via REPL.eval, and restores on exit.  The connected and menu
// entry points bind Ctrl+` (CIO_KEY_WREN_CONSOLE = 0x29E0) separately.
//
// Loaded into its own "wren_console" module; pulls bindings from the
// foundational "syncterm" module via import.  Lines submitted at the
// prompt are evaluated in the module chosen by the entry point via
// REPL.eval, so `var x = ...` and `class Foo {...}` declarations
// persist across submissions inside that module.  The connected entry
// uses "syncterm"; the menu uses "main_menu".
//
// `/in <module>` switches the eval target, letting the user inspect
// or mutate any other module that's been loaded.

import "syncterm" for Screen, Console, Input, Clipboard, Host, Key, Mod, PhysicalKeyEvent,
    REPL, LogSource, KeyEvent, MouseEvent, Mouse

class WrenConsole {
  // __history (static field) keeps submitted lines across run()
  // invocations so reopening the console doesn't lose context.
  // Up/Down arrows walk it; if there's anything typed at the time the
  // user hits Up, that becomes a *prefix anchor* — only entries that
  // start with it are visited.  Editing the line (printable, backspace,
  // Ctrl+W) cancels the search and the typed text becomes the new
  // free-form input.

  // Largest i < beforeIdx where __history[i].startsWith(prefix), or -1.
  static historyFindBackward_(prefix, beforeIdx) {
    var i = beforeIdx - 1
    while (i >= 0) {
      if (__history[i].startsWith(prefix)) return i
      i = i - 1
    }
    return -1
  }

  // Smallest i > afterIdx where __history[i].startsWith(prefix), or -1.
  static historyFindForward_(prefix, afterIdx) {
    var i = afterIdx + 1
    while (i < __history.count) {
      if (__history[i].startsWith(prefix)) return i
      i = i + 1
    }
    return -1
  }

  // Register a /<name> command other modules can plug into the
  // console.  `help` is the line shown by /? after the command name.
  // `fn` is invoked with the raw argument string — everything after
  // the name and its first separating space, "" if none — and runs
  // inside a Fiber so an abort in the handler doesn't tear down the
  // console.  Re-registering an existing name overwrites.  Names
  // can't contain spaces (the dispatcher splits on the first one).
  static register(name, help, fn) {
    if (name.contains(" ")) {
      Fiber.abort("WrenConsole.register: name '%(name)' contains a space")
    }
    if (__commands == null) __commands = {}
    __commands[name] = [help, fn]
  }

  // Drop a previously-registered command; no-op if not registered.
  static unregister(name) {
    if (__commands != null) __commands.remove(name)
  }

  // Currently-registered command names, sorted byte-wise.  Mostly a
  // hook for tests and tooling; the run-loop uses __commands directly.
  static commands {
    if (__commands == null) return []
    var names = __commands.keys.toList
    names.sort {|a, b| stringLT_(a, b) }
    return names
  }

  // Byte-wise less-than for two Strings.  Wren's String doesn't
  // implement `<` (Unicode collation is out of scope for the core),
  // so List.sort() needs a comparator for any sort that touches
  // strings.  Module names are ASCII, so byte order is fine.
  static stringLT_(a, b) {
    var ab = a.bytes
    var bb = b.bytes
    var n  = ab.count.min(bb.count)
    for (i in 0...n) {
      if (ab[i] != bb[i]) return ab[i] < bb[i]
    }
    return ab.count < bb.count
  }

  // Decode one strictly-valid UTF-8 scalar at a byte offset.  Wren's
  // built-in string iterator accepts malformed and non-canonical sequences,
  // which is unsuitable for deciding editor boundaries.  Return -1 for an
  // invalid byte so callers can reject or escape it without losing the valid
  // text around it.
  static utf8CodepointAt_(s, index) {
    var bytes = s.bytes
    var count = bytes.count
    if (index < 0 || index >= count) return -1
    var b0 = bytes[index]
    if (b0 < 0x80) return b0
    if (b0 < 0xc2 || b0 > 0xf4) return -1
    if (b0 <= 0xdf) {
      if (index + 1 >= count) return -1
      var b1 = bytes[index + 1]
      if (b1 < 0x80 || b1 > 0xbf) return -1
      return ((b0 & 0x1f) << 6) | (b1 & 0x3f)
    }
    if (b0 <= 0xef) {
      if (index + 2 >= count) return -1
      var b1 = bytes[index + 1]
      var b2 = bytes[index + 2]
      if (b1 < 0x80 || b1 > 0xbf || b2 < 0x80 || b2 > 0xbf) return -1
      if ((b0 == 0xe0 && b1 < 0xa0) || (b0 == 0xed && b1 >= 0xa0)) return -1
      return ((b0 & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f)
    }
    if (index + 3 >= count) return -1
    var b1 = bytes[index + 1]
    var b2 = bytes[index + 2]
    var b3 = bytes[index + 3]
    if (b1 < 0x80 || b1 > 0xbf || b2 < 0x80 || b2 > 0xbf ||
        b3 < 0x80 || b3 > 0xbf) return -1
    if ((b0 == 0xf0 && b1 < 0x90) || (b0 == 0xf4 && b1 > 0x8f)) return -1
    return ((b0 & 0x07) << 18) | ((b1 & 0x3f) << 12) |
        ((b2 & 0x3f) << 6) | (b3 & 0x3f)
  }

  static utf8Length_(codepoint) {
    if (codepoint < 0) return 0
    if (codepoint < 0x80) return 1
    if (codepoint < 0x800) return 2
    if (codepoint < 0x10000) return 3
    return 4
  }

  static nextIndex_(s, index) {
    if (index >= s.bytes.count) return s.bytes.count
    var length = utf8Length_(utf8CodepointAt_(s, index))
    return index + (length == 0 ? 1 : length)
  }

  static previousIndex_(s, index) {
    if (index <= 0) return 0
    var bytes = s.bytes
    var candidate = index - 1
    while (candidate > 0 && bytes[candidate] >= 0x80 &&
        bytes[candidate] <= 0xbf) candidate = candidate - 1
    var length = utf8Length_(utf8CodepointAt_(s, candidate))
    return length > 0 && candidate + length == index ? candidate : index - 1
  }

  // Backward kill-word (Ctrl+W).  Strip trailing ASCII spaces, then strip
  // the run of non-spaces before it.  This is deliberately independent of
  // host locale; any codepoint other than U+0020 belongs to the word.
  static killWord_(s) {
    var i = s.bytes.count
    while (i > 0) {
      var previous = previousIndex_(s, i)
      if (s[previous...i] != " ") break
      i = previous
    }
    while (i > 0) {
      var previous = previousIndex_(s, i)
      if (s[previous...i] == " ") break
      i = previous
    }
    return s[0...i]
  }

  // Apply clipboard text to the current edit buffer.  Newlines submit the
  // current line, with CRLF treated as one newline.  Tab and valid printable
  // Unicode scalars are retained; C0 (other than Tab/newline), DEL, C1, and
  // malformed UTF-8 are discarded.  The returned cursor remains a byte
  // offset because Wren string slices use byte offsets.
  static pasteText_(pasted, current, input, cursor) {
    var i = 0
    var previousCR = false
    while (i < pasted.bytes.count) {
      var codepoint = utf8CodepointAt_(pasted, i)
      var length = utf8Length_(codepoint)
      if (length == 0) {
        length = 1
        previousCR = false
      } else if (codepoint == 0x0d) {
        current = submitLine_(input, current)
        input = ""
        cursor = 0
        previousCR = true
      } else if (codepoint == 0x0a) {
        if (!previousCR) {
          current = submitLine_(input, current)
          input = ""
          cursor = 0
        }
        previousCR = false
      } else {
        previousCR = false
        var printable = codepoint == 0x09 ||
            (codepoint >= 0x20 && codepoint != 0x7f &&
            !(codepoint >= 0x80 && codepoint <= 0x9f))
        if (printable) {
          var text = pasted[i...(i + length)]
          input = input[0...cursor] + text +
              input[cursor...input.bytes.count]
          cursor = cursor + length
        }
      }
      i = i + length
    }
    return [current, input, cursor]
  }

  // Append `line` to history if non-empty and not a dup of the last
  // entry, then dispatch it like Enter would.  Returns the (possibly
  // changed) current module.
  static submitLine_(line, current) {
    if (__history == null) __history = []
    if (line.count > 0 &&
        (__history.count == 0 || __history[__history.count - 1] != line)) {
      __history.add(line)
    }
    return handleLine_(line, current)
  }

  static run() { run("syncterm") }

  static setupMouse_() {
    var saved = Input.mouseEvents
    Input.mouseEvents = 0
    Input.enableMouseEvent(Mouse.button1DragStart)
    Input.enableMouseEvent(Mouse.button1DragMove)
    Input.enableMouseEvent(Mouse.button1DragEnd)
    Input.enableMouseEvent(Mouse.button2Click)
    return saved
  }

  static run(initialModule) {
    if (__history == null) __history = []
    var saved = Screen.checked_(Screen.save())
    var savedMouse = Input.mouseEvents
    var origBounds = null
    var f = Fiber.new {
      Cleanup_.run(Fn.new {
        Screen.checked_(Screen.setAttr(0x07))
        origBounds = Screen.checked_(Screen.window.bounds)
        var size = Screen.checked_(Screen.size)
        Screen.checked_(Screen.window.setBounds([1, 1, size[0], size[1]]))
        setupMouse_()
        runBody_(initialModule)
      }, Fn.new {
        Screen.cleanup_([
          Fn.new {
            if (origBounds != null) Screen.checked_(Screen.window.setBounds(origBounds))
          },
          Fn.new { Screen.checked_(Screen.restore(saved)) },
          Fn.new { Input.mouseEvents = savedMouse }
        ])
      })
    }
    f.try()
    if (f.error != null) {
      System.print("console aborted: %(f.error)")
      REPL.printTrace_(f)
    } else {
      Console.markSeen()
    }
  }

  static runBody_(initialModule) {
    var currentModule = initialModule

    var screenSize = Screen.checked_(Screen.size)

    // Scrollback state: scrollTop == -1 means "live mode" (auto-tail
    // — new entries paint at the bottom as they arrive).  Any other
    // value is a display-row index into renderAllRows_'s output: the
    // row pinned to the top of the viewport.  PageUp/PageDown move
    // by a viewport's worth of rows, Up/Down move by one row, all in
    // exact display-row units.  Any typed printable character or
    // Enter rejoins live mode.
    var scrollTop    = -1
    var winRows      = screenSize[1]
    var viewportRows = winRows - 1

    // One-time paint of whatever's currently in the log buffer.
    // Subsequent loop iterations only emit entries that arrived
    // *after* `lastTotal`; conio's built-in scroll-on-bottom-row
    // behavior moves history up naturally as new entries arrive.
    Screen.checked_(Screen.window.clear())
    Screen.checked_(Screen.window.setPosition([1, 1]))
    var lastTotal = paintNew_(0, true)

    var input  = ""
    var cursor = 0   // byte index into `input`; 0..input.bytes.count
    var promptRow = Screen.checked_(Screen.window.position)[1]
    drawPrompt_(promptRow, currentModule, input, cursor)

    __quit = false
    // History search state: histIdx == -1 means "not searching"; the
    // buffer is whatever the user has typed.  Otherwise histIdx is the
    // current index into __history and histAnchor is the prefix the
    // user had typed at the moment they first pressed Up.
    var histIdx    = -1
    var histAnchor = ""
    var done = false
    while (!done && !__quit) {
      var ev = Input.next()

      // Handle the event.
      var redrawPrompt = false
      if (ev is MouseEvent) {
        // 7  = CIOLIB_BUTTON_1_DRAG_START — hand off to the existing
        //      select-and-copy loop; it consumes the rest of the drag
        //      itself, then restores the screen on exit.
        // 12 = CIOLIB_BUTTON_2_CLICK     — paste clipboard into the
        //      input line.  CR, LF, and CRLF submit the line in progress;
        //      invalid text and controls other than Tab are skipped.
        if (ev.event == 7) {
          Input.mousedrag()
          redrawPrompt = true
        } else if (ev.event == 12) {
          var pasted = Screen.checked_(Clipboard.text)
          if (pasted != null) {
            var result = pasteText_(pasted, currentModule, input, cursor)
            currentModule = result[0]
            input = result[1]
            cursor = result[2]
            histIdx = -1
            redrawPrompt = true
          }
        }
      // Physical command edges remain separate from committed text.
      } else if (ev is KeyEvent || ev is PhysicalKeyEvent) {
        var key = ev is KeyEvent ? ev.shortcut_ : Key.physicalShortcut_(ev)
        var controlKeyExit = Screen.checked_(Host.localControlKeys) && (key == Key.chord_(113, Mod.control) || key == Key.chord_(115, Mod.control))
        if (key == Key.chord_(96, Mod.control) || key == Key.escape || key == Key.quit ||
            controlKeyExit) {
          done = true
        } else if (ev is PhysicalKeyEvent) {
          continue
        } else if (key == Key.enter || key == Key.chord_(Key.enter, Mod.control)) { // CR or LF
          // Submitting in scrollback mode rejoins live first so the
          // eval's output paints on the live tail rather than into
          // the frozen viewport.
          if (scrollTop != -1) {
            scrollTop = -1
            Screen.checked_(Screen.window.clear())
            Screen.checked_(Screen.window.setPosition([1, 1]))
            lastTotal = paintNew_(0, true)
            promptRow = Screen.checked_(Screen.window.position)[1]
          }
          currentModule = submitLine_(input, currentModule)
          input  = ""
          cursor = 0
          histIdx = -1
          redrawPrompt = true
        } else if (key == Key.backspace || key == Key.delChar) { // BS or DEL
          if (cursor > 0) {
            var previous = previousIndex_(input, cursor)
            input  = input[0...previous] + input[cursor...input.bytes.count]
            cursor = previous
            histIdx = -1
            redrawPrompt = true
          }
        } else if (key == Key.delete) {              // forward-delete
          if (cursor < input.bytes.count) {
            var next = nextIndex_(input, cursor)
            input = input[0...cursor] + input[next...input.bytes.count]
            histIdx = -1
            redrawPrompt = true
          }
        } else if (key == Key.left) {
          if (cursor > 0) {
            cursor = previousIndex_(input, cursor)
            redrawPrompt = true
          }
        } else if (key == Key.right) {
          if (cursor < input.bytes.count) {
            cursor = nextIndex_(input, cursor)
            redrawPrompt = true
          }
        } else if (key == Key.home) {
          if (cursor != 0) {
            cursor = 0
            redrawPrompt = true
          }
        } else if (key == Key.end) {
          if (cursor != input.bytes.count) {
            cursor = input.bytes.count
            redrawPrompt = true
          }
        } else if (key == Key.chord_(119, Mod.control)) {                  // Ctrl+W
          // Kill the word ending at the cursor; tail (cursor..end)
          // stays put, cursor lands where the killed run started.
          var head = killWord_(input[0...cursor])
          input  = head + input[cursor...input.bytes.count]
          cursor = head.bytes.count
          histIdx = -1
          redrawPrompt = true
        } else if (key == Key.pageUp) {
          // Enter scrollback if in live mode by anchoring scrollTop
          // at the row that's currently at the top of the live view,
          // then move up by one viewport (minus 1 for overlap).
          // No-op if the entire log fits in the live viewport, or
          // if we're already pinned at the top — repainting in
          // either case is wasted work and triggers a conio
          // scroll-up that would drop the top row.
          var rendered  = renderAllRows_()
          var totalRows = rendered.count
          if (totalRows > viewportRows) {
            var firstEntry = (scrollTop == -1)
            if (firstEntry) scrollTop = totalRows - viewportRows
            var newTop = (scrollTop - (viewportRows - 1)).max(0)
            if (firstEntry || newTop != scrollTop) {
              scrollTop = newTop
              paintViewport_(rendered, scrollTop, viewportRows)
              promptRow = winRows
              redrawPrompt = true
            }
          }
        } else if (key == Key.pageDown) {
          if (scrollTop != -1) {
            var rendered  = renderAllRows_()
            var totalRows = rendered.count
            scrollTop = scrollTop + (viewportRows - 1)
            if (scrollTop + viewportRows >= totalRows) {
              // Caught up — back to live.
              scrollTop = -1
              Screen.checked_(Screen.window.clear())
              Screen.checked_(Screen.window.setPosition([1, 1]))
              lastTotal = paintNew_(0, true)
              promptRow = Screen.checked_(Screen.window.position)[1]
            } else {
              paintViewport_(rendered, scrollTop, viewportRows)
              promptRow = winRows
            }
            redrawPrompt = true
          }
        } else if (key == Key.up && scrollTop != -1) {
          if (scrollTop > 0) {
            scrollTop = scrollTop - 1
            var rendered = renderAllRows_()
            scrollViewportUp1_(rendered, scrollTop, viewportRows)
          }
          promptRow = winRows
          redrawPrompt = true
        } else if (key == Key.down && scrollTop != -1) {
          var rendered  = renderAllRows_()
          var totalRows = rendered.count
          if (scrollTop + viewportRows >= totalRows - 1) {
            // Bottom row of viewport is already (or would become)
            // the live tail — drop back to live mode.
            scrollTop = -1
            Screen.checked_(Screen.window.clear())
            Screen.checked_(Screen.window.setPosition([1, 1]))
            lastTotal = paintNew_(0, true)
            promptRow = Screen.checked_(Screen.window.position)[1]
          } else {
            scrollTop = scrollTop + 1
            scrollViewportDown1_(rendered, scrollTop, viewportRows)
            promptRow = winRows
          }
          redrawPrompt = true
        } else if (key == Key.up) {
          // First Up in a search anchors the prefix to whatever the
          // user had typed; subsequent Ups walk further back through
          // entries that share that prefix.
          var startIdx = histIdx
          if (histIdx == -1) {
            histAnchor = input
            startIdx = __history.count
          }
          var found = historyFindBackward_(histAnchor, startIdx)
          if (found >= 0) {
            histIdx = found
            input   = __history[found]
            cursor  = input.bytes.count
            redrawPrompt = true
          }
        } else if (key == Key.down) {
          if (histIdx != -1) {
            var found = historyFindForward_(histAnchor, histIdx)
            if (found >= 0) {
              histIdx = found
              input   = __history[found]
            } else {
              // Walked off the newest match — restore the anchor
              // (whatever the user had typed before Up).
              histIdx = -1
              input   = histAnchor
            }
            cursor = input.bytes.count
            redrawPrompt = true
          }
        } else if (key == Key.chord_(108, Mod.control)) {                  // Ctrl+L
          Console.clear()
          Screen.checked_(Screen.window.clear())
          Screen.checked_(Screen.window.setPosition([1, 1]))
          scrollTop = -1
          lastTotal = Console.total
          promptRow = Screen.checked_(Screen.window.position)[1]
          redrawPrompt = true
        } else if (ev.codepoint != null && ev.codepoint >= 0x20 &&
            ev.codepoint != 0x7f &&
            !(ev.codepoint >= 0x80 && ev.codepoint <= 0x9f)) {
          // Typing a printable char rejoins live mode if we're
          // currently in scrollback.
          if (scrollTop != -1) {
            scrollTop = -1
            Screen.checked_(Screen.window.clear())
            Screen.checked_(Screen.window.setPosition([1, 1]))
            lastTotal = paintNew_(0, true)
            promptRow = Screen.checked_(Screen.window.position)[1]
          }
          // input.count and ev.text.count are codepoint counts;
          // cursor / slice indices are byte-based, so use
          // bytes.count for both to keep the byte arithmetic right
          // when the input includes multi-byte UTF-8.
          input  = input[0...cursor] + ev.text +
                   input[cursor...input.bytes.count]
          cursor = cursor + ev.text.bytes.count
          histIdx = -1
          redrawPrompt = true
        }
      }
      if (done) break

      // Drain any new log entries that landed during the keystroke
      // (e.g. System.print echo + eval output from the Enter path).
      // The prompt row gets overwritten as we emit; conio scrolls
      // past the bottom for free.  In scrollback mode the viewport
      // is frozen — leave new entries pending until the user
      // PageDown's back to live.
      if (scrollTop == -1 && Console.total > lastTotal) {
        Screen.checked_(Screen.window.setPosition([1, promptRow]))
        Screen.checked_(Screen.window.clearToLineEnd())
        lastTotal = paintNew_(lastTotal, false)
        promptRow = Screen.checked_(Screen.window.position)[1]
        redrawPrompt = true
      }

      if (redrawPrompt) drawPrompt_(promptRow, currentModule, input, cursor)
    }

  }

  // Dispatches a finished line: a leading "/" makes it a console
  // command; anything else is Wren source for REPL.eval.  Returns the
  // (possibly updated) current module.
  //
  // We wrap the eval in Fiber.new{}.try() so a runtime abort in the
  // user's snippet doesn't tear the console out from under the Hook
  // dispatcher.  Wren's runtime only emits stack frames for *uncaught*
  // aborts, but the failed fiber still has its frames intact — we
  // hand it to REPL.printTrace_ which walks them and pushes them
  // through the host's error callback into the log buffer.
  //
  // .try() can't be used to detect the abort because it returns the
  // block's last-expression value on normal completion (here the
  // result of the `v = ...` assignment, NOT null).  Use fiber.error
  // instead — it stays null unless the fiber actually aborted.
  static handleLine_(line, current) {
    if (line.startsWith("/")) return runCommand_(line, current)
    System.print("(%(current))> " + line)
    // Blank or whitespace-only input: echo for visual feedback (so
    // Enter still advances to a fresh prompt row) but skip the eval
    // — Wren's compiler errors on empty source with "Expected
    // expression" and we don't want that in the log every time the
    // user presses Enter on a clean line.
    if (line.trim().count == 0) return current
    var v = null
    var f = Fiber.new { v = REPL.eval(current, line) }
    f.try()
    // REPL.eval returns null for a statement, [value] for an
    // expression — so v != null means "we got a value, even if v[0]
    // happens to be null itself".
    if (f.error != null) {
      System.print("error: " + f.error)
      REPL.printTrace_(f)
    } else if (v != null) {
      System.print(formatValue_(v[0]))
    }
    return current
  }

  // Render a Wren value for REPL output.  Strings get C-style quoting
  // so control bytes don't dump literal newlines/escapes into the
  // scrollback and so a String result is visibly distinct from a Num
  // (e.g. "7" the string vs 7 the number).  Everything else goes
  // through Wren's default Object.toString.
  static formatValue_(v) {
    if (v == null) return "null"
    if (v is String) return quoteString_(v)
    return "%(v)"
  }

  static quoteString_(s) {
    var hex = "0123456789abcdef"
    var out = "\""
    var i = 0
    while (i < s.bytes.count) {
      var b = s.bytes[i]
      var codepoint = utf8CodepointAt_(s, i)
      var length = utf8Length_(codepoint)
      if (length == 0) {
        out = out + "\\x" + hex[(b >> 4) & 0xf] + hex[b & 0xf]
        i = i + 1
      } else if (b == 0x22) {                // "
        out = out + "\\\""
        i = i + 1
      } else if (b == 0x5C) {                // \
        out = out + "\\\\"
        i = i + 1
      } else if (b == 0x0A) {
        out = out + "\\n"
        i = i + 1
      } else if (b == 0x0D) {
        out = out + "\\r"
        i = i + 1
      } else if (b == 0x09) {
        out = out + "\\t"
        i = i + 1
      } else if (b == 0x00) {
        out = out + "\\0"
        i = i + 1
      } else if (b < 0x20 || b == 0x7F) {
        out = out + "\\x" + hex[(b >> 4) & 0xf] + hex[b & 0xf]
        i = i + 1
      } else {
        out = out + s[i...(i + length)]
        i = i + length
      }
    }
    return out + "\""
  }

  // /in <module>  — switch the current eval target.
  // /in           — show the current target.
  // /quit, /q     — leave the console (same as the active exit keys).
  // /?            — list commands.
  //
  // Sets the static field __quit when the user asks to leave; the main
  // loop checks it alongside the local `done` flag.  Static fields in
  // Wren are double-underscore-prefixed and class-scoped, which is
  // exactly the right shape for "the run loop's exit signal."
  static runCommand_(line, current) {
    if (line == "/?" || line == "/help") {
      System.print("commands: /in [module], /mods, /quit, /?")
      if (__commands != null && __commands.count > 0) {
        var names = __commands.keys.toList
        names.sort {|a, b| stringLT_(a, b) }
        for (n in names) {
          System.print("          /%(n) — %(__commands[n][0])")
        }
      }
      System.print("editing:  Left/Right, Home/End, Backspace, Delete,")
      System.print("          Ctrl+W (kill word back), Ctrl+L (clear screen)")
      System.print("history:  Up/Down — type a prefix first to filter")
      System.print("scroll:   PgUp/PgDn, plus Up/Down inside scrollback")
      if (Screen.checked_(Host.localControlKeys)) {
        System.print("exit:     Esc, Ctrl+Q, Ctrl+S, /quit")
      } else {
        System.print("exit:     Esc, Ctrl+`, /quit")
      }
      return current
    }
    if (line == "/quit" || line == "/q") {
      __quit = true
      return current
    }
    if (line == "/mods") {
      // List.sort() defaults to `a < b`; String doesn't implement `<`,
      // so pass a byte-wise comparator.  Module names are ASCII, so
      // byte order matches lexicographic.
      var mods = REPL.modules
      mods.sort {|a, b| stringLT_(a, b) }
      System.print("modules (%(mods.count)):")
      for (m in mods) System.print("  " + m)
      return current
    }
    if (line == "/in") {
      System.print("current module: " + current)
      return current
    }
    if (line.startsWith("/in ")) {
      var name = line[4...line.bytes.count].trim()
      if (name == "") {
        System.print("usage: /in <module>")
        return current
      }
      if (!REPL.hasModule(name)) {
        System.print("warning: module '%(name)' not loaded; an empty one will be created")
      }
      System.print("now in module: " + name)
      return name
    }
    // Module-registered commands: /<name> [args].  Match the leading
    // word against __commands; anything left of the first space is
    // the command name, everything after is passed to the handler as
    // a single string.  Run inside a Fiber so an abort in the handler
    // surfaces as a logged error instead of tearing out the console.
    if (__commands != null) {
      var sp   = line.indexOf(" ")
      var name = (sp == -1) ? line[1...line.bytes.count] : line[1...sp]
      var args = (sp == -1) ? "" : line[(sp + 1)...line.bytes.count]
      if (__commands.containsKey(name)) {
        var fn = __commands[name][1]
        var f  = Fiber.new { fn.call(args) }
        f.try()
        if (f.error != null) {
          System.print("error: " + f.error)
          REPL.printTrace_(f)
        }
        return current
      }
    }
    System.print("unknown command: " + line)
    return current
  }

  // Emit log entries with seq >= `since` (clamped up to whatever's still
  // present in the ring after eviction).  `firstPaint` is true on the
  // initial paint; controls whether atLineStart starts assumed-true.
  // Returns the new `lastTotal` watermark.
  static paintNew_(since, firstPaint) {
    var totalNow = Console.total
    var oldest   = totalNow - Console.count
    var start    = since.max(oldest)
    var atStart  = firstPaint
    for (s in start...totalNow) {
      var e = Console[s]
      if (e != null) atStart = emitEntry_(e, atStart)
    }
    if (!atStart) put_("\n")
    return totalNow
  }

  // Render every still-buffered log entry into a flat list of
  // display-row Strings (no trailing newline on each row).  The
  // rendering rules mirror emitEntry_:
  //   - Print entries chain on the same row until a "\n" appears
  //     (so System.print's "value" + "\n" pair shows on one row).
  //   - Non-print entries force a row break before themselves if
  //     we're mid-row, and emit "[label ts] text" terminated with
  //     an implicit \n.
  // Used as the source for scrollback paint; also for the row count
  // needed by the PageUp/PageDown math.
  static renderAllRows_() {
    var rows     = []
    var current  = ""
    var totalNow = Console.total
    var oldest   = totalNow - Console.count
    var atStart  = true
    for (s in oldest...totalNow) {
      var e = Console[s]
      if (e == null) continue
      var src  = e[1]
      var ts   = e[0]
      var text = e[2]
      var combined
      if (src == LogSource.print) {
        combined = text
      } else {
        if (!atStart) {
          rows.add(current)
          current = ""
          atStart = true
        }
        var label = "?"
        if (src == LogSource.compileError) label = "compile"
        if (src == LogSource.runtimeError) label = "runtime"
        if (src == LogSource.stackFrame)   label = "trace"
        combined = "[%(label) %(ts)] " + text
      }
      var pieces = combined.split("\n")
      for (i in 0...pieces.count - 1) {
        current = current + pieces[i]
        rows.add(current)
        current = ""
      }
      current = current + pieces[pieces.count - 1]
      atStart = current.count == 0
      if (src != LogSource.print && !atStart) {
        rows.add(current)
        current = ""
        atStart = true
      }
    }
    if (current.count > 0) rows.add(current)
    return rows
  }

  // Paint the scrollback viewport: clear, then dump rendered rows
  // [startRow .. startRow + viewportRows).  startRow is a display-row
  // index into renderAllRows_'s output, so navigation moves in
  // exact-row units.  No trailing newline after the final row —
  // emitting one would push the cursor past the bottom of the
  // screen and trigger a conio scroll, dropping the top row of the
  // viewport we just painted.
  static paintViewport_(rows, startRow, viewportRows) {
    Screen.checked_(Screen.window.clear())
    Screen.checked_(Screen.window.setPosition([1, 1]))
    var endRow = (startRow + viewportRows).min(rows.count)
    for (i in startRow...endRow) {
      if (i > startRow) put_("\n")
      put_(rows[i])
    }
  }

  // Single-row scroll helpers: capture and blit to shift the
  // existing viewport content by one row and only repaint the
  // newly-exposed row.  Avoids the full clear/repaint that
  // paintViewport_ would do for an arrow-key step.

  // Up: viewport shows one row older at the top.  Caller has already
  // decremented scrollTop, so rendered[scrollTop] is the new top row.
  static scrollViewportUp1_(rendered, scrollTop, viewportRows) {
    var w = Screen.checked_(Screen.size)[0]
    if (viewportRows > 1) Screen.checked_(Screen.putRect(Screen.checked_(Screen.readRect(1, 1, w, viewportRows - 1)), 1, 2))
    Screen.checked_(Screen.window.setPosition([1, 1]))
    Screen.checked_(Screen.window.clearToLineEnd())
    if (scrollTop >= 0 && scrollTop < rendered.count) {
      put_(rendered[scrollTop])
    }
  }

  // Down: viewport shows one row newer at the bottom.  Caller has
  // already incremented scrollTop, so the new bottom is at index
  // scrollTop + viewportRows - 1.
  static scrollViewportDown1_(rendered, scrollTop, viewportRows) {
    var w = Screen.checked_(Screen.size)[0]
    if (viewportRows > 1) Screen.checked_(Screen.putRect(Screen.checked_(Screen.readRect(1, 2, w, viewportRows)), 1, 1))
    Screen.checked_(Screen.window.setPosition([1, viewportRows]))
    Screen.checked_(Screen.window.clearToLineEnd())
    var idx = scrollTop + viewportRows - 1
    if (idx >= 0 && idx < rendered.count) {
      put_(rendered[idx])
    }
  }

  static drawPrompt_(row, mod, input, cursor) {
    Screen.checked_(Screen.window.setPosition([1, row]))
    Screen.checked_(Screen.window.clearToLineEnd())
    var prefix = "(%(mod)) wren> "
    Screen.checked_(Screen.window.print(prefix + input))
    // Re-position the cursor by rendered columns, not UTF-8 bytes.  Tab
    // stops are relative to the whole line; Screen.textWidth supplies the
    // selected-font/fallback width for each rendered codepoint.
    Screen.checked_(Screen.window.setPosition([displayColumns_(prefix + input[0...cursor], 0) + 1,
        row]))
  }

  static displayColumns_(s, font) {
    var columns = 0
    var start = 0
    var i = 0
    while (i < s.bytes.count) {
      var codepoint = utf8CodepointAt_(s, i)
      var length = utf8Length_(codepoint)
      if (length == 0) length = 1
      if (codepoint == 0x09) {
        if (i > start) columns = columns + Screen.checked_(Screen.textWidth(s[start...i], font))
        columns = columns + 8 - (columns % 8)
        i = i + length
        start = i
      } else {
        i = i + length
      }
    }
    if (i > start) columns = columns + Screen.checked_(Screen.textWidth(s[start...i], font))
    return columns
  }

  // conio's cputs treats LF as line-feed-only (cursor down, same column).
  // The console expects \n to start a new line at column 1, so translate
  // every \n in `s` to \r\n on the way out.
  //
  // String.count is *codepoint* count (inherited from Sequence's
  // iteration), but String[i] and String[a...b] are *byte* indexed.
  // Mixing the two truncates multi-byte content by (bytes - codepoints)
  // at the tail.  Use s.bytes.count for the byte-iteration bounds.
  static put_(s) {
    var n = s.bytes.count
    var start = 0
    var i = 0
    while (i < n) {
      if (s[i] == "\n") {
        if (i > start) Screen.checked_(Screen.window.print(s[start...i]))
        Screen.checked_(Screen.window.print("\r\n"))
        start = i + 1
      }
      i = i + 1
    }
    if (start < n) Screen.checked_(Screen.window.print(s[start...n]))
  }

  // Emit one log entry, returning whether the cursor is now at column 1.
  static emitEntry_(e, atLineStart) {
    var ts     = e[0]
    var src    = e[1]
    var text   = e[2]
    var newAtStart = atLineStart

    // text[i] is byte-indexed; use bytes.count - 1 to read the last
    // byte rather than text.count - 1 (which is codepoints and lands
    // mid-multibyte for non-ASCII content).
    var bn = text.bytes.count
    if (src == LogSource.print) {
      put_(text)
      if (bn > 0) {
        var last = text[bn - 1]
        newAtStart = (last == "\n")
      }
    } else {
      if (!atLineStart) put_("\n")
      var label = "?"
      if (src == LogSource.compileError) label = "compile"
      if (src == LogSource.runtimeError) label = "runtime"
      if (src == LogSource.stackFrame)   label = "trace"
      put_("[%(label) %(ts)] ")
      put_(text)
      newAtStart = true
      if (bn > 0 && text[bn - 1] != "\n") put_("\n")
    }
    return newAtStart
  }

}
