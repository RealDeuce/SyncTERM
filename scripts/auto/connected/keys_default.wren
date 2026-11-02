// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Hook, Key, Mouse, Host, CTerm, Input, Conn, Emoji, Mod
import "disconnect_flow" for DisconnectFlow
import "scrollback_view" for ScrollbackView
import "music_menu" for MusicMenu
import "capture_menu" for CaptureMenu
import "font_pick" for FontApp
import "session_log" for SessionLogView
import "emoji_pick" for EmojiFlow

Hook.onCommand_(112) {|key|
  if (!Emoji.canSend) return false
  Fiber.new { EmojiFlow.run() }.call()
  return true
}

Hook.onCommand_(118) { |key|
  SessionLogView.run()
  return true
}

Hook.onCommand_(120) { |key|
  DisconnectFlow.run(true)
  return true
}

Hook.onCommand_(98) { |key|
  ScrollbackView.run()
  return true
}

Hook.onCommand_(109) { |key|
  MusicMenu.run()
  return true
}

Hook.onCommand_(99) { |key|
  CaptureMenu.run()
  return true
}

Hook.onCommand_(101) { |key|
  Fiber.new { Host.editBBSList() }.call()
  return true
}

Hook.onCommand_(102) { |key|
  FontApp.run()
  return true
}

Hook.onCommand_(111) { |key|
  CTerm.mouseDisabled = !CTerm.mouseDisabled
  return true
}

Hook.onKey(Key.up, Mod.command) { |key|
  CTerm.throttleSpeedUp()
  return true
}

Hook.onKey(Key.down, Mod.command) { |key|
  CTerm.throttleSpeedDown()
  return true
}

Hook.onKey(Key.insert, Mod.shift) { |key|
  Conn.paste()
  return true
}

Hook.onMouse(Mouse.button1DragStart) {|event|
  var mode = CTerm.mouseMode
  if (CTerm.mouseDisabled || mode == 0 || mode == 9) {
    Input.mousedrag()
    return true
  }
  return false
}

var pasteOnClick = Fn.new {|event|
  if (CTerm.mouseDisabled || CTerm.mouseMode == 0) {
    Conn.paste()
    return true
  }
  return false
}
Hook.onMouse(Mouse.button2Click, pasteOnClick)
Hook.onMouse(Mouse.button3Click, pasteOnClick)

Hook.onCommand_(104) { |key|
  DisconnectFlow.run(false)
  return true
}
if (Host.localControlKeys) {
  Hook.onKey(113, Mod.control) { |key|
    DisconnectFlow.run(false)
    return true
  }
}
