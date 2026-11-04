// Reserved picker-VM recovery surface.  The host interprets the embedded
// copy before loading user picker scripts so an implementation error still
// leaves the picker-local console reachable.

import "syncterm" for Host, Input, Key, KeyEvent, REPL, Screen, Mod, PhysicalKeyEvent
import "wren_console" for WrenConsole

class PickerBootstrap {
  static console() { WrenConsole.run("file_picker") }

  static run(picker, request) {
    var fiber = Fiber.new { picker.run(request) }
    fiber.try()
    if (fiber.error == null) return
    System.print("File picker failed: %(fiber.error)")
    REPL.printTrace_(fiber)
    recover(request, fiber.error)
  }

  // Returns true only for the application-close key.  Escape cancels the
  // failed picker call while keeping SyncTERM alive.
  static recover(request, message) {
    var oldBounds = Screen.checked_(Screen.window.bounds)
    var size = Screen.checked_(Screen.size)
    Screen.checked_(Screen.window.setBounds([1, 1, size[0], size[1]]))
    Screen.checked_(Screen.window.clear())
    Screen.checked_(Screen.window.setPosition([3, 2]))
    Screen.checked_(Screen.window.print("File picker error"))
    Screen.checked_(Screen.window.setPosition([3, 4]))
    Screen.checked_(Screen.window.print(message))
    Screen.checked_(Screen.window.setPosition([3, 6]))
    Screen.checked_(Screen.window.print("Ctrl+` opens the picker console.  Escape cancels."))

    while (true) {
      if (Host.logUnread) {
        Screen.checked_(Screen.window.setPosition([size[0], 1]))
        Screen.checked_(Screen.window.print("\u203c"))
      }
      var event = Input.next()
      if (!(event is KeyEvent) && !(event is PhysicalKeyEvent)) continue
      var key = event is KeyEvent ? event.shortcut_ : Key.physicalShortcut_(event)
      if (key == Key.chord_(96, Mod.control)) {
        console()
        Screen.checked_(Screen.window.clear())
        Screen.checked_(Screen.window.setPosition([3, 2]))
        Screen.checked_(Screen.window.print("File picker error"))
        Screen.checked_(Screen.window.setPosition([3, 4]))
        Screen.checked_(Screen.window.print(message))
        Screen.checked_(Screen.window.setPosition([3, 6]))
        Screen.checked_(Screen.window.print("Ctrl+` opens the picker console.  Escape cancels."))
      } else if (key == Key.escape) {
        request.cancel()
        Screen.checked_(Screen.window.setBounds(oldBounds))
        return
      } else if (key == Key.quit) {
        request.quitApplication()
        Screen.checked_(Screen.window.setBounds(oldBounds))
        return
      }
    }
  }
}
