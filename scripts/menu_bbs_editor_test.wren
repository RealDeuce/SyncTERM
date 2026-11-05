// Menu-VM self-tests for per-entry Wren script configuration.

import "syncterm" for Key, KeyEvent, TLSVersion
import "syncterm_menu" for Menu
import "menu_bbs_editor" for BbsEditor, EditorPane, ScriptListPane

class FakeApp {
  construct new() { _popped = 0 }
  popModal() { _popped = _popped + 1 }
  popped { _popped }
}

class MenuTest {
  // Read-only subset for the isolated audit host, which has no list paths.
  static runHelpers() {
    __pass = 0
    __fail = 0
    testMoveCommit_()
    testMoveCancel_()
    testDelete_()
    testEditorClear_()
    testDiscovery_()
    testSkyPixSettings_()
    return [__pass, __fail]
  }

  static run() {
    __pass = 0
    __fail = 0
    System.print("=== menu_bbs_editor self-test starting ===")

    testMoveCommit_()
    testMoveCancel_()
    testDelete_()
    testEditorClear_()
    testDiscovery_()
    testSkyPixSettings_()
    testDefaultsRejectScripts_()
    testEditorScripts_()
    testPersistence_()

    var total = __pass + __fail
    System.print("=== menu_bbs_editor: %(total) tests, %(__pass) pass, " +
        "%(__fail) fail ===")
    return [__pass, __fail]
  }

  static check_(ok, label) {
    if (ok) {
      __pass = __pass + 1
    } else {
      __fail = __fail + 1
      System.print("  FAIL %(label)")
    }
  }

  static same_(left, right) {
    if (left.count != right.count) return false
    for (i in 0...left.count) {
      if (left[i] != right[i]) return false
    }
    return true
  }

  static pane_(scripts, changes) {
    return ScriptListPane.new(FakeApp.new(), scripts,
        Fn.new { changes[0] = changes[0] + 1 })
  }

  static testSkyPixSettings_() {
    for (variant in [3, 4]) {
      var d = {"graphics": variant, "screenMode": 0, "encoding": 0, "amigaFont": "Ruby.font", "amigaFontSize": 12}
      d["graphics"] = variant
      BbsEditor.changeScreen_(d, 3)
      BbsEditor.changeEncoding_(d, 255)
      check_(d["graphics"] == variant && d["screenMode"] == 3 && d["encoding"] == 255,
          "SkyPix screen and encoding remain configurable")
      check_(d["amigaFont"] == "Ruby.font" && d["amigaFontSize"] == 12,
          "SkyPix font survives independent screen and encoding edits")
    }
  }

  static testMoveCommit_() {
    var scripts = ["alpha", "beta", "gamma"]
    var changes = [0]
    var pane = pane_(scripts, changes)
    pane.selected = 1
    pane.handle(KeyEvent.new(Key.enter))
    var tagged = pane.moving && pane.tagged.count == 1 &&
        pane.tagged[0] == 1
    pane.handle(KeyEvent.new(Key.down))
    pane.handle(KeyEvent.new(Key.enter))
    check_(tagged && !pane.moving &&
           same_(scripts, ["alpha", "gamma", "beta"]) &&
           changes[0] == 1,
           "ScriptListPane: Enter tags and arrows move in stored order")
  }

  static testMoveCancel_() {
    var scripts = ["alpha", "beta", "gamma"]
    var changes = [0]
    var pane = pane_(scripts, changes)
    pane.selected = 1
    pane.handle(KeyEvent.new(Key.enter))
    pane.handle(KeyEvent.new(Key.up))
    pane.handle(KeyEvent.new(Key.escape))
    check_(!pane.moving && pane.selected == 1 &&
           same_(scripts, ["alpha", "beta", "gamma"]) && changes[0] == 0,
           "ScriptListPane: Escape restores the pre-move order")
  }

  static testDelete_() {
    var scripts = ["alpha", "beta"]
    var changes = [0]
    var pane = pane_(scripts, changes)
    var oldHeight = pane.bounds.h
    pane.selected = 0
    pane.handle(KeyEvent.new(Key.delete))
    check_(same_(scripts, ["beta"]) && changes[0] == 1 &&
           pane.bounds.h == oldHeight - 1,
           "ScriptListPane: Delete removes the script and refits the pane")
  }

  static testEditorClear_() {
    var clears = [0]
    var pane = EditorPane.new()
    pane.onClear = Fn.new { clears[0] = clears[0] + 1 }
    var handled = pane.handle(KeyEvent.new(Key.delete))
    check_(handled && clears[0] == 1,
           "EditorPane: Delete invokes the selected row's clear action")
  }

