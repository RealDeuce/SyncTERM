// Independent draft, metadata and formatting contracts for the DOS interface.
import "syncterm" for Host, Key, KeyEvent, Mod, Mouse, MouseEvent
import "syncterm_menu" for Menu, MenuError
import "menu_dos" for DosTable
import "menu_dos_forms" for DosForms, DosFormTab
import "menu_bbs_editor" for BbsEditor
import "ui_app" for App
import "ui_widget" for Rect
import "ui_style" for Style, Theme
import "main_menu" for DosMainList

class DosTests {
  static check(value, message) {
    if (!value) Fiber.abort(message)
  }
  static run() {
    check(Menu.load(null) == null, "load")
    var count = Menu.entries.count
    var p = Menu.defaults.copy()
    p.addr = "draft.example"
    p.port = 2222
    p.comment = "draft notes"
    var entry = Menu.createFromProfile("Draft Test", p, 17)
    check(entry is MenuError == false, "create")
    check(Menu.entries.count == count + 1, "creation count")
    check(entry.profile.addr == "draft.example" && entry.sortOrder == 17, "created values")
    check(entry.lastCallText == "Never", "never called")
    var id = entry.id
    var copy = entry.profile.copy()
    copy.addr = "changed.example"
    check(entry.profile.addr == "draft.example", "detached draft")
    check(entry.saveDraft(copy, "syncterm-system-cache", 3) is MenuError, "reserved name")
    check(entry.profile.addr == "draft.example" && entry.name == "Draft Test" && entry.sortOrder == 17, "failed draft publication")
    check(Menu.createFromProfile("Draft Test", copy, 4) is MenuError, "duplicate")
    check(Menu.entries.count == count + 1, "failed creation count")
    check(entry.saveDraft(copy, "Renamed Draft", -2) == null, "save draft")
    check(entry.id == id && entry.name == "Renamed Draft" && entry.profile.addr == "changed.example" && entry.sortOrder == -2, "published draft")
    check(entry.calls == 0 && entry.connected == 0, "call metadata")
    var borrowed = Fiber.new { entry.saveDraft(entry.profile, "Borrowed", 0) }
    borrowed.try()
    check(borrowed.error != null && entry.name == "Renamed Draft", "borrowed draft rejected")
    var nul = Fiber.new { entry.saveDraft(copy, "prefix\0suffix", 0) }
    nul.try()
    check(nul.error != null && entry.name == "Renamed Draft", "NUL name rejected")
    var nulCreate = Fiber.new { Menu.createFromProfile("prefix\0suffix", copy, 0) }
    nulCreate.try()
    check(nulCreate.error != null && Menu.entries.count == count + 1, "NUL creation rejected")
    var alpha = Menu.entries.where {|e| e.name == "Alpha" }.toList[0]
    var stats = alpha.profile.copy()
    stats.comment = "edited privately"
    var alphaId = alpha.id
    check(alpha.saveDraft(stats, "Alpha", alpha.sortOrder) == null, "edit with existing stats")
    check(alpha.id == alphaId && alpha.calls == 42 && alpha.connected == 1700000000 && alpha.added == 1600000000, "preserve existing stats and identity")
    var defaults = Menu.defaults.copy()
    var before = Menu.defaults.port
    defaults.port = 4242
    check(Menu.defaults.port == before, "default draft isolation")
    check(Menu.saveDefaults(defaults) == null && Menu.defaults.port == 4242, "defaults save")
    var bad = Fiber.new { entry.saveDraft(Menu.settings, "invalid", 0) }
    bad.try()
    check(bad.error != null && entry.name == "Renamed Draft", "foreign type guard")

    p.screenMode = 1
    p.encoding = 0
    for (i in 0...7) {
      p.graphics = i
      check(DosTable.terminal(p) == ["ANSI", "ANSI/RIP1", "ANSI/RIP3", "ANSI/Sky1.3", "ANSI/Sky3.1", "ANSI/IGS", "ANSI/BIG"][i], "graphics label %(i)")
    }
    check(DosTable.fit("一Z", 2) == "一", "wide clipping")
    var tabTheme = Theme.new({
      "default": Style.new(0, 0x1f, 0xffffff, 0x0000aa),
      "button.focused": Style.new(null, 0x4f, 0xffffff, 0xaa0000),
      "tab": Style.new(null, 0x1f, 0xffffff, 0x0000aa),
      "tab.selected": Style.new(null, 0x30, 0x000000, 0x00aaaa),
      "tab.focused": Style.new(null, 0x70, 0x000000, 0xaaaaaa),
      "tab.selected.focused": Style.new(null, 0x60, 0x000000, 0xaaaa00)
    }, Theme.default.glyphs)
    var currentTab = DosFormTab.new("Current", true)
    currentTab.theme = tabTheme
    currentTab.bounds = Rect.new(1, 1, 14, 1)
    check(currentTab.draw().cellAt(2, 0).bgRgb == 0x00aaaa, "current tab has its own theme role")
    currentTab.focused = true
    check(currentTab.draw().cellAt(2, 0).bgRgb == 0xaaaa00, "focused current tab has its own theme role")
    var otherTab = DosFormTab.new("Other", false)
    otherTab.theme = tabTheme
    otherTab.bounds = Rect.new(1, 1, 14, 1)
    check(otherTab.draw().cellAt(2, 0).bgRgb == 0x0000aa, "ordinary tab has its own theme role")
    otherTab.focused = true
    check(otherTab.draw().cellAt(2, 0).bgRgb == 0xaaaaaa, "tab focus is independent of current tab and button focus")
    var app = App.new()
    for (type in 1..13) {
      var d = BbsEditor.draft_(p)
      BbsEditor.changeConnection_(d, type)
      var rows = BbsEditor.rows_(app, p, d, false)
      var labels = []
      for (page in 0...6) for (f in DosForms.entryFields_(app, p, d, false, page)) labels.add(f[0])
      for (row in rows) check(row[3][0] == "Log Configuration" || labels.contains(row[3][0]), "missing entry field %(row[3][0]) type %(type)")
    }
    var activation = 0
    var list = DosMainList.new(Fn.new { true }, null, null, null, null, null, null)
    list.entries = Menu.entries
    list.items = Menu.entries.map {|e| e.name }.toList
    list.bounds = Rect.new(2, 2, 70, 6)
    list.onSelect = Fn.new {|index, item| activation = activation + 1 }
    list.handle(MouseEvent.new(Mouse.button1Click, 3, 2))
    check(activation == 0 && list.selected == 0, "single click selects")
    list.handle(MouseEvent.new(Mouse.button1DblClick, 3, 2))
    check(activation == 1, "double click activates")
    var settings = Menu.settings
    var ui = settings.mainUi
    settings.mainUi = 1 - ui
    check(Menu.settings.mainUi == ui, "settings isolation")
    var invalid = Fiber.new { settings.mainUi = 2 }
    invalid.try()
    check(invalid.error != null, "UI enum validation")
    for (page in 0...7) {
      for (f in DosForms.programFields_(app, settings, false, page)) {
        check(f[2].call() != null, "missing setting value %(f[0])")
        check(f[9] is String, "missing setting help %(f[0])")
      }
    }
    check(Menu.load(null) == null, "reload")
    var stale = Fiber.new { entry.saveDraft(copy, "Retired", 0) }
    stale.try()
    check(stale.error != null, "stale draft target")
    var loaded = Menu.entries.where {|e| e.name == "Renamed Draft" }.toList
    check(loaded.count == 1 && loaded[0].profile.addr == "changed.example", "persistence")
    Host.print("DOS-CONTRACTS-PASS\n")
  }
}
