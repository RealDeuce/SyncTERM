// Full-page draft editors for the built-in DOS interface.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Host, Screen, Key, KeyEvent, Mouse, MouseEvent, Mod, Profile, Text
import "syncterm_menu" for Menu, MenuError, DirectoryEntry
import "ui_widget" for Rect
import "ui_pane" for Pane
import "ui_app" for App
import "ui_input" for SelectOnFocusInput
import "ui_button" for Button
import "ui_checkbox" for Checkbox
import "ui_draw" for Painter
import "ui_popup" for Alert, Confirm
import "ui_style" for Theme
import "menu_ui" for MenuUi
import "menu_bbs_editor" for BbsEditor
import "menu_settings_ui" for SettingsMenu

// Selection belongs to the tab; keyboard focus can move into its fields.
class DosFormTab is Button {
  construct new(label, selected) {
    super(label)
    _selected = selected
    hotkeyIdx = -1
  }
  onPaint_() {
    var role = _selected ? "tab.selected" : "tab"
    if (focused) role = role + ".focused"
    var st = style(role)
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", st)
    var text = "%(focused ? ">" : " ")%(_selected ? "[" : " ")%(label)%(_selected ? "]" : " ") "
    Painter.text(surface, 0, 0, text, st, bounds.w)
  }
}

class DosFormInput is SelectOnFocusInput {
  construct new() { super() }
  paintStyle_ { style(allSelected ? "input.focused" : "input") }
  fillStyle_ { style("input") }
  handle(event) {
    if (event is MouseEvent && bounds.contains(event.startX, event.startY)) {
      var gaining = !focused
      if (event.event == Mouse.button1Click || event.event == Mouse.button2Click || event.event == Mouse.button3Click) {
        parent.focusedIndex = parent.children.indexOf(this)
        if (gaining && event.event == Mouse.button1Click) return true
      }
    }
    return super.handle(event)
  }
}

class DosFormChoice is Button {
  construct new(label, value, valueX, dropdown) {
    super(label)
    _value = value == null || value.toString.count == 0 ? "Open" : value.toString
    _valueX = valueX
    _dropdown = dropdown
    hotkeyIdx = -1
  }
  valueBounds_ { Rect.new(bounds.x + _valueX, bounds.y, bounds.w - _valueX, 1) }
  dropdown { _dropdown }
  onPaint_() {
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", style("default"))
    Painter.text(surface, 0, 0, label, style("default"), _valueX - 1)
    var st = style(focused ? "button.focused" : "button")
    var width = bounds.w - _valueX
    Painter.fill(surface, Rect.new(_valueX, 0, width, 1), " ", st)
    Painter.text(surface, _valueX, 0, "[ %(_value)", st, width - (_dropdown ? 4 : 2))
    Painter.text(surface, bounds.w - (_dropdown ? 3 : 1), 0, _dropdown ? " v]" : "]", st, _dropdown ? 3 : 1)
  }
  handle(event) {
    if (_dropdown && event is KeyEvent && event.shortcut_ == Key.chord_(Key.down, Mod.alt)) {
      activate_()
      return true
    }
    if (event is MouseEvent && event.event == Mouse.button1Click && bounds.contains(event.startX, event.startY)) parent.focusedIndex = parent.children.indexOf(this)
    return super.handle(event)
  }
}

class DosFormCheckbox is Checkbox {
  construct new(label, valueX) {
    super(label)
    _valueX = valueX
  }
  onPaint_() {
    var st = style(focused ? "checkbox.focused" : "checkbox")
    Painter.fill(surface, Rect.new(0, 0, bounds.w, 1), " ", st)
    Painter.text(surface, 0, 0, label, st, _valueX - 1)
    Painter.text(surface, _valueX, 0, "[%(value ? glyph("check.on") : " ")]", st, 3)
  }
  handle(event) {
    if (event is MouseEvent && event.event == Mouse.button1Click && bounds.contains(event.startX, event.startY)) parent.focusedIndex = parent.children.indexOf(this)
    return super.handle(event)
  }
}

class DosFormPane is Pane {
  construct new(app, heading, pages, fields, save, dirty) {
    super()
    _app = app
    title = heading
    titleAsBar = false
    focused = true
    _pages = pages
    _fields = fields
    _save = save
    _dirty = dirty
    _page = 0
    _offset = 0
    _capacity = 1
    _pending = {}
    _inputs = []
    _labels = []
    _fieldStart = 0
    _actionsStart = 0
    _fieldLabels = []
    _fieldColumns = []
    _lastField = null
    _lastAction = 0
    _pagingRow = null
    _tabBottom = 0
    _saved = false
    _onFinish = null
    _size = null
    onClose = Fn.new { cancel_() }
    keyHints = [["F1", "Help"], ["F10", "Save"], ["Esc", "Cancel"]]
    helpText = "# %(heading)\n\nTab and Shift-Tab move between the tab bar, fields, and Save/Cancel. " +
        "Arrow keys move within each group; Left/Right never wrap into another row. " +
        "Text inputs retain cursor movement; at either end, Left/Right can cross to an adjacent column. " +
        "Home/End choose the first/last tab. Enter activates a tab or edits a field. " +
        "Click a tab or use Ctrl-PageUp/Down to change categories. " +
        "PageUp/Down and Previous/Next reach additional fields in the same category. " +
        "F10 saves the complete draft. " +
        "Escape cancels; changes are not accepted when moving between pages."
    resize_()
  }

