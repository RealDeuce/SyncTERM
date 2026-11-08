// Drive shipped settings actions without a physical display or keyboard.
import "syncterm" for Key, KeyEvent, AddressFamily
import "syncterm_menu" for Menu
import "menu_settings_ui" for SettingsMenu
import "menu_theme_editor" for ThemeEditor
import "menu_bbs_editor" for BbsEditor
import "menu_sort_profiles" for SortProfiles
import "menu_ui" for MenuUi
import "ui_app" for App
import "ui_list" for ListView
import "ui_popup" for Alert

class SettingsTestApp is App {
  construct new(actions) {
    super()
    _actions = actions
    _alerts = 0
  }
  alerts { _alerts }
  remaining { _actions.count }
  modal(pane) {
    pushModal(pane)
    if (pane is Alert) {
      _alerts = _alerts + 1
      pane.dismissWith_(null)
      return
    }
    if (_actions.count == 0) Fiber.abort("Unexpected dialog: %(pane.title)")
    _actions.removeAt(0).call(pane)
    if (modalStack.indexOf(pane) >= 0) popModal()
  }
}

class SettingsMenuTest {
  static check_(ok, message) {
    if (!ok) Fiber.abort(message)
  }
  static list_(pane) {
    for (child in pane.children) if (child is ListView) return child
    Fiber.abort("No choice list in %(pane.title)")
  }
  static select_(pane, label) {
    var list = list_(pane)
    for (i in 0...list.items.count) {
      if (list.items[i].startsWith(label)) {
        list.selected = i
        list.handle(KeyEvent.new(Key.enter))
        return
      }
    }
    Fiber.abort("Missing row: %(label) in %(pane.title)")
  }
  static row_(label) { Fn.new {|pane| select_(pane, label) } }
  static answer_(value) { Fn.new {|pane| pane.dismissWith_(value) } }
  static cancel_ { Fn.new {|pane| null } }
  static run() {
    testProgram_()
    testConnectedMenu_()
    testAddressFamily_()
    testDefaults_()
    testCTermLog_()
    testTlsPairs_()
    testFileLocations_()
    testLicenses_()
    testInputLimits_()
    testLongFontName_()
    testInvalidNames_()
    testEncryption_()
    testThemeFilenames_()
  }
  static testProgram_() {
    var before = Menu.settings
    var colorEmoji = before.colorEmoji
    var confirm = before.confirmClose
    var promptSave = before.promptSave
    before.tlsClientCert = "client.pem"
    before.tlsClientKey = "client.key"
    check_((before.apply() == null), "prepare TLS file fields")
    var actions = [
      row_("Confirm Program Exit"), row_("Prompt to Save"),
      row_("Startup Screen Mode"), row_(Menu.screenModes[3]),
      row_("Video Output Mode"), row_(Menu.outputModes[0][1]),
      row_("Default Cursor Style"), row_(Menu.cursorStyles[4])
    ]
    var audioBefore = Menu.settings.audioModes
    var audioBit = 0
    if (Menu.audioModes.count > 0) {
      audioBit = Menu.audioModes[0][0]
      actions.add(row_("Audio Output Mode"))
      var mark = (audioBefore & audioBit) != 0 ? "[X] " : "[ ] "
      actions.add(row_(mark + Menu.audioModes[0][1]))
      actions.add(cancel_)
    }
    var midiBefore = Menu.settings.midiModes
    var midiBit = 0
    if (Menu.midiModes.count > 0) {
      midiBit = Menu.midiModes[0][0]
      actions.add(row_("MIDI"))
      var mark = (midiBefore & midiBit) != 0 ? "[X] " : "[ ] "
      actions.add(row_(mark + Menu.midiModes[0][1]))
      actions.add(cancel_)
    }
    for (edit in [
      ["Scrollback Buffer Lines", 123],
      ["Modem/Comm Device", "/dev/audit"],
      ["Modem/Comm Rate", 9600],
      ["Modem Init String", "ATZ"],
      ["Modem Dial String", "ATDP"],
      ["List Path", before.listPath],
      ["TERM For Shell", "ansi"],
      ["Key Derivation Shift", 8]
    ]) {
      actions.add(row_(edit[0]))
      actions.add(answer_(edit[1].toString))
    }
    actions.add(row_("Scaling"))
    actions.add(row_("Invert Mouse Wheel"))
    actions.add(row_("Colour Emoji"))
    actions.add(row_("Classic Theme Colours"))
    for (edit in [["Frame Colour", "Blue"], ["Text Colour", "Green"],
        ["Background Colour", "Cyan"], ["Inverse Colour", "Red"],
        ["Lightbar Colour", "Yellow"], ["Lightbar Background", "Magenta"]]) {
      actions.add(row_(edit[0]))
      actions.add(row_(edit[1]))
    }
    actions.add(cancel_)
    actions.add(row_("Custom Screen Mode"))
    for (edit in [["Rows", 30], ["Columns", 100]]) {
      actions.add(row_(edit[0]))
      actions.add(answer_(edit[1].toString))
    }
    actions.add(row_("Font Size"))
    actions.add(row_("8x8"))
    for (edit in [["Aspect Ratio Width", 16], ["Aspect Ratio Height", 9]]) {
      actions.add(row_(edit[0]))
      actions.add(answer_(edit[1].toString))
    }
    actions.add(cancel_)
    for (label in ["TLS Client Certificate", "TLS Client Key"]) {
      actions.add(Fn.new {|pane|
        var list = list_(pane)
        for (i in 0...list.items.count) {
          if (list.items[i].startsWith(label)) list.selected = i
        }
        pane.handle(KeyEvent.new(Key.delete))
      })
    }
    actions.add(cancel_)
    var app = SettingsTestApp.new(actions)
    check_(SettingsMenu.program_(app, false), "program editor reports changes")
    check_(app.remaining == 0 && app.alerts == 0, "all program controls accepted")
    var s = Menu.settings
    check_(s.colorEmoji == !colorEmoji, "colour emoji setting applied")
    check_(s.confirmClose == !confirm && s.promptSave == !promptSave &&
        s.startupMode == 3 && s.outputMode == 0 && s.cursorStyle == 4,
        "program toggles and display choices")
    check_(s.midiModes == (midiBefore ^ midiBit), "MIDI library mask edit")
    check_(s.audioModes == (audioBefore ^ audioBit) &&
        s.scrollbackLines == 123 && s.modemDevice == "/dev/audit" &&
        s.modemRate == 9600 && s.modemInit == "ATZ" &&
        s.modemDial == "ATDP" && s.shellTerm == "ansi",
        "audio, scrollback, modem and shell edits")
    check_(s.scalingMode == 1 && s.invertWheel && s.kdfShift == 8,
        "scaling, wheel direction and KDF edits")
    check_(s.frameColor == 1 && s.textColor == 2 &&
        s.backgroundColor == 3 && s.inverseColor == 4 &&
        s.lightbarColor == 14 && s.lightbarBackgroundColor == 5,
        "all Classic Theme colours")
    check_(s.customRows == 30 && s.customColumns == 100 &&
        s.customFontHeight == 8 && s.customAspectWidth == 16 &&
        s.customAspectHeight == 9 && s.tlsClientCert == "" &&
        s.tlsClientKey == "", "custom geometry and clearing TLS files")
  }
  static testDefaults_() {
    var b = Menu.defaults
    var d = BbsEditor.draft_(b)
    var changes = {
      "connType": 12, "port": 992, "user": "guest", "password": "pass",
      "syspass": "system", "addressFamily": 3, "screenMode": 3,
      "encoding": 255, "termName": "ansi", "music": 0, "bpsRate": 9600,
      "noStatus": true, "hidePopups": true, "yellowIsYellow": true,
      "forceLcf": true, "lfExpand": true, "dlDir": "/downloads",
      "ulDir": "/uploads", "logFile": "audit.log", "appendLogFile": false,
      "xferLogLevel": 6, "protocolLogLevel": 7, "ctermLogLevel": 5, "dataBits": 7,
      "stopBits": 2, "parity": 2, "flowControl": 2,
      "telnetNoBinary": true, "deferTelnetNegotiation": true,
      "tlsTrustWebPki": false, "tlsTrustedCert": "server.pem",
      "tlsClientCert": "client.pem", "tlsClientKey": "client.key",
      "tlsPskIdentity": "identity", "tlsPsk": "secret", "tlsPskVersion": 1,
      "sftpPublicKey": true, "sshAllowAes128Cbc": true,
      "sshAcceptEarlyData": true
    }
    for (key in changes.keys) d[key] = changes[key]
    var app = SettingsTestApp.new([])
    check_(BbsEditor.update_(app, b, d, true) && (Menu.saveDefaults() == null),
        "save every editable connection default")
    check_(Menu.load(null) == null, "reload defaults")
    var actual = BbsEditor.draft_(Menu.defaults)
    for (key in changes.keys) check_(actual[key] == changes[key],
        "default did not persist: %(key)")
    var entry = Menu.create("Inherited")
    actual = BbsEditor.draft_(entry)
    for (key in changes.keys) check_(actual[key] == changes[key],
        "new entry did not inherit: %(key)")
    check_((entry.delete() == null), "discard inherited test entry")
  }
  static testAddressFamily_() {
    check_(Menu.defaults.addressFamily == AddressFamily.unspec,
        "factory defaults must use only the preferred address")
    var names = ["As per DNS", "IPv4 only", "IPv6 only", "Happy Eyeballs"]
    var b = Menu.create("Address Family Test")
    check_(b.profile.addressFamily == AddressFamily.unspec, "new entry uses DNS by default")
    for (choice in [AddressFamily.happyEyeballs, AddressFamily.unspec]) {
      var app = SettingsTestApp.new([Fn.new {|pane|
        select_(pane, "Address Family")
        pane.handle(KeyEvent.new(Key.escape))
      }, Fn.new {|pane|
        var items = list_(pane).items
        check_(items.count == 4, "four address-family choices")
        for (i in 0...names.count) check_(items[i] == names[i],
            "address-family choice order")
        select_(pane, names[choice])
      }])
      check_(BbsEditor.edit(app, b, false, false) && app.remaining == 0 &&
          b.profile.addressFamily == choice, "address-family UI applies selected mode")
      check_(Menu.load(null) == null, "reload address-family UI choice")
      for (entry in Menu.entries) if (entry.name == "Address Family Test") b = entry
      check_(b.profile.addressFamily == choice, "address-family UI choice persists")
    }
    check_((b.delete() == null), "discard address-family test entry")
  }
  static testCTermLog_() {
    var b = Menu.defaults
    var d = BbsEditor.draft_(b)
    var app = SettingsTestApp.new([
      row_("CTerm Log Level"), row_("Debug"), cancel_
    ])
    BbsEditor.editLog_(app, d, Fn.new { b.ctermLogLevel = d["ctermLogLevel"] })
    check_(app.remaining == 0 && b.ctermLogLevel == 7,
        "CTerm log submenu must apply the accepted level immediately")
    check_((Menu.saveDefaults() == null) && Menu.load(null) == null && Menu.defaults.ctermLogLevel == 7,
        "CTerm log submenu value must persist")
  }
  static testConnectedMenu_() {
    for (row in SettingsMenu.rows(true)) {
      check_(row[0] != 2 && row[0] != 8, "connected settings restrictions")
    }
    var app = SettingsTestApp.new([Fn.new {|pane|
      for (label in list_(pane).items) {
        check_(!label.startsWith("Custom Screen Mode"),
            "connected program settings must omit custom geometry")
      }
    }])
    check_(!SettingsMenu.program_(app, true), "cancel must not change settings")
  }
  static testTlsPairs_() {
    var b = Menu.defaults
    var app = null
    app = SettingsTestApp.new([Fn.new {|pane|
      select_(pane, "TLS PSK Identity")
      check_(b.tlsPskIdentity == "", "first PSK edit must reach working entry")
      check_(app.alerts == 0, "paired fields must be editable one at a time")
      pane.handle(KeyEvent.new(Key.escape))
      check_(app.modalStack.indexOf(pane) >= 0 && app.alerts == 1,
          "an incomplete PSK pair must keep the editor open")
      select_(pane, "TLS PSK (Text)")
      pane.handle(KeyEvent.new(Key.escape))
    }, answer_(""), answer_("")])
    check_(BbsEditor.edit(app, b, true, false) &&
        b.tlsPskIdentity == "" && b.tlsPsk == "", "completed PSK clear must save")
    check_(app.remaining == 0, "TLS field actions consumed")
  }
  static testInputLimits_() {
    var tooLong = (0...16).map {|i| "é" }.join()
    var app = SettingsTestApp.new([answer_(tooLong), answer_("valid")])
    var result = MenuUi.promptBytes(app, "Username", "Username", "", 30,
        false, null)
    check_(result == "valid" && app.alerts == 1 && app.remaining == 0,
        "bounded fields must reject UTF-8 byte overflow without aborting")
    app = SettingsTestApp.new([answer_(tooLong), answer_(null)])
    check_(MenuUi.promptBytes(app, "Username", "Username", "", 30,
        false, null) == null && app.alerts == 1,
        "invalid byte-length input must remain cancellable")
    var decomposed = (0...15).map {|i| "e\u0301" }.join()
    app = SettingsTestApp.new([answer_(decomposed)])
    check_(MenuUi.promptBytes(app, "Username", "Username", "", 30,
        false, null) == decomposed && app.alerts == 0,
        "byte limits must use the same NFC normalization as the setter")
  }
  static testFileLocations_() {
    var s = Menu.settings
    var original = s.listPath
    s.listPath = original + ".alternate"
    check_((s.apply() == null), "switch personal list")
    check_(Menu.fileLocations["personalList"] == s.listPath,
        "file locations must show the active personal list")
    s.listPath = original
    check_((s.apply() == null), "restore personal list")
  }
  static testLicenses_() {
    var notices = Menu.licenses
    var seen = {}
    for (notice in notices) {
      check_(notice.count == 2 && notice[1].count > 200,
          "license entries contain a name and full notice")
      check_(!seen.containsKey(notice[0]), "unique license names")
      seen[notice[0]] = notice[1]
    }
    check_(seen["Wren (MIT)"].contains("Robert Nystrom") &&
        seen["Wren (MIT)"].endsWith("OTHER DEALINGS IN THE\nSOFTWARE.\n"),
        "embedded MIT notice includes attribution and final terms")
    check_(seen["libretro API (MIT)"].contains("2010-2023 The RetroArch team") &&
        seen["libretro API (MIT)"].contains("OTHER DEALINGS IN THE SOFTWARE."),
        "libretro header attribution and terms")
    check_(seen["Unicode data (Unicode License v3)"].contains("Unicode, Inc.") &&
        seen["UNSCII (Public domain)"].contains("unscii-16-full"),
        "embedded Unicode and UNSCII notices")
    var emoji = seen["SyncTERM Emoji (SIL OFL 1.1)"]
    check_(emoji.contains("Copyright 2013 Google LLC") &&
        emoji.contains("Copyright 2022 Google Inc.") &&
        emoji.contains("Noto Color Emoji 2.051") &&
        emoji.endsWith("OTHER DEALINGS IN THE FONT SOFTWARE.\n"),
        "emoji notice retains both source copyrights and complete OFL")
    var hasFont = false
    for (row in Menu.buildOptions) {
      if (row[1] == "Unifont (SyncTERM Unicode)") hasFont = row[2]
    }
    var fontName = "SyncTERM Unicode (SIL OFL 1.1)"
    check_(seen.containsKey(fontName) == hasFont,
        "OFL notice is present exactly when the font is built")
    if (hasFont) {
      var text = seen[fontName]
      check_(text.contains("Ælla Chiana Moskopp") &&
          text.contains("SIL OPEN FONT LICENSE Version 1.1") &&
          text.endsWith("OTHER DEALINGS IN THE FONT SOFTWARE.\n"),
          "font notice retains upstream copyright and complete OFL")
    }
    notices[0][1] = "changed"
    notices.clear()
    check_(Menu.licenses[0][1].contains("Copyright (c) 2026 Stephen Hurd"),
        "mutating a returned catalog cannot change embedded notices")
    for (connected in [false, true]) {
      var app = null
      app = SettingsTestApp.new([
        row_("Licenses"),
        row_(hasFont ? fontName : "Wren (MIT)"),
        Fn.new {|pane|
          check_(app.modalStack[-2].title == "Licenses",
              "license picker stays mounted beneath the notice")
          check_(pane.literal && !pane.preformatted,
              "license viewer uses literal wrapped text")
          pane.handle(KeyEvent.new(Key.end))
          check_(pane.scrollTop > 0, "license viewer reaches final terms")
          pane.handle(KeyEvent.new(Key.home))
          check_(pane.scrollTop == 0, "license viewer returns to copyright")
          pane.handle(KeyEvent.new(Key.escape))
        }, cancel_, cancel_
      ])
      check_(!SettingsMenu.run(app, connected) && app.remaining == 0 &&
          app.alerts == 0, "licenses work in offline and connected settings")
    }
  }