  static testDiscovery_() {
    var modules = Menu.scriptModules
    modules.add("__catalog_mutation_test")
    var fresh = Menu.scriptModules
    check_(fresh.indexOf("__catalog_mutation_test") < 0 &&
           fresh.indexOf("syncterm_menu") < 0 &&
           fresh.indexOf("syncterm_picker") < 0 &&
           fresh.indexOf("picker_bootstrap") < 0 &&
           Menu.wrenScriptsMaxLength == 1023,
           "Menu.scriptModules: returns a fresh catalog without reserved VMs")
  }

  static testDefaultsRejectScripts_() {
    var loaded = Menu.load(null) == null
    var defaults = loaded ? Menu.defaults : null
    var fiber = Fiber.new {
      defaults.wrenScripts = ["user_script"]
    }
    if (defaults != null) fiber.try()
    check_(defaults != null && fiber.error != null &&
           defaults.wrenScripts.count == 0,
           "Profile.wrenScripts: defaults reject per-entry scripts")
  }

  static testEditorScripts_() {
    if (Menu.load(null) != null) {
      check_(false, "per-entry scripts: load")
      return
    }
    var app = FakeApp.new()
    var defaults = Menu.defaults
    var draft = BbsEditor.draft_(defaults)
    var rows = BbsEditor.rows_(app, defaults, draft, true)
    var scriptRows = rows.where {|row| row[0].startsWith("Wren Scripts") }.toList
    check_(scriptRows.count == 0, "defaults editor: scripts are omitted")

    var entry = Menu.create("Empty Scripts Test")
    var profile = entry.profile
    draft = BbsEditor.draft_(entry)
    rows = BbsEditor.rows_(app, entry, draft, false)
    scriptRows = rows.where {|row| row[0].startsWith("Wren Scripts") }.toList
    check_(profile.wrenScripts.count == 0 && scriptRows.count == 1 &&
           scriptRows[0][0].endsWith("None"),
           "entry editor: an empty script list is shown as None")
    draft["comment"] = "edited without adding scripts"
    check_(BbsEditor.update_(app, entry, draft, false) &&
           profile.wrenScripts.count == 0 && (entry.save() == null),
           "entry editor: other edits keep scripts empty")
    check_((entry.delete() == null), "entry editor: remove test entry")
  }

  static testPersistence_() {
    var loaded = Menu.load(null) == null
    var entry = loaded ? Menu.create("Wren Script Test") : null
    var created = entry != null
    var assigned = false
    var saved = false
    if (created) {
      entry.profile.wrenScripts = ["ui_list", "menu_bbs_editor", "ui_list"]
      entry.profile.tlsTrustWebPki = false
      entry.profile.tlsTrustedCert = "/tls/bbs.pem"
      entry.profile.tlsClientCert = "/tls/client.pem"
      entry.profile.tlsClientKey = "/tls/client.key"
      entry.profile.tlsPskIdentity = "test identity"
      entry.profile.tlsPsk = "test shared secret"
      entry.profile.tlsPskVersion = TLSVersion.tls12
      entry.profile.tlsVersionFloor = TLSVersion.tls13
      assigned = same_(entry.profile.wrenScripts, ["ui_list", "menu_bbs_editor"]) &&
          entry.profile.tlsTrustedCert == "/tls/bbs.pem" &&
          entry.profile.tlsClientCert == "/tls/client.pem" &&
          entry.profile.tlsClientKey == "/tls/client.key" &&
          entry.profile.tlsPskIdentity == "test identity" &&
          entry.profile.tlsPsk == "test shared secret" &&
          entry.profile.tlsPskVersion == TLSVersion.tls12 &&
          entry.profile.tlsVersionFloor == TLSVersion.tls13
      saved = (entry.save() == null)
    }

    var reloaded = created && assigned && saved &&
        Menu.load(null) == null
    var found = null
    if (reloaded) {
      for (candidate in Menu.entries) {
        if (candidate.name == "Wren Script Test") found = candidate
      }
    }
    var retained = found != null && same_(found.profile.wrenScripts,
        ["ui_list", "menu_bbs_editor"]) && !found.profile.tlsTrustWebPki &&
        found.profile.tlsTrustedCert == "/tls/bbs.pem" &&
        found.profile.tlsClientCert == "/tls/client.pem" &&
        found.profile.tlsClientKey == "/tls/client.key" &&
        found.profile.tlsPskIdentity == "test identity" &&
        found.profile.tlsPsk == "test shared secret" &&
        found.profile.tlsPskVersion == TLSVersion.tls12 &&
        found.profile.tlsVersionFloor == TLSVersion.tls13
    if (!(loaded && created && assigned && saved && reloaded && retained)) {
      System.print("  persistence state: loaded=%(loaded), " +
          "created=%(created), assigned=%(assigned), saved=%(saved), " +
          "reloaded=%(reloaded), found=%(found != null), " +
          "scripts=%(found == null ? [] : found.profile.wrenScripts)")
    }
    if (found != null) (found.delete() == null)
    check_(loaded && created && assigned && saved && reloaded && retained,
           "BBS TLS and Wren settings survive save and reload")
  }
}