  saved { _saved }
  onFinish=(fn) { _onFinish = fn }
  page { _page }
  controls { children }
  focusGroup_ {
    if (focusedChild is DosFormTab) return 0
    return focusedIndex != null && focusedIndex >= _actionsStart ? 2 : 1
  }

  cycleFocusGroup_(delta) {
    var group = focusGroup_
    if (group == 1 && focusedIndex != null && focusedIndex >= _fieldStart && focusedIndex < _actionsStart) _lastField = _fieldLabels[focusedIndex - _fieldStart]
    if (group == 2) _lastAction = focusedIndex - _actionsStart
    group = (group + delta + 3) % 3
    if (group == 1 && _fieldStart == _actionsStart) group = (group + delta + 3) % 3
    if (group == 0) {
      focusedIndex = _page
    } else if (group == 1) {
      var index = _lastField == null ? -1 : _fieldLabels.indexOf(_lastField)
      focusedIndex = _fieldStart + index.max(0)
    } else {
      focusedIndex = _actionsStart + _lastAction.min(children.count - _actionsStart - 1)
    }
  }

  moveWithinGroup_(key) {
    if (focusedChild == null) return false
    var group = focusGroup_
    var first = group == 0 ? 0 : group == 1 ? _fieldStart : _actionsStart
    var end = group == 0 ? _pages.count : group == 1 ? _actionsStart : children.count
    var horizontal = key == Key.left || key == Key.right
    var direction = key == Key.left || key == Key.up ? -1 : 1
    var current = focusedChild.bounds
    var best = null
    var distance = 0
    var across = 0
    for (i in first...end) {
      if (i == focusedIndex) continue
      var rect = children[i].bounds
      var dx = (rect.x + rect.w / 2) - (current.x + current.w / 2)
      var dy = rect.y - current.y
      if (horizontal && (dy != 0 || dx * direction <= 0)) continue
      if (!horizontal && dy * direction <= 0) continue
      if (!horizontal && group == 1 && _fieldColumns[i - _fieldStart] != _fieldColumns[focusedIndex - _fieldStart]) continue
      var primary = horizontal ? dx.abs : dy.abs
      var secondary = horizontal ? 0 : dx.abs
      if (best == null || primary < distance || (primary == distance && secondary < across)) {
        best = i
        distance = primary
        across = secondary
      }
    }
    if (best == null) return false
    focusedIndex = best
    return true
  }
  choiceAnchor_ {
    var field = focusedChild
    if (!(field is DosFormChoice) || !field.dropdown) return null
    var label = field.label
    // Resolve by field identity after reflow; child indexes can change when
    // overflow navigation appears or disappears.
    return Fn.new {
      for (child in children) {
        if (child is DosFormChoice && child.dropdown && child.label == label) return child.valueBounds_
      }
      return null
    }
  }

  button_(label, action, x, y, width) {
    var b = Button.new(label)
    b.hotkeyIdx = -1
    b.onPress = action
    add(b)
    b.bounds = Rect.new(x, y, width.max(1), 1)
    return b
  }

  resize_() {
    var size = Screen.checked_(Screen.size)
    if (_size != null && size[0] == _size[0] && size[1] == _size[1]) return
    _size = size
    bounds = Rect.new(1, 1, size[0], size[1])
    rebuild_()
  }

  draw() {
    resize_()
    return super.draw()
  }

