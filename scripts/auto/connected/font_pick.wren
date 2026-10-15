// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

import "syncterm" for CTerm, FilePickerOptions, Font, Host, Input, Screen, ScreenSupports
import "ui_picker" for ListPicker
import "ui_app" for App
import "ui_popup" for Alert

class FontApp {
  static run() {
    Fiber.new {
      if (Host.safeMode) return
      Screen.modalRun(Fn.new {
        if (!ScreenSupports.fontSelection) {
          Alert.runStandalone(App.new(),
              "Font cannot be changed in the current video output mode.")
          return
        }
        var labels = []
        var selectors = []
        for (selector in 0...Font.count) {
          if (Font.available(selector)) {
            var name = Font.name(selector)
            if (name != null && name != "") {
              labels.add(name)
              selectors.add(selector)
            }
          }
        }
        var current = selectors.indexOf(CTerm.altFont)
        labels.add("Load font from file…")
        var picked = ListPicker.pickWithSelection("Font Setup",
            "# Font Setup\n\nSelect a font for new terminal output, " +
            "or load a 256-glyph font file.",
            labels, current < 0 ? 0 : current)
        if (picked == selectors.count) {
          var file = Host.pickFile(null, "*", FilePickerOptions.none)
          if (file != null && !Font.load(file)) {
            Alert.runStandalone(App.new(), "Unable to load the font file.")
          }
        } else if (picked >= 0) {
          CTerm.altFont = selectors[picked]
        }
      })
      Input.setupMouseEvents()
    }.call()
  }
}
