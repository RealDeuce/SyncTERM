// Optional, independently installed SyncTERM directory/settings frontend.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, Screen, ScreenMode, Key, Text
import "wren_console" for WrenConsole
import "syncterm_menu" for Menu
import "ui_widget" for Rect
import "ui_draw" for Painter
import "ui_input" for TextInput
import "ui_popup" for Alert, Confirm
import "ui_help" for Help
import "menu_ui" for MenuUi
import "menu_bbs_editor" for BbsEditor
import "menu_settings_ui" for SettingsMenu
import "menu_sort_profiles" for SortProfiles
import "menu_scrollback" for OfflineScrollbackView
import "modern_ui_model" for ModernModel
import "modern_ui_style" for ModernApp, ModernStyle, ModernCanvas, ModernLabel, ModernButton, ModernList

class ModernUI {
  static commands {
    if (__commands == null) __commands = []
    return __commands
  }
  static registerCommand(key, keyName, description, callback) {
    if (!(key is Num) || !key.isInteger || !(keyName is String) ||
        !(description is String) || !(callback is Fn)) {
      Fiber.abort("MainMenu.registerCommand: invalid command")
    }
    for (i in 0...commands.count) {
      if (commands[i][0] == key) {
        commands[i] = [key, keyName, description, callback]
        return
      }
    }
    commands.add([key, keyName, description, callback])
  }
  static splash() {
    var size = Screen.size
    var app = ModernApp.new()
    var canvas = ModernLabel.new {|sf|
      Painter.fill(sf, Rect.new(0, 0, size[0], size[1]), " ",
          ModernStyle.text(ModernStyle.ink, ModernStyle.background))
      Painter.text(sf, 3, 2, "SyncTERM", ModernStyle.text(ModernStyle.accent, ModernStyle.background), size[0] - 3)
      Painter.text(sf, 3, 4, "Loading your directory...", ModernStyle.text(ModernStyle.muted, ModernStyle.background), size[0] - 3)
    }
    canvas.bounds = Rect.new(1, 1, size[0], size[1])
    app.root.add(canvas)
    app.drawAll_()
  }
  static run(current, connected) {
    if (!ModernModel.prepare()) return null
    var state = connected ? __connectedState : __offlineState
    var view = ModernDirectory.new(current, connected, state)
    var result = view.run()
    if (connected) {
      __connectedState = view.state
    } else {
      __offlineState = view.state
    }
    return result
  }
}

class ModernDirectory {
  construct new(current, connected, state) {
    _connected = connected
    _result = null
    _notice = null
    _settingsPage = false
    _entries = []
    _visible = []
    _preferred = current
    if (_preferred == null && state != null) _preferred = state[0]
    _app = ModernApp.new()
    _app.onError = Fn.new {|fiber|
      _notice = "Error: %(fiber.error)"
      Host.print("Modern UI: %(fiber.error)\n")
      _canvas.markDirty()
    }
    _canvas = ModernCanvas.new {|sf| paint_(sf) }
    _canvas.bounds = Rect.new(1, 1, Screen.size[0], Screen.size[1])
    _app.root.add(_canvas)
    _directoryButton = button_("Directory", Fn.new { showDirectory_() })
    _settingsButton = button_("Settings", Fn.new { showSettings_() })
    _exitButton = button_(connected ? "Return" : "Quit", Fn.new { exit_() })
    _search = TextInput.new()
    _search.maxLen = 128
    if (state != null) _search.value = state[1]
    _search.onChange = Fn.new {|text| filter_() }
    _search.onSubmit = Fn.new {|text| focus_(_list) }
    _canvas.add(_search)
    _list = ModernList.new()
    _list.wrap = false
    _list.onChange = Fn.new {|index, text|
      var entry = selected
      Menu.showEntry(entry)
      if (entry != null) _preferred = entry.name
      _canvas.markDirty()
    }
    _list.onSelect = Fn.new {|index, text| connect_() }
    _canvas.add(_list)
    _newButton = button_("New", Fn.new { add_() })
    _editButton = button_("Edit", Fn.new { edit_() })
    _connectButton = button_(connected ? "Edit entry" : "Connect", Fn.new { connect_() })
    _quickButton = button_("Quick connect", Fn.new { quick_() })
    _sortButton = button_("Sort", Fn.new { sort_() })
    _deleteButton = button_("Delete", Fn.new { delete_() })
    _categories = []
    for (row in SettingsMenu.rows(connected)) {
      var action = row[0]
      var label = row[1] == "Default Connection Settings" ? "Connection Defaults" : row[1]
      _categories.add(button_(label, Fn.new { settings_(action) }))
    }
    _app.onLayout = Fn.new {|w, h| layout_(w, h) }
    _app.chrome = Fn.new { chrome_() }
    _app.shortcut = Fn.new {|event| shortcut_(event) }
    _app.bind(Key.escape, Fn.new { escape_() })
    _app.bind(Key.f1, Fn.new { help_() })
    _app.bind(Key.wrenConsole, Fn.new { console_() })
    if (Host.localControlKeys) _app.bind(Key.ctrlQ, Fn.new { console_() })
    reload_()
    layout_(Screen.size[0], Screen.size[1])
    if (state != null && state.count > 2 && selected != null && selected.name == state[0]) {
      _list.scrollTop = state[2]
    }
    focus_(_list)
  }