  static inspectEncryption_(title, canChange) {
    var app = SettingsTestApp.new([Fn.new {|pane|
      check_(pane.title == title, "encryption title")
      check_(list_(pane).items.contains("Change Password") == canChange,
          "password action visibility")
    }])
    check_(!SettingsMenu.encryption_(app) && app.remaining == 0,
        "cancelling encryption leaves list unchanged")
  }

  static testEncryption_() {
    inspectEncryption_("Not Encrypted", false)
    if (!Menu.encryptionAvailable) return
    for (choice in [["Encrypt Using ChaCha20", 7, 256],
        ["Encrypt Using AES-128", 6, 128], ["Encrypt Using AES-256", 6, 256]]) {
      var actions = [row_(choice[0])]
      if (Menu.encryptionInfo.algorithm == 0) actions.add(answer_("audit password"))
      var app = SettingsTestApp.new(actions)
      check_(SettingsMenu.encryption_(app) && app.alerts == 0 &&
          Menu.encryptionInfo.algorithm == choice[1] &&
          Menu.encryptionInfo.keyBits == choice[2], "encryption choice: %(choice[0])")
    }
    var retained = Menu.encryptionInfo
    var cancel = SettingsTestApp.new([row_("Change Password"), answer_(null)])
    check_(!SettingsMenu.encryption_(cancel) && cancel.remaining == 0 &&
        Menu.encryptionInfo.algorithm == retained.algorithm,
        "cancel password change")
    var app = SettingsTestApp.new([row_("Change Password"), answer_("new password")])
    check_(SettingsMenu.encryption_(app) && app.alerts == 0, "change list password")
    app = SettingsTestApp.new([row_("Decrypt")])
    check_(SettingsMenu.encryption_(app) && Menu.encryptionInfo.algorithm == 0,
        "decrypt list")
  }
  static testInvalidNames_() {
    var app = SettingsTestApp.new([answer_("\u00a0Font")])
    var count = Menu.fonts.count
    check_(!SettingsMenu.addFont_(app, Menu.fonts, count) &&
        Menu.fonts.count == count && app.alerts == 1,
        "invalid Unicode font name must report an error without aborting")
    app = SettingsTestApp.new([answer_("\u00a0Sort"), cancel_])
    count = Menu.sortProfiles.count
    SortProfiles.new_(app, count)
    check_(Menu.sortProfiles.count == count && app.alerts == 1,
        "invalid Unicode sort name must report an error without aborting")
  }
  static testLongFontName_() {
    var name = (0...45).map {|i| "é" }.join()
    var font = Menu.createFont(name, 0)
    check_(font != null, "create a valid long UTF-8 font name")
    var entry = Menu.create("Long Font")
    entry.profile.font = name
    check_((entry.save() == null) && Menu.load(null) == null, "persist a long font selection")
    var saved = null
    for (bbs in Menu.entries) if (bbs.name == "Long Font") saved = bbs
    check_(saved != null && saved.profile.font == name, "long font selection round trip")
    check_((saved.delete() == null) && (font.delete() == null) && (Menu.saveFonts() == null), "discard long font fixtures")
  }
  static testThemeFilenames_() {
    var document = Menu.newThemeDocument()
    for (entry in [["é.ini", "é.ini"], ["界.INI", "界.INI"],
        ["Thème", "Thème.ini"]]) {
      var app = SettingsTestApp.new([answer_(entry[0])])
      check_(ThemeEditor.filename_(app, document) == entry[1],
          "theme filename must preserve its UTF-8 basename and existing extension")
    }
  }
}