  rebuild_() {
    var focus = focusedIndex
    var tab = focusedChild is DosFormTab ? focus : null
    var action = focus != null && _actionsStart > 0 && focus >= _actionsStart ? focus - _actionsStart : null
    var field = focus != null && focus >= _fieldStart && focus < _actionsStart ? _fieldLabels[focus - _fieldStart] : null
    for (c in children.toList) remove(c)
    _inputs = []
    _labels = []
    _fieldLabels = []
    _fieldColumns = []
    var w = bounds.w
    var h = bounds.h
    var x = 2
    var y = 2
    for (i in 0..._pages.count) {
      var page = i
      var width = (_pages[i].count + 4).min((w - 2).max(1))
      if (x + width > w) {
        x = 2
        y = y + 1
      }
      var tab = DosFormTab.new(_pages[i], page == _page)
      tab.onPress = Fn.new { selectPage_(page) }
      add(tab)
      tab.bounds = Rect.new(x, y, width, 1)
      x = x + width
    }
    _tabBottom = y
    var top = y + 2
    var columns = w >= 120 ? 2 : 1
    var fields = _fields.call(_page)
    var rows = (h - top - 3).max(1)
    _pagingRow = null
    if (fields.count > rows * columns) {
      _pagingRow = top
      top = top + 1
      rows = (h - top - 3).max(1)
    }
    _capacity = rows * columns
    if (_offset >= fields.count) _offset = 0
    _offset = (_offset / _capacity).floor * _capacity
    var pitch = ((w - 4) / columns).floor.max(1)
    var end = (_offset + _capacity).min(fields.count)
    // Balance short categories across wide screens instead of filling the
    // entire first column before using the second.
    rows = ((end - _offset) / columns).ceil.max(1)
    var labelWidth = 0
    for (f in fields) labelWidth = labelWidth.max(Screen.checked_(Screen.textWidth(f[0], 0)))
    labelWidth = labelWidth.min(((pitch - 2) / 2).floor)
    var valueX = labelWidth + 1
    if (_pagingRow != null) {
      if (_offset > 0) button_(w >= 60 ? "Previous" : "Prev", Fn.new { moveFields_(-1) }, w - 24, _pagingRow, 13).focusable = false
      if (end < fields.count) button_("Next", Fn.new { moveFields_(1) }, w - 10, _pagingRow, 9).focusable = false
    }
    _fieldStart = children.count
    for (i in _offset...end) {
      var f = fields[i]
      var index = i - _offset
      var col = (index / rows).floor
      _fieldLabels.add(f[0])
      _fieldColumns.add(col)
      var left = 3 + col * pitch
      var row = top + (index % rows)
      var width = (pitch - 2).max(1)
      var widget = null
      if (f[1] == "text" || f[1] == "number") {
        widget = DosFormInput.new()
        var input = widget
        var value = _pending.containsKey(f[0]) ? _pending[f[0]] : f[2].call().toString
        input.value = value
        input.maxLen = f[0] == "Name" ? 30 : f[4]
        if (f[5]) input.mask = "*"
        input.onChange = Fn.new {|text| _pending[f[0]] = text }
        input.onSubmit = Fn.new {|text| moveWithinGroup_(Key.down) }
        _inputs.add([input, f])
        _labels.add([left - 1, row - 1, f[0], valueX, width])
      } else if (f[1] == "bool") {
        widget = DosFormCheckbox.new(f[0], valueX)
        widget.value = f[2].call()
        widget.onChange = Fn.new {|value| f[3].call(value) }
      } else {
        var value = f[2].call()
        widget = DosFormChoice.new(f[0], value, valueX, f[1] == "enum")
        widget.onPress = Fn.new {
          if (!flush_()) return
          f[6].call()
          rebuild_()
        }
      }
      widget.helpText = f[9]
      add(widget)
      if (f[1] == "text" || f[1] == "number") {
        widget.bounds = Rect.new(left + valueX + 1, row, (width - valueX - 2).max(1), 1)
      } else {
        widget.bounds = Rect.new(left, row, width, 1)
      }
    }
    var saveLabel = Host.safeMode ? "Apply" : "Save"
    _actionsStart = children.count
    button_(saveLabel, Fn.new { save_() }, 2, h - 1, 8)
    button_("Cancel", Fn.new { cancel_() }, 10, h - 1, 10)
    focusedIndex = _fieldStart
    if (tab != null) focusedIndex = tab
    if (action != null) focusedIndex = _actionsStart + action.min(children.count - _actionsStart - 1)
    if (field != null && _fieldLabels.contains(field)) focusedIndex = _fieldStart + _fieldLabels.indexOf(field)
    markDirty()
  }

  onPaint_() {
    super.onPaint_()
    Painter.hline(surface, 1, _tabBottom, bounds.w - 2, glyph("frame.display.separator"), style("frame"))
    for (label in _labels) {
      Painter.text(surface, label[0], label[1], label[2], style("default"), label[3] - 1)
      Painter.text(surface, label[0] + label[3], label[1], "[", style("frame"), 1)
      Painter.text(surface, label[0] + label[4] - 1, label[1], "]", style("frame"), 1)
    }
    if (_pagingRow != null) {
      var count = _fields.call(_page).count
      var text = "Fields %(_offset + 1)-%((_offset + _capacity).min(count))/%(count)"
      Painter.text(surface, 2, _pagingRow - 1, text, style("statusbar"), bounds.w - 28)
    }
    Painter.hline(surface, 1, bounds.h - 4, bounds.w - 2, glyph("frame.display.separator"), style("frame"))
    Painter.text(surface, 2, bounds.h - 3, "Tab Groups  Arrows Move  Ctrl-PgUp/PgDn Tabs  F1 Help", style("statusbar"), bounds.w - 4)
  }

  flush_() {
    var fields = _fields.call(_page)
    var inputs = _inputs.toList
    // Resizing can move an edited field onto another overflow page. Its
    // pending value still participates in validation and saving.
    for (f in fields) {
      if (_pending.containsKey(f[0]) && !_inputs.any {|pair| pair[1][0] == f[0] }) inputs.add([null, f])
    }
    for (pair in inputs) {
      var input = pair[0]
      var f = pair[1]
      var text = input == null ? _pending[f[0]] : input.value
      var value = text
      var error = null
      if (f[1] == "number") {
        value = Num.fromString(text)
        if (value == null || !value.isInteger || value < f[7] || value > f[8]) {
          error = "Enter an integer from %(f[7]) through %(f[8])."
        }
      } else if (Text.normalizedByteCount(text) > f[4] && text != f[2].call()) {
        error = "The value may contain at most %(f[4]) UTF-8 bytes."
      }
      if (error != null) {
        if (input == null) {
          _offset = fields.indexOf(f)
          focusedIndex = null
          rebuild_()
          input = _inputs.where {|pair| pair[1][0] == f[0] }.toList[0][0]
        }
        focusedIndex = children.indexOf(input)
        Alert.show(_app, f[0], error, f[9])
        return false
      }
      f[3].call(value)
      _pending.remove(f[0])
    }
    return true
  }

