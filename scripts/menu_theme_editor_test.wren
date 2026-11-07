// Menu-VM self-tests for the theme editor's non-blocking UI pieces.

import "syncterm" for Font, Key, KeyEvent, Mouse, MouseEvent
import "ui_widget" for Rect
import "menu_settings_ui" for SettingsMenu
import "menu_theme_editor" for ThemeAtlas, ThemeEditor, ThemeEditorModel,
    ThemeCodepointPicker, ThemeGlyphGrid, ThemeInspector, ThemeWidgetPreviewContent,
    ThemeWidgetPreviewPane

// UI fixture with the named cloud-catalog interface.
class CloudThemeFixture {
  construct new(package, name, author, description, version, cached, update, error) {
    _package = package
    _name = name
    _author = author
    _description = description
    _version = version
    _cached = cached
    _update = update
    _error = error
  }
  package { _package }
  name { _name }
  author { _author }
  description { _description }
  version { _version }
  cached { _cached }
  updateAvailable { _update }
  error { _error }
}

class FakeThemeDocument {
  construct new() {
    _styles = [
      ["default", true, 0, 0, 0x07, 0xffffff, 0x000000],
      ["list", true, 4, -1, -1, 0x123456, -1],
      ["list.item", true, 4, -1, -1, -1, -1],
      ["list.item.focused", true, 4, -1, -1, -1, -1],
      ["input.focused", true, 0, -1, -1, -1, -1]
    ]
    _glyphs = [["focus.right", true, 0, 0x10, 0x3e, 0x25ba]]
  }

  styles { _styles }
  glyphs { _glyphs }
  themeData {
    return [[
      ["default", 0, 0x07, 0xffffff, 0x000000],
      ["list", null, null, 0x123456, null],
      ["list.item", null, null, null, null],
      ["list.item.focused", null, null, null, null],
      ["input.focused", null, null, null, null]
    ], [["focus.right", String.fromCodePoint(0x25ba), ">"]]]
  }
}

class MenuTest {
  static run() {
    __pass = 0
    __fail = 0
    System.print("=== menu_theme_editor self-test starting ===")

    testGridGeometry_()
    testGridDecodesCp437_()
    testGridKeyboardWrap_()
    testGridPageNavigation_()
    testGridMouseSelection_()
    testAtlasPageNavigation_()
    testAtlasBoundaryNavigation_()
    testCascadeTrace_()
    testCodepointInput_()
    testCodepointDisplay_()
    testPreviewRoleSelection_()
    testThemeBrowserPartition_()
    testThemeSelectionIdentity_()
    testThemeComment_()

    var total = __pass + __fail
    System.print("=== menu_theme_editor: %(total) tests, %(__pass) pass, " +
        "%(__fail) fail ===")
    return [__pass, __fail]
  }

  static runCloudCatalogAudit() {
    __pass = 0
    __fail = 0
    testThemeBrowserPartition_()
    testThemeSelectionIdentity_()
    testThemeComment_()
    return [__pass, __fail]
  }

