// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Transfer, Screen, Key, KeyEvent, Timer, Cleanup_, Mod, IOErr
import "ui_app" for App
import "ui_pane" for Pane
import "ui_widget" for Rect
import "ui_progress" for ProgressBar, ProgressText
import "ui_logview" for LogView
import "menu_ui" for MenuUi

class TickStateView {
  construct new(value) { _value = value }
  line1 { _value[0] }
  line2 { _value[1] }
  line3 { _value[2] }
  line4 { _value[3] }
  bytesCur { _value[4] }
  bytesTotal { _value[5] }
}

class TransferApp is App {
  construct new(label) {
    super()
    _label = label
    _waitTimeoutMs = 5000
    _deadline = null
    _dialogActive = false
    _success = false
    _aborted = false
    tickMs = 50
    bind(Key.escape, Fn.new {|key| abort_() })
    bind(99, Mod.control, Fn.new {|key| abort_() })
    bind(120, Mod.control, Fn.new {|key| abort_() })
  }
  success { _success }
  aborted { _aborted }
  waitTimeoutMs { _waitTimeoutMs }
  waitTimeoutMs=(value) { _waitTimeoutMs = value }
  abort_() {
    if (Transfer.done) { quit() } else { Transfer.requestAbort() }
  }
  dispatchKey_(event) {
    if (_deadline != null) {
      quit()
      return true
    }
    return super.dispatchKey_(event)
  }
  build_() {
    var size = Screen.checked_(Screen.size)
    var pane = Pane.new()
    pane.title = _label
    pane.bounds = Pane.modalBounds(66.min(size[0]), 18.min(size[1]))
    pane.focused = true
    pane.onClose = Fn.new { abort_() }
    pane.keyHints = [["Esc", "Abort"], ["Ctrl-C", "Abort"]]
    var inner = pane.innerBounds
    _text = ProgressText.new()
    _text.bounds = Rect.new(inner.x, inner.y, inner.w, inner.h.min(4))
    pane.add(_text)
    _progress = ProgressBar.new()
    _progress.bounds = Rect.new(inner.x, inner.y + 4, inner.w, 1)
    if (inner.h > 4) pane.add(_progress)
    _log = LogView.new()
    _log.bounds = Rect.new(inner.x, inner.y + 6, inner.w, (inner.h - 6).max(1))
    if (inner.h > 6) pane.add(_log)
    pushModal(pane)
  }
  onTick_() {
    for (entry in Transfer.drainLog()) _log.append(entry[0], entry[1])
    if (Transfer.tickDirty) {
      var snap = TickStateView.new(Transfer.snapshot())
      _text.lines = [snap.line1, snap.line2, snap.line3, snap.line4]
      _progress.set(snap.bytesCur, snap.bytesTotal)
    }
    if (!_dialogActive && Transfer.dialogPending != 0) {
      _dialogActive = true
      Cleanup_.run(Fn.new { dialog_(Transfer.dialogPending) },
          Fn.new { _dialogActive = false })
    }
    if (Transfer.done && _deadline == null) {
      var result = Transfer.result
      _success = result == null
      _aborted = Transfer.aborted
      var message = "Transfer complete"
      if (result != null) message = result.code == IOErr.cancelled ? "Transfer cancelled" : result.message
      _log.append(_success ? 6 : 3, message)
      _deadline = Timer.now * 1000 + _waitTimeoutMs
    }
    if (_deadline != null && Timer.now * 1000 >= _deadline) quit()
  }
  dialog_(kind) {
    var filename = Transfer.dialogFilename
    if (kind == 1) {
      var choice = MenuUi.choice(this, "File Already Exists", [
        [0, "Overwrite"], [1, "Skip"], [2, "Rename"]
      ], 1, "# Existing file\n\n%(filename)")
      var name = ""
      if (choice == 2) {
        name = MenuUi.promptBytes(this, "New Filename", "Filename", "", 1023, false, null)
        if (name == null || name == "") choice = 1
      }
      Transfer.dialogRespond(choice == null ? 1 : choice, name)
    } else if (kind == 2) {
      var name = MenuUi.promptBytes(this, filename, "Filename", "", 1023, false, null)
      Transfer.dialogRespond(name == null || name == "" ? 1 : 0, name)
    } else {
      Transfer.dialogRespond(1, "")
    }
  }
  run() {
    Screen.checked_(Screen.modalRun(Fn.new {
      build_()
      super.run()
    }))
  }
}