  selectPage_(page) {
    if (!flush_()) return
    _page = page
    _lastField = null
    _offset = 0
    focusedIndex = null
    rebuild_()
  }

  moveFields_(delta) {
    if (!flush_()) return
    var count = _fields.call(_page).count
    _offset = (_offset + delta * _capacity).max(0).min(((count - 1).max(0) / _capacity).floor * _capacity)
    focusedIndex = null
    rebuild_()
  }

  save_() {
    if (!flush_()) return
    var accepted = false
    var saving = Fiber.new { accepted = _save.call() }
    saving.try()
    if (saving.error != null) {
      Alert.show(_app, "Save Failed", saving.error.toString)
      return
    }
    if (!accepted) return
    _saved = true
    _app.popModal()
    if (_onFinish != null) _onFinish.call()
  }

  cancel_() {
    var pending = _fields.call(_page).any {|f| _pending.containsKey(f[0]) && _pending[f[0]] != f[2].call().toString }
    if ((_dirty.call() || pending) && !Confirm.show(_app, "Discard changes?",
        "The changes on all pages will be discarded.")) return
    _app.popModal()
    if (_onFinish != null) _onFinish.call()
  }

  handle(event) {
    if (event is KeyEvent) {
      var key = event.shortcut_
      if (key == Key.tab || key == Key.chord_(Key.tab, Mod.shift)) {
        cycleFocusGroup_(key == Key.tab ? 1 : -1)
        return true
      }
      if (focusGroup_ == 0 && (key == Key.home || key == Key.end)) {
        focusedIndex = key == Key.home ? 0 : _pages.count - 1
        return true
      }
      if (key == Key.left || key == Key.right || key == Key.up || key == Key.down) {
        var field = focusedChild
        if (field is DosFormInput && !field.allSelected &&
            ((key == Key.left && field.cursor == 0) || (key == Key.right && field.cursor == field.count))) {
          if (moveWithinGroup_(key)) return true
        }
        if (field != null && field.handle(event)) return true
        moveWithinGroup_(key)
        return true
      }
      if (event.shortcut_ == Key.f10) {
        save_()
        return true
      }
      if (event.shortcut_ == Key.escape) {
        cancel_()
        return true
      }
      if (event.shortcut_ == Key.chord_(Key.pageDown, Mod.control)) {
        selectPage_((_page + 1) % _pages.count)
        return true
      }
      if (event.shortcut_ == Key.chord_(Key.pageUp, Mod.control)) {
        selectPage_((_page + _pages.count - 1) % _pages.count)
        return true
      }
      if (event.shortcut_ == Key.pageDown || event.shortcut_ == Key.pageUp) {
        moveFields_(event.shortcut_ == Key.pageDown ? 1 : -1)
        return true
      }
    }
    return super.handle(event)
  }
}

class DosForms {
  static entryPages_(defaults) {
    return ["Connection", "Terminal", defaults ? "Files" : "Files & Notes"]
  }
  // [label, kind, getter, setter, limit, masked, action, min, max, help]
  static field_(label, kind, get, set, limit, masked, action, min, max, help) {
    return [label, kind, get, set, limit, masked, action, min, max, help]
  }

  static entryPage_(key, label) {
    if (["name", "addr", "connType", "addressFamily", "port", "flowControl", "bpsRate", "stopBits", "dataBits", "parity", "sortOrder"].contains(key)) return 0
    if (["user", "password", "syspass", "sftpPublicKey", "sshAllowAes128Cbc", "sshAcceptEarlyData", "telnetNoBinary", "deferTelnetNegotiation", "tlsTrustWebPki", "tlsTrustedCert", "tlsClientCert", "tlsClientKey", "tlsPskIdentity", "tlsPsk", "tlsPskVersion"].contains(key)) return 0
    if (["dlDir", "ulDir", "logFile", "appendLogFile", "xferLogLevel", "protocolLogLevel", "ctermLogLevel", "comment"].contains(key) || label == "Log Configuration") return 2
    return 1
  }

