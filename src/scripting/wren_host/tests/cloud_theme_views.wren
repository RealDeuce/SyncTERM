// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, IOErr, Key, KeyEvent
import "syncterm_menu" for Menu, MenuError, CloudThemeEntry
import "menu_settings_ui" for SettingsMenu
import "ui_app" for App
import "ui_popup" for Alert
import "ui_list" for ListView

class CloudErrorApp is App {
  construct new() {
    super()
    _alerts = 0
  }
  alerts { _alerts }
  modal(pane) {
    if (!(pane is Alert)) Fiber.abort("metadata error opened an action dialog")
    pushModal(pane)
    _alerts = _alerts + 1
    pane.dismissWith_(null)
  }
}

// Exercise the real Themes browser before any online refresh.
class CloudStartupApp is App {
  construct new(accept) {
    super()
    _accept = accept
    _seen = false
  }
  seen { _seen }
  modal(pane) {
    if (pane.title != "Themes") Fiber.abort("unexpected startup theme dialog")
    pushModal(pane)
    var list = null
    for (child in pane.children) if (child is ListView) list = child
    if (list == null || list.selected == null ||
        !list.items[list.selected].contains("Cloud Theme")) {
      Fiber.abort("cached selected theme missing or not highlighted after restart")
    }
    _seen = true
    if (_accept) {
      list.handle(KeyEvent.new(Key.enter))
    } else {
      pane.handle(KeyEvent.new(Key.escape))
    }
  }
}

class CloudThemeTest {
  static check_(ok, message) {
    if (!ok) Fiber.abort(message)
  }
  static named_(package) {
    for (entry in Menu.cloudThemes) if (entry.package == package) return entry
    Fiber.abort("Missing cloud package: %(package)")
  }
  static stale_(action) {
    check_(Fiber.new { action.call() }.try() is String, "stale cloud view accepted")
  }
  static startup_(verifyHash) {
    var entry = named_("official/blue")
    check_(entry.cached, "cold-start cache state")
    var before = Menu.selectedThemePackage
    var app = CloudStartupApp.new(false)
    SettingsMenu.themes_(app, null)
    check_(app.seen && Menu.selectedThemePackage == before,
        "opening/cancelling Themes changed the selected package")
    if (verifyHash) {
      app = CloudStartupApp.new(true)
      SettingsMenu.themes_(app, null)
      check_(app.seen && Menu.selectedThemePackage == before,
          "selected cached theme cannot be retained with Enter")
    }
  }
  static run(verifyHash) {
    __blue = named_("official/blue")
    check_(__blue is CloudThemeEntry && __blue.name == "Cloud Theme" &&
        __blue.author == "Jos\u00e9" && __blue.description == "Blue skies" &&
        __blue.version == "1" && __blue.sourceUri == "https://example.invalid/blue.ini" &&
        __blue.expectedSha256 == "1891D225F61EF1AAA8DF82E47F540561A4829FB5CC62535A34B1454F9BB0BFD5" &&
        __blue.cached && __blue.updateAvailable == !verifyHash && __blue.error == null,
        "typed cloud metadata")
    for (change in [Fn.new { __blue.package = "other" }, Fn.new { __blue.name = "other" },
        Fn.new { __blue.author = null }, Fn.new { __blue.description = null },
        Fn.new { __blue.version = null }, Fn.new { __blue.sourceUri = null },
        Fn.new { __blue.expectedSha256 = null }, Fn.new { __blue.cached = false },
        Fn.new { __blue.updateAvailable = false }, Fn.new { __blue.error = null },
        Fn.new { __blue[0] }, Fn.new { CloudThemeEntry.new() }]) stale_(change)
    __name = __blue.name
    __uri = __blue.sourceUri
    __hash = __blue.expectedSha256
    if (verifyHash) {
      var generation = Host.themeGeneration
      check_(Menu.previewCloudTheme(__blue.package) == null &&
          Host.themeGeneration > generation && __blue.cached, "preview invalidated catalog view")
      Menu.cancelThemePreview()
    }
    check_(__blue.name == __name, "cancel invalidated catalog view")
  }
  static blue { __blue }
  static retained_() {
    check_(__blue.package == "official/blue" && __blue.name == __name,
        "failed refresh invalidated view")
  }
  static replaced_() {
    for (read in [Fn.new { __blue.package }, Fn.new { __blue.name },
        Fn.new { __blue.sourceUri }, Fn.new { __blue.expectedSha256 },
        Fn.new { __blue.cached }, Fn.new { __blue.error }]) stale_(read)
    check_(__name == "Cloud Theme" && __uri == "https://example.invalid/blue.ini" &&
        __hash.startsWith("1891D225"), "captured cloud strings changed")
    __blue = named_("official/blue")
    System.gc()
  }
}