  static runNavigationTests() {
    __pass = 0
    __fail = 0
    testGridKeyboardWrap_()
    testGridPageNavigation_()
    testAtlasPageNavigation_()
    testAtlasBoundaryNavigation_()
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

  static testGridGeometry_() {
    var cp437 = ThemeGlyphGrid.new("cp437", -1)
    var ascii = ThemeGlyphGrid.new("ascii", 0xff)
    check_(cp437.minimum == 0 && cp437.maximum == 0xff &&
           cp437.columns == 16 && cp437.rows == 16 &&
           cp437.preferredWidth == 33 && cp437.preferredHeight == 16 &&
           cp437.selected == 0,
           "ThemeGlyphGrid: CP437 is a clamped 16 by 16 grid")
    check_(ascii.minimum == 0x20 && ascii.maximum == 0x7e &&
           ascii.columns == 16 && ascii.rows == 6 &&
           ascii.preferredWidth == 33 && ascii.preferredHeight == 6 &&
           ascii.selected == 0x7e,
           "ThemeGlyphGrid: ASCII is a clamped 16 by 6 grid")
  }

  static testGridDecodesCp437_() {
    var cp437 = ThemeGlyphGrid.new("cp437", 0xdb)
    cp437.bounds = Rect.new(3, 4, 33, 16)
    var cpSurface = cp437.draw()
    check_(cpSurface.cellAt(1, 0).ch == "\u0000" &&
           cpSurface.cellAt(1, 0).font == Font.cp437English &&
           cpSurface.cellAt(23, 13).ch == "█" &&
           cpSurface.cellAt(31, 15).ch == "\u00a0" &&
           cpSurface.cellAt(22, 13).legacyAttr ==
               cpSurface.cellAt(23, 13).legacyAttr &&
           cpSurface.cellAt(23, 13).legacyAttr ==
               cpSurface.cellAt(24, 13).legacyAttr,
           "ThemeGlyphGrid: decoded CP437 characters have symmetric shared highlights")

    var ascii = ThemeGlyphGrid.new("ascii", 0x7e)
    ascii.bounds = Rect.new(0, 0, 33, 6)
    var asciiSurface = ascii.draw()
    check_(asciiSurface.cellAt(1, 0).ch == " " &&
           asciiSurface.cellAt(29, 5).ch == "~" &&
           asciiSurface.cellAt(31, 5).ch == " ",
           "ThemeGlyphGrid: ASCII leaves its unused final cell blank")
  }

  static testGridKeyboardWrap_() {
    var cp437 = ThemeGlyphGrid.new("cp437", 0xff)
    cp437.handle(KeyEvent.new(Key.right))
    var right = cp437.selected == 0
    cp437.handle(KeyEvent.new(Key.left))
    var left = cp437.selected == 0xff

    var ascii = ThemeGlyphGrid.new("ascii", 0x6f)
    ascii.handle(KeyEvent.new(Key.down))
    var down = ascii.selected == 0x2f
    ascii.handle(KeyEvent.new(Key.up))
    var up = ascii.selected == 0x6f
    ascii.selected = 0x7e
    ascii.handle(KeyEvent.new(Key.down))
    var partial = ascii.selected == 0x2e
    check_(right && left && down && up && partial,
           "ThemeGlyphGrid: keyboard movement wraps without changing columns")
  }

  static testGridMouseSelection_() {
    var selected = null
    var ascii = ThemeGlyphGrid.new("ascii", 0x20)
    ascii.bounds = Rect.new(10, 5, 33, 6)
    ascii.onSelect = Fn.new {|value| selected = value }
    var click = ascii.handle(
        MouseEvent.new(Mouse.button1Click, 15, 7, 15, 7))
    var clicked = click && ascii.selected == 0x42 && selected == null
    var activate = ascii.handle(
        MouseEvent.new(Mouse.button1DblClick, 17, 7, 17, 7))
    check_(clicked && activate && selected == 0x43,
           "ThemeGlyphGrid: click selects and double click accepts")
  }

  static testGridPageNavigation_() {
    for (mode in ["cp437", "ascii"]) {
      var bottom = mode == "cp437" ? 0xfe : 0x6f
      var top = mode == "cp437" ? 0x0e : 0x2f
      var grid = ThemeGlyphGrid.new(mode, bottom - 16)
      grid.handle(KeyEvent.new(Key.pageDown))
      check_(grid.selected == bottom, "ThemeGlyphGrid: %(mode) page reaches last row in column")
      grid.handle(KeyEvent.new(Key.pageDown))
      check_(grid.selected == bottom, "ThemeGlyphGrid: %(mode) Page Down never wraps")
      grid.selected = top + 16
      grid.handle(KeyEvent.new(Key.pageUp))
      check_(grid.selected == top, "ThemeGlyphGrid: %(mode) page reaches first row in column")
      grid.handle(KeyEvent.new(Key.pageUp))
      check_(grid.selected == top, "ThemeGlyphGrid: %(mode) Page Up never wraps")
    }
  }

  static model_() { ThemeEditorModel.new(FakeThemeDocument.new()) }

  static testAtlasPageNavigation_() {
    var model = model_()
    var atlas = ThemeAtlas.new(model, Fn.new {}, Fn.new {})
    atlas.bounds = Rect.new(0, 0, 40, 4)
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 3, "ThemeAtlas: Page Down advances a page")
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 4, "ThemeAtlas: Page Down stops at the last item")
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 4, "ThemeAtlas: Page Down stays at the last item")

    atlas.handle(KeyEvent.new(Key.end))
    atlas.handle(KeyEvent.new(Key.pageUp))
    check_(model.selectedIndex == 1, "ThemeAtlas: Page Up retreats a page")
    atlas.handle(KeyEvent.new(Key.pageUp))
    check_(model.selectedIndex == 0, "ThemeAtlas: Page Up stops at the first item")
    atlas.handle(KeyEvent.new(Key.pageUp))
    check_(model.selectedIndex == 0, "ThemeAtlas: Page Up stays at the first item")

    atlas.bounds = Rect.new(0, 0, 40, 10)
    model.select(1)
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 4, "ThemeAtlas: oversized page reaches the last item")
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 4, "ThemeAtlas: oversized page stays at the last item")
    model.select(3)
    atlas.handle(KeyEvent.new(Key.pageUp))
    check_(model.selectedIndex == 0, "ThemeAtlas: oversized page reaches the first item")
    atlas.handle(KeyEvent.new(Key.pageUp))
    check_(model.selectedIndex == 0, "ThemeAtlas: oversized page stays at the first item")

    atlas.bounds = Rect.new(0, 0, 40, 4)
    model.select(3)
    atlas.handle(MouseEvent.new(Mouse.wheelDown, 39, 1, 39, 1))
    check_(model.selectedIndex == 4, "ThemeAtlas: scrollbar wheel reaches the last item")
    atlas.handle(MouseEvent.new(Mouse.wheelDown, 39, 1, 39, 1))
    check_(model.selectedIndex == 4, "ThemeAtlas: scrollbar wheel does not wrap down")
    model.select(1)
    atlas.handle(MouseEvent.new(Mouse.wheelUp, 39, 1, 39, 1))
    check_(model.selectedIndex == 0, "ThemeAtlas: scrollbar wheel reaches the first item")
    atlas.handle(MouseEvent.new(Mouse.wheelUp, 39, 1, 39, 1))
    check_(model.selectedIndex == 0, "ThemeAtlas: scrollbar wheel does not wrap up")
  }

  static testAtlasBoundaryNavigation_() {
    var model = model_()
    var atlas = ThemeAtlas.new(model, Fn.new {}, Fn.new {})
    atlas.bounds = Rect.new(0, 0, 40, 4)
    atlas.handle(KeyEvent.new(Key.up))
    check_(model.selectedIndex == 4, "ThemeAtlas: Up wraps at the first item")
    atlas.handle(KeyEvent.new(Key.down))
    check_(model.selectedIndex == 0, "ThemeAtlas: Down wraps at the last item")
    model.select(1)
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 4, "ThemeAtlas: exact page reaches the last item")

    atlas.bounds = Rect.new(0, 0, 40, 1)
    atlas.handle(KeyEvent.new(Key.pageDown))
    check_(model.selectedIndex == 4, "ThemeAtlas: zero-distance page does not wrap")
    model.mode = "glyphs"
    atlas.bounds = Rect.new(0, 0, 40, 4)
    atlas.handle(KeyEvent.new(Key.pageDown))
    atlas.handle(KeyEvent.new(Key.pageUp))
    check_(model.selectedIndex == 0, "ThemeAtlas: single-item list stays selected")
  }

  static testCascadeTrace_() {
    var model = model_()
    model.selectStyle("list.item.focused")
    var cascade = model.styleCascade("list.item.focused", 2)
    var resolved = model.styleField("list.item.focused", 2)
    var text = ThemeEditor.cascadeText_(model)
    check_(cascade.count == 4 &&
           cascade[0][0] == "list.item.focused" &&
           cascade[0][1] == "explicit inherit" && !cascade[0][3] &&
           cascade[1][0] == "list.item" &&
           cascade[1][1] == "explicit inherit" && !cascade[1][3] &&
           cascade[2][0] == "list" && cascade[2][1] == "explicit" &&
           cascade[2][2] == 0x123456 && cascade[2][3] &&
           cascade[3][0] == "default" && !cascade[3][3] &&
           resolved[0] == 0x123456 && resolved[1] == "list" &&
           text.indexOf("`list.item.focused`") >= 0 &&
           text.indexOf("`list.item`") >= 0 &&
           text.indexOf("`list`") >= 0 &&
           text.indexOf("**winner**") >= 0,
           "ThemeEditorModel: cascade trace retains every candidate and winner")
  }

  static testCodepointInput_() {
    check_(ThemeCodepointPicker.parse_(String.fromCodePoint(0x2603)) == 0x2603 &&
           ThemeCodepointPicker.parse_("U+250C") == 0x250c &&
           ThemeCodepointPicker.parse_("0x1F642") == 0x1f642 &&
           ThemeCodepointPicker.parse_("U+D800") == null &&
           ThemeCodepointPicker.parse_("ab") == null &&
           ThemeCodepointPicker.parse_("U+") == null,
           "ThemeCodepointPicker accepts one character or U+/0x notation")
  }

  static testCodepointDisplay_() {
    var model = model_()
    model.mode = "glyphs"
    var atlas = ThemeAtlas.new(model, Fn.new {}, Fn.new {})
    atlas.bounds = Rect.new(0, 0, 40, 3)
    var unicode = atlas.draw()
    var unicodeAtlas = unicode.cellAt(2, 0).ch == String.fromCodePoint(0x25ba) &&
        unicode.cellAt(18, 0).ch == "[" &&
        unicode.cellAt(19, 0).ch == "U" && unicode.cellAt(20, 0).ch == "+" &&
        unicode.cellAt(21, 0).ch == "2" && unicode.cellAt(24, 0).ch == "A" &&
        unicode.cellAt(25, 0).ch == "]"

    var inspector = ThemeInspector.new(model)
    inspector.bounds = Rect.new(0, 0, 30, 8)
    var unicodeInspector = inspector.draw().cellAt(0, 3).ch == "C"

    model.glyphMode = "ascii"
    atlas.refresh()
    inspector.refresh()
    var ascii = atlas.draw()
    var asciiAtlas = ascii.cellAt(2, 0).ch == ">" &&
        ascii.cellAt(18, 0).ch == "[" && ascii.cellAt(19, 0).ch == "6" &&
        ascii.cellAt(20, 0).ch == "2" && ascii.cellAt(21, 0).ch == "]"
    var asciiInspector = inspector.draw().cellAt(0, 7).ch == "A"
    check_(unicodeAtlas && unicodeInspector && asciiAtlas && asciiInspector,
           "Theme glyph display: Unicode uses codepoints and ASCII fallback")
  }

  static testPreviewRoleSelection_() {
    var model = model_()
    var selected = null
    var content = ThemeWidgetPreviewContent.new(model.theme, Fn.new {},
        Fn.new {|role| selected = role })
    var initial = content.currentRole == "list.item.focused"
    content.handle(KeyEvent.new(Key.tab))
    var input = content.currentRole == "input.focused"
    var pane = ThemeWidgetPreviewPane.new(content, Fn.new {})
    var handled = pane.handle(KeyEvent.new(Key.f2))
    check_(initial && input && handled && selected == "input.focused",
           "Theme widget preview: F2 selects the focused widget role")
  }

  static testThemeBrowserPartition_() {
    var local = [
      ["", "Classic Theme", "SyncTERM", null, null, null],
      ["local.ini", "Local", null, null, null, null]
    ]
    var cloud = [
      CloudThemeFixture.new("official/cached", "Cached", null, null, null, true, false, null),
      CloudThemeFixture.new("official/online", "Online", null, null, null, false, false, null)
    ]
    var installed = SettingsMenu.installedThemeEntries_(local, cloud)
    var online = SettingsMenu.onlineThemeEntries_(cloud)
    check_(installed.count == 3 && installed[0][0] == "classic" &&
           installed[1][0] == "file:local.ini" &&
           installed[2][0] == "package:official/cached" &&
           online.count == 1 && online[0].package == "official/online",
           "Theme browsers partition cached and online-only packages")
  }

  static testThemeSelectionIdentity_() {
    check_(SettingsMenu.selectedThemeIdentity_("", "") == "classic" &&
           SettingsMenu.selectedThemeIdentity_("local.ini", "") ==
               "file:local.ini" &&
           SettingsMenu.selectedThemeIdentity_("", "official/cached") ==
               "package:official/cached",
           "Theme browser identities distinguish files and packages")
  }

  static testThemeComment_() {
    var entry = ["package:official/cached", "Cached", null,
        "Cached description", null, null, "package", "official/cached",
        false]
    check_(SettingsMenu.themeComment_(entry) == "Cached description" &&
           SettingsMenu.themeComment_(null) == "",
           "Theme browser exposes descriptions through the comment line")
  }
}