  static entryFields_(app, profile, draft, defaults, page) {
    var fields = []
    var keys = {"Name": "name", "Connection Type": "connType", "Address Family": "addressFamily", "Flow Control": "flowControl", "Comm Rate": "bpsRate", "Stop Bits": "stopBits", "Data Bits": "dataBits", "Parity": "parity", "TCP Port": "port", "Trust Web PKI": "tlsTrustWebPki", "TLS PSK Version": "tlsPskVersion", "Screen Mode": "screenMode", "Encoding": "encoding", "Font": "font", "Terminal Type": "termName", "Graphics": "graphics", "ANSI Music": "music", "Fake Comm Rate": "bpsRate", "File Transfer Log Level": "xferLogLevel", "Protocol Log Level": "protocolLogLevel", "CTerm Log Level": "ctermLogLevel"}
    var rows = BbsEditor.rows_(app, profile, draft, defaults)
    if (page == 2) {
      rows = rows.where {|row| row[3][0] != "Log Configuration" }.toList
      rows.add(BbsEditor.textRow_(app, draft, "logFile", "Log File", Menu.maxPathLength, false))
      rows.add(BbsEditor.boolRow_(draft, "appendLogFile", "Append Log File"))
      rows.add(BbsEditor.choiceRow_(app, draft, "xferLogLevel", "File Transfer Log Level", Menu.logLevels))
      rows.add(BbsEditor.choiceRow_(app, draft, "protocolLogLevel", "Protocol Log Level", Menu.logLevels))
      rows.add(BbsEditor.choiceRow_(app, draft, "ctermLogLevel", "CTerm Log Level", Menu.logLevels))
    }
    for (row in rows) {
      var meta = row[3]
      var label = meta[0]
      var kind = meta[1]
      var key = meta[2]
      if (key == null && keys.containsKey(label)) key = keys[label]
      if (label == "Fake Comm Rate") {
        if (page != 1) continue
      } else if (entryPage_(key, label) != page) continue
      if (key == "name" || key == "termName") kind = "text"
      if (key == "port") kind = "number"
      if (key == "tlsTrustWebPki") kind = "bool"
      if (["connType", "addressFamily", "flowControl", "bpsRate", "parity", "tlsPskVersion", "screenMode", "encoding", "font", "graphics", "music", "xferLogLevel", "protocolLogLevel", "ctermLogLevel"].contains(key)) kind = "enum"
      var get = Fn.new { meta[5] }
      var set = null
      var limit = meta[3]
      if (kind == "text" || kind == "bool" || kind == "number") {
        get = Fn.new { draft[key] }
        set = Fn.new {|value| draft[key] = value }
      }
      if (key == "tlsTrustWebPki") set = Fn.new {|value|
        if (value != draft[key]) row[1].call()
      }
      if (key == "name") limit = 60
      if (key == "termName") limit = 31
      if (key == "port") limit = 5
      var help = key == null ? BbsEditor.editorHelp_(draft, defaults, true) : BbsEditor.fieldHelp_(key, draft)
      fields.add(field_(label, kind, get, set, limit, meta[4], row[1], key == "port" ? 1 : null, key == "port" ? 65535 : null, help))
    }
    if (page == 0 && !defaults) fields.add(field_("Explicit Sort Order", "number", Fn.new { draft["sortOrder"] }, Fn.new {|v| draft["sortOrder"] = v }, 12, false, null, -2147483648, 2147483647, BbsEditor.explicitSortHelp_()))
    if (page == 2 && !defaults) fields.add(field_("Comment", "text", Fn.new { draft["comment"] }, Fn.new {|v| draft["comment"] = v }, 1023, false, null, null, null, "# Comment\n\nPersonal notes for this connection."))
    return fields
  }

  static entry(app, original, defaults, isNew) {
    return entry_(app, original, defaults, isNew, null, false)
  }

  static offerSave(source, name) {
    var app = App.new()
    app.menuPanels = true
    return entry_(app, source, false, true, name, true) != null
  }

  static entry_(app, original, defaults, isNew, name, standalone) {
    var profile = (original is DirectoryEntry ? original.profile : original).copy()
    var draft = BbsEditor.draft_(original)
    var baseline = BbsEditor.draft_(original)
    if (name != null) draft["name"] = name
    if (isNew && !(original is DirectoryEntry)) draft["sortOrder"] = 2147483647
    var savedEntry = original
    var pane = DosFormPane.new(app, defaults ? "New Connection Defaults" : isNew ? "New Directory Entry" : "Edit Directory Entry",
        entryPages_(defaults),
        Fn.new {|page| entryFields_(app, profile, draft, defaults, page) }, Fn.new {
      if (!defaults && draft["addr"].count == 0) {
        Alert.show(app, "Address Required", "Enter an address, phone number, device, or command on the Connection page.")
        return false
      }
      if (!BbsEditor.validate_(app, draft) || !BbsEditor.update_(app, profile, draft, defaults)) return false
      var error = null
      if (defaults) {
        if (Host.safeMode) return BbsEditor.update_(app, Menu.defaults, draft, true)
        error = Menu.saveDefaults(profile)
      } else if (isNew || original.readOnly) {
        var result = Menu.createFromProfile(draft["name"], profile, draft["sortOrder"])
        if (result is MenuError) {
          error = result
        } else {
          savedEntry = result
        }
      } else {
        error = original.saveDraft(profile, draft["name"], draft["sortOrder"])
      }
      if (error == null) return true
      Alert.show(app, "Save Failed", error.message)
      return false
    }, Fn.new { !BbsEditor.sameDraft_(draft, baseline) })
    if (standalone) {
      pane.atExit = true
      pane.onFinish = Fn.new { app.quit() }
      app.pushModal(pane)
      app.runSync()
    } else {
      app.modal(pane)
    }
    return pane.saved ? savedEntry : null
  }