  state { [_preferred, _search.value, _list.scrollTop] }
  selected {
    var index = _list.selected
    if (index == null || index >= _visible.count) return null
    return _visible[index]
  }
  run() {
    _app.runSync()
    return _result
  }
  button_(label, action) {
    var button = ModernButton.new(label)
    button.onPress = action
    _canvas.add(button)
    return button
  }
  focus_(widget) {
    _app.root.focusedIndex = 0
    _canvas.focusedIndex = _canvas.children.indexOf(widget)
  }
  directoryWidgets { [_search, _list, _newButton, _editButton, _connectButton, _sortButton, _deleteButton] }
  showDirectory_() {
    _settingsPage = false
    layout_(Screen.size[0], Screen.size[1])
    focus_(_list)
  }
  showSettings_() {
    _settingsPage = true
    layout_(Screen.size[0], Screen.size[1])
    if (_categories.count > 0) focus_(_categories[0])
  }
  reload_() {
    _entries = Menu.entries
    filter_()
  }
  filter_() {
    _visible = []
    var labels = []
    var chosen = 0
    var query = _search.value
    for (entry in _entries) {
      if (query.count == 0 || Text.caselessContains(entry.name, query) ||
          Text.caselessContains(entry.addr, query) || Text.caselessContains(entry.comment, query)) {
        if (entry.name == _preferred) chosen = _visible.count
        _visible.add(entry)
        labels.add(entry.name)
      }
    }
    _list.setEntries(labels, _visible.map {|entry| entry.addr }.toList)
    _list.emptyMessage = _entries.count == 0 ? "Your directory is empty." : "No matching connections."
    _list.focusable = labels.count > 0
    if (labels.count > 0) {
      _list.selected = chosen
    } else {
      _list.selected = null
    }
    _canvas.markDirty()
  }
  layout_(w, h) {
    _canvas.bounds = Rect.new(1, 1, w, h)
    _small = w < 72 || h < 24
    for (widget in _canvas.children) {
      widget.visible = !_small
      widget.focusable = !_small
    }
    if (_small) {
      _canvas.markDirty()
      return
    }
    _side = w >= 72 ? 17 : 0
    _contentX = _side + 3
    _contentW = (w - _contentX - 1).max(4)
    _listRect = Rect.new(_contentX, 10, _contentW, (h - 16).max(1))
    _detailsRect = Rect.new(_contentX, h - 5, _contentW, 2)
    _directoryButton.bounds = Rect.new(2, 5, 15, 3)
    _settingsButton.bounds = Rect.new(2, 9, 15, 3)
    _exitButton.bounds = Rect.new(w - 11, 1, 10, 3)
    _quickButton.bounds = Rect.new(2, h - 5, 15, 3)
    _search.bounds = Rect.new(_contentX, 6, _contentW, 1)
    _list.bounds = _listRect
    var buttonY = (h - 2).max(10)
    _newButton.bounds = Rect.new(_contentX, 7, 8.min(_contentW), 2)
    _sortButton.bounds = Rect.new(_contentX + 10, 7, 8.min(_contentW), 2)
    _editButton.bounds = Rect.new(_contentX, buttonY, 9, 2)
    _deleteButton.bounds = Rect.new(_contentX + 11, buttonY, 10, 2)
    _connectButton.bounds = Rect.new((_contentX + _contentW - 13).max(_contentX), buttonY, 13.min(_contentW), 2)
    for (widget in directoryWidgets) {
      widget.visible = !_settingsPage
      widget.focusable = !_settingsPage
    }
    _list.focusable = !_settingsPage && _visible.count > 0
    for (i in 0..._categories.count) {
      var columns = _contentW >= 48 ? 2 : 1
      var width = ((_contentW - (columns - 1) * 2) / columns).floor
      _categories[i].bounds = Rect.new(_contentX + (i % columns) * (width + 2),
          (h >= 25 ? 6 : 5) + (i / columns).floor * 4, width, 3)
      _categories[i].visible = _settingsPage
      _categories[i].focusable = _settingsPage
    }
    _canvas.markDirty()
  }
  text_(sf, x, y, text, fg, bg, width) {
    if (x >= 0 && y >= 0 && x < sf.width && y < sf.height && width > 0) {
      Painter.text(sf, x, y, text, ModernStyle.text(fg, bg), width.min(sf.width - x))
    }
  }
  paint_(sf) {
    var w = sf.width
    var h = sf.height
    Painter.fill(sf, Rect.new(0, 0, w, h), " ", ModernStyle.text(ModernStyle.ink, ModernStyle.background))
    Painter.fill(sf, Rect.new(0, 0, w, 3), " ", ModernStyle.text(0xffffff, ModernStyle.dark))
    text_(sf, 2, 1, "SyncTERM", 0xffffff, ModernStyle.dark, 20)
    if (_small) {
      text_(sf, 2, 5, "Modern UI needs 72 columns and 24 rows.", ModernStyle.ink, ModernStyle.background, w - 4)
      text_(sf, 2, 7, "Enter: 80x25   Escape: exit", ModernStyle.muted, ModernStyle.background, w - 4)
      if (_notice != null) text_(sf, 2, 9, _notice, ModernStyle.muted, ModernStyle.background, w - 4)
      return
    }
    if (w >= 55) text_(sf, 14, 1, "MODERN UI  /  " + (_connected ? "SESSION DIRECTORY" : "YOUR CONNECTIONS"), 0x94a3b8, ModernStyle.dark, w - 29)
    if (_side > 0) Painter.fill(sf, Rect.new(0, 3, _side, h - 4), " ", ModernStyle.text(0xffffff, ModernStyle.dark))
    var x = _contentX - 1
    text_(sf, x, 3, _settingsPage ? "Settings" : "Connection directory", ModernStyle.ink, ModernStyle.background, _contentW)
    if (_settingsPage) {
      if (h >= 25) text_(sf, x, 4, "Configure SyncTERM and your connection defaults", ModernStyle.muted, ModernStyle.background, _contentW)
    } else {
      text_(sf, x, 4, "Search by name, address or comment", ModernStyle.muted, ModernStyle.background, _contentW)
      text_(sf, x + 2, 8, "Name", ModernStyle.muted, ModernStyle.background, _list.columnX - 4)
      text_(sf, x + _list.columnX, 8, "Address", ModernStyle.muted, ModernStyle.background, _contentW - _list.columnX)
      var entry = selected
      var detailBg = 0xe8eef6
      text_(sf, x + 2, _detailsRect.y - 2, "Selected connection", ModernStyle.muted, ModernStyle.background, _contentW - 4)
      Painter.fill(sf, Rect.new(x, _detailsRect.y - 1, _detailsRect.w, 2), " ", ModernStyle.text(ModernStyle.ink, detailBg))
      var dy = _detailsRect.y - 1
      text_(sf, x + 2, dy, entry == null ? "Select a connection" : entry.name,
          ModernStyle.accent, detailBg, _contentW - 4)
      if (entry != null) {
        var source = entry.type == 0 ? "Personal" : "Shared"
        var column = _list.columnX.max(Screen.textWidth(entry.name, 0) + 4)
        var meta = "%(entry.connTypeName) :%(entry.port) / %(source)"
        if (Screen.textWidth(meta, 0) > _contentW - column - 2) {
          meta = "%(entry.connTypeName):%(entry.port) %(source)"
        }
        text_(sf, x + column, dy, meta,
            ModernStyle.muted, detailBg, _contentW - column - 2)
        text_(sf, x + 2, dy + 1, entry.comment.count > 0 ? entry.comment : entry.addr,
            ModernStyle.muted, detailBg, _contentW - 4)
      }
    }
    Painter.fill(sf, Rect.new(0, h - 1, w, 1), " ", ModernStyle.text(ModernStyle.muted, 0xe2e8f0))
    var status = _settingsPage ? "Choose a settings category" : "%(_visible.count) connections  |  Ctrl-F Search  F2 Edit  Insert New"
    var hints = _app.keyHints
    if (_app.modalStack.count > 0 && hints != null && hints.count > 0) {
      status = hints.map {|hint| hint.join(" ") }.join("  ")
    }
    if (Host.safeMode) status = "SAFE MODE  |  " + status
    if (_notice != null) status = _notice
    text_(sf, 1, h - 1, status, ModernStyle.muted, 0xe2e8f0, w - 2)
  }
  chrome_() {
    if (_small) return
    if (!_settingsPage) _list.chrome(_app)
    if (!_settingsPage) _app.round(_detailsRect, 0xe8eef6, ModernStyle.background)
    for (button in [_directoryButton, _settingsButton, _quickButton, _exitButton]) {
      button.chrome(_app, ModernStyle.dark)
    }
    for (button in _settingsPage ? _categories : [_newButton, _editButton, _deleteButton, _connectButton, _sortButton]) {
      button.chrome(_app, ModernStyle.background)
    }
  }
  shortcut_(event) {
    if (_small) {
      if (event.code != Key.enter) return false
      if (!Menu.setScreenMode(ScreenMode.c80x25)) _notice = "80x25 mode is unavailable."
      _app.markAllDirty_()
      return true
    }
    if (event.code == Key.wrenConsole || (Host.localControlKeys && event.code == Key.ctrlQ)) {
      console_()
      return true
    }
    if (!_connected && event.code == Key.altB) {
      OfflineScrollbackView.show(_app)
      return true
    }
    for (command in ModernUI.commands) {
      if (command[0] == event.code) {
        command[3].call(_app, _connected)
        return true
      }
    }
    if (event.code == Key.ctrlF) {
      showDirectory_()
      focus_(_search)
      _search.selectAll()
    } else if (event.code == Key.insert || event.code == Key.ctrlN) {
      add_()
    } else if (event.code == Key.f2) {
      edit_()
    } else {
      return false
    }
    return true
  }
  mutable_() {
    if (!Host.safeMode) return true
    Alert.show(_app, "Safe mode", "Directory changes are disabled in safe mode.")
    return false
  }
  connect_() {
    if (selected == null) return
    if (_connected) {
      edit_()
    } else {
      _result = selected
      _app.quit()
    }
  }
  add_() {
    if (!mutable_()) return
    if (!Menu.canAppendEntry) {
      Alert.show(_app, "Directory full", "No more entries can be added.")
      return
    }
    var name = ""
    while (true) {
      name = MenuUi.prompt(_app, "New connection", "Name", name, 30, false)
      if (name == null || name.count == 0) return
      if (Menu.nameAvailable(name)) break
      BbsEditor.showNameError(_app, "New connection", name)
    }
    var entry = Menu.create(name)
    if (entry == null) {
      Alert.show(_app, "New connection", "The entry could not be created.")
      return
    }
    _preferred = name
    _search.value = ""
    reload_()
    var type = BbsEditor.chooseConnectionType(_app, entry.connType)
    if (type == null) {
      entry.delete()
      reload_()
      return
    }
    entry.connType = type
    var port = Menu.defaultPort(type)
    if (port > 0) entry.port = port
    var address = MenuUi.prompt(_app, "New connection", "Address / device / command", entry.addr, 64, false)
    if (address == null || address.count == 0) {
      entry.delete()
      reload_()
      return
    }
    entry.addr = address
    if (!entry.save()) {
      Alert.show(_app, "New connection", "The entry could not be saved.")
      entry.delete()
    }
    reload_()
  }
  edit_() {
    var entry = selected
    if (entry == null || !mutable_()) return
    if (entry.type != 0) {
      if (!Confirm.show(_app, "Copy to personal directory?")) return
      entry = Menu.copy(entry, entry.name)
      if (entry == null) {
        Alert.show(_app, "Copy failed", "The personal entry could not be created.")
        return
      }
      reload_()
      if (!entry.save()) {
        Alert.show(_app, "Copy failed", "The personal copy could not be saved.")
        entry.delete()
        reload_()
        return
      }
    }
    if (BbsEditor.edit(_app, entry, false, false)) _preferred = entry.name
    reload_()
  }
  delete_() {
    var entry = selected
    if (entry == null || !mutable_()) return
    if (entry.type != 0) {
      Alert.show(_app, "Shared connection", "Shared directory entries cannot be deleted.")
      return
    }
    if (Confirm.show(_app, "Delete %(entry.name)?")) {
      if (!entry.delete()) Alert.show(_app, "Delete failed", "The entry could not be deleted.")
      reload_()
    }
  }
  quick_() {
    if (_connected) {
      Alert.show(_app, "Connection active", "Return to the terminal to use the current connection.")
      return
    }
    if (!mutable_()) return
    var url = MenuUi.prompt(_app, "Quick connect", "Address or URL", "", 1024, false)
    if (url == null || url.count == 0) return
    var entry = Menu.quickConnect(url)
    if (entry == null) {
      Alert.show(_app, "Quick connect", "The address could not be parsed.")
      return
    }
    _result = entry
    _app.quit()
  }
  sort_() {
    SortProfiles.show(_app)
    reload_()
  }
  settings_(action) {
    // Stop retaining model handles while nested editors may replace the model.
    var name = selected == null ? _preferred : selected.name
    _entries = []
    _visible = []
    _list.items = []
    SettingsMenu.begin()
    var changed = SettingsMenu.runAction(_app, action, _connected)
    _app.theme = ModernStyle.theme
    if (SettingsMenu.passwordChanged) ModernModel.password = SettingsMenu.directoryPassword
    if (changed) ModernModel.prepare()
    _preferred = name
    reload_()
  }
  escape_() {
    if (_settingsPage) {
      showDirectory_()
    } else if (_search.value.count > 0) {
      _search.value = ""
      filter_()
    } else {
      exit_()
    }
  }
  exit_() {
    if (_connected) {
      _app.quit()
      return
    }
    if (!Menu.settings.confirmClose || Confirm.show(_app, "Exit SyncTERM?")) {
      Menu.quitApplication()
      _app.quit()
    }
  }
  help_() {
    var body = "# Modern UI\n\nUse Directory and Settings to switch workspaces. Search matches names, addresses and comments.\n\nCtrl-F focuses search; F2 edits; Insert creates an entry. Tab moves focus. Escape clears a search or returns to the directory.\n\nConnect opens the selected entry; the connected directory is edit-only. The standard terminal and file picker remain available."
    for (command in ModernUI.commands) body = body + "\n\n" + command[1] + "\n: " + command[2]
    var help = Help.new("Modern UI", body)
    help.fitToScreen(64, 20)
    _app.modal(help)
  }
  console_() {
    WrenConsole.run("main_menu")
    _app.markAllDirty_()
  }
}
