// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for Capture, CTerm, FileError, FilePickerOptions, Host, Input, Screen
import "ui_app" for App
import "ui_picker" for ListPicker
import "ui_popup" for Alert

class CaptureMenu {
  static run() {
    Fiber.new {
      var picked = -1
      var active = Capture.active
      var paused = Capture.paused
      var options
      if (active) {
        options = [paused ? "Resume Capture" : "Pause Capture", "Stop Capture"]
      } else {
        options = ["Capture Plain Text", "Capture with ANSI",
            "Save Screen Binary", "Save Screen Binary with SAUCE"]
      }
      Screen.checked_(Screen.modalRun(Fn.new {
        picked = ListPicker.pick("Capture Control",
            "# Capture Control\n\nSave the remote display stream to a local file.",
            options)
      }))
      if (picked < 0) return
      if (active) {
        if (picked == 0) {
          if (paused) {
            Capture.resume()
          } else {
            Capture.pause()
          }
        } else if (picked == 1) {
          Capture.stop()
        }
        return
      }
      var mask = picked >= 2 ? "*.bin" : "*"
      var saveOptions = picked >= 2 ? FilePickerOptions.none : FilePickerOptions.allowAppend
      var file = Host.pickSavePath(null, mask, saveOptions)
      if (file == null) return
      var result = null
      if (picked >= 2) {
        result = CTerm.saveScreenshot(file, picked == 3)
      } else {
        result = Capture.start(file, picked == 1)
      }
      if (result is FileError) {
        Screen.checked_(Screen.modalRun(Fn.new {
          Alert.runStandalone(App.new(), "Capture Control", result.toString)
        }))
      }
    }.call()
  }
}