  static program(app, connected) {
    var s = Menu.settings
    var pane = DosFormPane.new(app, "Program Settings",
        ["General", "Display", "Audio/MIDI", "Paths", "Modem", "Security", "Colours"],
        Fn.new {|page| programFields_(app, s, connected, page) }, Fn.new {
      var error = Host.safeMode ? s.apply() : s.save()
      if (error != null) {
        Alert.show(app, "Program Settings", error.message)
        return false
      }
      app.invalidateCursorState()
      app.theme = Theme.current
      return true
    }, Fn.new { s.dirty })
    app.modal(pane)
    return pane.saved
  }

  static settingHelp_(label) {
    var help = SettingsMenu.settingHelp_(label)
    return help == null ? SettingsMenu.programHelp_(false, true) : help
  }

  static choiceName_(rows, value) {
    if (rows.count > 0 && rows[0] is String) return value >= 0 && value < rows.count ? rows[value] : "Unknown (%(value))"
    return MenuUi.rowName(rows, value)
  }

  static programFields_(app, s, connected, page) {
    var fields = []
    if (page == 0) {
      var get = Fn.new { s.mainUi }
      var set = Fn.new {|value|
        if (value != s.mainUi) s.mainUi = value
      }
      get = Fn.new { choiceName_(Menu.mainUiChoices, s.mainUi) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Main Interface", Menu.mainUiChoices, s.mainUi, "# Main Interface\n\nClassic keeps the original layout. DOS uses a wide directory and full-page forms. The choice applies when Program Settings closes. Custom main-menu script overrides take precedence.")
        if (value != null && value != s.mainUi) s.mainUi = value
      }
      fields.add(field_("Main Interface", "enum", get, set, null, false, choose, null, null, "# Main Interface\n\nClassic keeps the original layout. DOS uses a wide directory and full-page forms. The choice applies when Program Settings closes. Custom main-menu script overrides take precedence."))
    }
    if (page == 0) {
      var get = Fn.new { s.confirmClose }
      var set = Fn.new {|value|
        if (value != s.confirmClose) s.confirmClose = value
      }
      fields.add(field_("Confirm Program Exit", "bool", get, set, null, false, null, null, null, settingHelp_("Confirm Program Exit")))
    }
    if (page == 0) {
      var get = Fn.new { s.promptSave }
      var set = Fn.new {|value|
        if (value != s.promptSave) s.promptSave = value
      }
      fields.add(field_("Prompt to Save", "bool", get, set, null, false, null, null, null, settingHelp_("Prompt to Save")))
    }
    if (page == 0) {
      var get = Fn.new { s.colorEmoji }
      var set = Fn.new {|value|
        if (value != s.colorEmoji) s.colorEmoji = value
      }
      fields.add(field_("Colour Emoji", "bool", get, set, null, false, null, null, null, settingHelp_("Colour Emoji")))
    }
    if (page == 0) {
      var get = Fn.new { s.invertWheel }
      var set = Fn.new {|value|
        if (value != s.invertWheel) s.invertWheel = value
      }
      fields.add(field_("Invert Mouse Wheel", "bool", get, set, null, false, null, null, null, settingHelp_("Invert Mouse Wheel")))
    }
    if (page == 0) {
      var get = Fn.new { s.atariStKeyClicks }
      var set = Fn.new {|value|
        if (value != s.atariStKeyClicks) s.atariStKeyClicks = value
      }
      fields.add(field_("Atari ST Key Clicks", "bool", get, set, null, false, null, null, null, settingHelp_("Atari ST Key Clicks")))
    }
    if (page == 0) {
      var get = Fn.new { s.scrollbackLines }
      var set = Fn.new {|value|
        if (value != s.scrollbackLines) s.scrollbackLines = value
      }
      fields.add(field_("Scrollback Buffer Lines", "number", get, set, 12, false, null, 1, 2147483647, settingHelp_("Scrollback Buffer Lines")))
    }
    if (page == 1) {
      var get = Fn.new { s.startupMode }
      var set = Fn.new {|value|
        if (value != s.startupMode) s.startupMode = value
      }
      get = Fn.new { choiceName_(Menu.screenModeChoices, s.startupMode) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Startup Screen Mode", Menu.screenModeChoices, s.startupMode, settingHelp_("Startup Screen Mode"))
        if (value != null && value != s.startupMode) s.startupMode = value
      }
      fields.add(field_("Startup Screen Mode", "enum", get, set, null, false, choose, null, null, settingHelp_("Startup Screen Mode")))
    }
    if (page == 1) {
      var get = Fn.new { s.outputMode }
      var set = Fn.new {|value|
        if (value != s.outputMode) s.outputMode = value
      }
      get = Fn.new { choiceName_(Menu.outputModes, s.outputMode) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Video Output Mode", Menu.outputModes, s.outputMode, settingHelp_("Video Output Mode"))
        if (value != null && value != s.outputMode) s.outputMode = value
      }
      fields.add(field_("Video Output Mode", "enum", get, set, null, false, choose, null, null, settingHelp_("Video Output Mode")))
    }
    if (page == 1) {
      var get = Fn.new { s.cursorStyle }
      var set = Fn.new {|value|
        if (value != s.cursorStyle) s.cursorStyle = value
      }
      get = Fn.new { choiceName_(Menu.cursorStyles, s.cursorStyle) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Default Cursor Style", Menu.cursorStyles, s.cursorStyle, settingHelp_("Default Cursor Style"))
        if (value != null && value != s.cursorStyle) s.cursorStyle = value
      }
      fields.add(field_("Default Cursor Style", "enum", get, set, null, false, choose, null, null, settingHelp_("Default Cursor Style")))
    }
    if (page == 1) {
      var get = Fn.new { s.scalingMode }
      var set = Fn.new {|value|
        if (value != s.scalingMode) s.scalingMode = value
      }
      get = Fn.new { choiceName_(Menu.scalingModes, s.scalingMode) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Scaling", Menu.scalingModes, s.scalingMode, settingHelp_("Scaling"))
        if (value != null && value != s.scalingMode) s.scalingMode = value
      }
      fields.add(field_("Scaling", "enum", get, set, null, false, choose, null, null, settingHelp_("Scaling")))
    }
    if (page == 1 && !connected) {
      var get = Fn.new { s.customColumns }
      var set = Fn.new {|value|
        if (value != s.customColumns) s.customColumns = value
      }
      fields.add(field_("Custom Columns", "number", get, set, 12, false, null, 40, 255, settingHelp_("Custom Columns")))
    }
    if (page == 1 && !connected) {
      var get = Fn.new { s.customRows }
      var set = Fn.new {|value|
        if (value != s.customRows) s.customRows = value
      }
      fields.add(field_("Custom Rows", "number", get, set, 12, false, null, 14, 255, settingHelp_("Custom Rows")))
    }
    if (page == 1 && !connected) {
      var get = Fn.new { s.customFontHeight }
      var set = Fn.new {|value|
        if (value != s.customFontHeight) s.customFontHeight = value
      }
      fields.add(field_("Custom Font Height", "number", get, set, 12, false, null, 8, 64, settingHelp_("Custom Font Height")))
    }
    if (page == 1 && !connected) {
      var get = Fn.new { s.customAspectWidth }
      var set = Fn.new {|value|
        if (value != s.customAspectWidth) s.customAspectWidth = value
      }
      fields.add(field_("Custom Aspect Width", "number", get, set, 12, false, null, 1, 2147483647, settingHelp_("Custom Aspect Width")))
    }
    if (page == 1 && !connected) {
      var get = Fn.new { s.customAspectHeight }
      var set = Fn.new {|value|
        if (value != s.customAspectHeight) s.customAspectHeight = value
      }
      fields.add(field_("Custom Aspect Height", "number", get, set, 12, false, null, 1, 2147483647, settingHelp_("Custom Aspect Height")))
    }
    if (page == 3) {
      var get = Fn.new { s.listPath }
      var set = Fn.new {|value|
        if (value != s.listPath) s.listPath = value
      }
      fields.add(field_("List Path", "text", get, set, Menu.maxPathLength, false, null, null, null, settingHelp_("List Path")))
    }
    if (page == 3) {
      var get = Fn.new { s.shellTerm }
      var set = Fn.new {|value|
        if (value != s.shellTerm) s.shellTerm = value
      }
      fields.add(field_("TERM For Shell", "text", get, set, 30, false, null, null, null, settingHelp_("TERM For Shell")))
    }
    if (page == 3) {
      var get = Fn.new { s.tlsClientCert }
      var set = Fn.new {|value|
        if (value != s.tlsClientCert) s.tlsClientCert = value
      }
      fields.add(field_("TLS Client Certificate", "text", get, set, Menu.maxPathLength, false, null, null, null, settingHelp_("TLS Client Certificate")))
    }
    if (page == 3) {
      var get = Fn.new { s.tlsClientKey }
      var set = Fn.new {|value|
        if (value != s.tlsClientKey) s.tlsClientKey = value
      }
      fields.add(field_("TLS Client Key", "text", get, set, Menu.maxPathLength, false, null, null, null, settingHelp_("TLS Client Key")))
    }
    if (page == 4) {
      var get = Fn.new { s.modemDevice }
      var set = Fn.new {|value|
        if (value != s.modemDevice) s.modemDevice = value
      }
      fields.add(field_("Modem/Comm Device", "text", get, set, 1024, false, null, null, null, settingHelp_("Modem/Comm Device")))
    }
    if (page == 4) {
      var get = Fn.new { s.modemRate }
      var set = Fn.new {|value|
        if (value != s.modemRate) s.modemRate = value
      }
      get = Fn.new { choiceName_(Menu.serialRates(s.modemDevice), s.modemRate) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Modem/Comm Rate", Menu.serialRates(s.modemDevice), s.modemRate, settingHelp_("Modem/Comm Rate"))
        if (value != null && value != s.modemRate) s.modemRate = value
      }
      fields.add(field_("Modem/Comm Rate", "enum", get, set, null, false, choose, null, null, settingHelp_("Modem/Comm Rate")))
    }
    if (page == 4) {
      var get = Fn.new { s.modemInit }
      var set = Fn.new {|value|
        if (value != s.modemInit) s.modemInit = value
      }
      fields.add(field_("Modem Init String", "text", get, set, 1023, false, null, null, null, settingHelp_("Modem Init String")))
    }
    if (page == 4) {
      var get = Fn.new { s.modemDial }
      var set = Fn.new {|value|
        if (value != s.modemDial) s.modemDial = value
      }
      fields.add(field_("Modem Dial String", "text", get, set, 1023, false, null, null, null, settingHelp_("Modem Dial String")))
    }
    if (page == 5) {
      var get = Fn.new { s.kdfShift }
      var set = Fn.new {|value|
        if (value != s.kdfShift) s.kdfShift = value
      }
      fields.add(field_("Key Derivation Shift", "number", get, set, 12, false, null, 8, 20, settingHelp_("Key Derivation Shift")))
    }
    if (page == 6) {
      var get = Fn.new { s.frameColor }
      var set = Fn.new {|value|
        if (value != s.frameColor) s.frameColor = value
      }
      get = Fn.new { choiceName_(Menu.colors, s.frameColor) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Frame Colour", Menu.colors, s.frameColor, settingHelp_("Frame Colour"))
        if (value != null && value != s.frameColor) s.frameColor = value
      }
      fields.add(field_("Frame Colour", "enum", get, set, null, false, choose, null, null, settingHelp_("Frame Colour")))
    }
    if (page == 6) {
      var get = Fn.new { s.textColor }
      var set = Fn.new {|value|
        if (value != s.textColor) s.textColor = value
      }
      get = Fn.new { choiceName_(Menu.colors, s.textColor) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Text Colour", Menu.colors, s.textColor, settingHelp_("Text Colour"))
        if (value != null && value != s.textColor) s.textColor = value
      }
      fields.add(field_("Text Colour", "enum", get, set, null, false, choose, null, null, settingHelp_("Text Colour")))
    }
    if (page == 6) {
      var get = Fn.new { s.backgroundColor }
      var set = Fn.new {|value|
        if (value != s.backgroundColor) s.backgroundColor = value
      }
      get = Fn.new { choiceName_(Menu.backgroundColors, s.backgroundColor) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Background Colour", Menu.backgroundColors, s.backgroundColor, settingHelp_("Background Colour"))
        if (value != null && value != s.backgroundColor) s.backgroundColor = value
      }
      fields.add(field_("Background Colour", "enum", get, set, null, false, choose, null, null, settingHelp_("Background Colour")))
    }
    if (page == 6) {
      var get = Fn.new { s.inverseColor }
      var set = Fn.new {|value|
        if (value != s.inverseColor) s.inverseColor = value
      }
      get = Fn.new { choiceName_(Menu.backgroundColors, s.inverseColor) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Inverse Colour", Menu.backgroundColors, s.inverseColor, settingHelp_("Inverse Colour"))
        if (value != null && value != s.inverseColor) s.inverseColor = value
      }
      fields.add(field_("Inverse Colour", "enum", get, set, null, false, choose, null, null, settingHelp_("Inverse Colour")))
    }
    if (page == 6) {
      var get = Fn.new { s.lightbarColor }
      var set = Fn.new {|value|
        if (value != s.lightbarColor) s.lightbarColor = value
      }
      get = Fn.new { choiceName_(Menu.colors, s.lightbarColor) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Lightbar Colour", Menu.colors, s.lightbarColor, settingHelp_("Lightbar Colour"))
        if (value != null && value != s.lightbarColor) s.lightbarColor = value
      }
      fields.add(field_("Lightbar Colour", "enum", get, set, null, false, choose, null, null, settingHelp_("Lightbar Colour")))
    }
    if (page == 6) {
      var get = Fn.new { s.lightbarBackgroundColor }
      var set = Fn.new {|value|
        if (value != s.lightbarBackgroundColor) s.lightbarBackgroundColor = value
      }
      get = Fn.new { choiceName_(Menu.backgroundColors, s.lightbarBackgroundColor) }
      var choose = Fn.new {
        var value = MenuUi.choice(app, "Lightbar Background", Menu.backgroundColors, s.lightbarBackgroundColor, settingHelp_("Lightbar Background"))
        if (value != null && value != s.lightbarBackgroundColor) s.lightbarBackgroundColor = value
      }
      fields.add(field_("Lightbar Background", "enum", get, set, null, false, choose, null, null, settingHelp_("Lightbar Background")))
    }
    if (page == 2) {
      for (mode in Menu.audioModes) {
        var bit = mode[0]
        var get = Fn.new { (s.audioModes & bit) != 0 }
        var set = Fn.new {|value|
          var mask = value ? s.audioModes | bit : s.audioModes & ~bit
          if (mask != s.audioModes) s.audioModes = mask
        }
        fields.add(field_("Audio: %(mode[1])", "bool", get, set, null, false, null, null, null, SettingsMenu.audioHelp_()))
      }
      for (mode in Menu.midiModes) {
        var bit = mode[0]
        var get = Fn.new { (s.midiModes & bit) != 0 }
        var set = Fn.new {|value|
          var mask = value ? s.midiModes | bit : s.midiModes & ~bit
          if (mask != s.midiModes) s.midiModes = mask
        }
        fields.add(field_("MIDI: %(mode[1])", "bool", get, set, null, false, null, null, null, "# MIDI\n\nSelect the allowed MIDI backends."))
      }
    }
    return fields
  }
}
