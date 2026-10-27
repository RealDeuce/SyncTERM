// BBS collection contracts, exercised in the normal application.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
if (true) {
  import "syncterm_menu" for Menu
  if (Menu.load(null) != 0) Fiber.abort("fixture load failed")
  var entry = Menu.entries[0]
  var bad = Fiber.new { entry.palette = [99, "bad"] }
  bad.try()
  if (bad.error == null) Fiber.abort("invalid palette accepted")
  if (entry.palette.count != 2 || entry.palette[0] != 1 || entry.palette[1] != 2 || entry.dirty) Fiber.abort("rejected palette changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  var bad = Fiber.new { entry.wrenScripts = ["x", false] }
  bad.try()
  if (bad.error == null) Fiber.abort("invalid scripts accepted")
  if (entry.wrenScripts.count != 1 || entry.wrenScripts[0] != "original" || entry.dirty) Fiber.abort("rejected scripts changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  var bad = Fiber.new { entry.palette = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }
  bad.try()
  if (bad.error == null || entry.palette.count != 2 || entry.palette[0] != 1 || entry.palette[1] != 2 || entry.dirty) Fiber.abort("oversized palette changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  var huge = "a"
  while (huge.bytes.count <= Menu.wrenScriptsMaxLength) huge = huge + "a"
  var bad = Fiber.new { entry.wrenScripts = [huge] }
  bad.try()
  if (bad.error == null || entry.wrenScripts.count != 1 || entry.wrenScripts[0] != "original" || entry.dirty) Fiber.abort("oversized scripts changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  for (name in ["syncterm_menu", "syncterm_picker", "picker_bootstrap"]) {
    var bad = Fiber.new { entry.wrenScripts = ["ok", name] }
    bad.try()
    if (bad.error == null || entry.wrenScripts[0] != "original" || entry.dirty) Fiber.abort("reserved name changed entry")
  }
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  entry.encoding = 255
  if (!entry.save() || Menu.load(null) != 0 || Menu.entries[0].encoding != 255) Fiber.abort("entry encoding persistence")
  var defaults = Menu.defaults
  defaults.encoding = 12
  if (!defaults.save() || Menu.load(null) != 0 || Menu.defaults.encoding != 12) Fiber.abort("default encoding persistence")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  if (entry.id < 0 || Menu.defaults.id != -1) Fiber.abort("identities")
  var id = entry.id
  entry.connType = 11
  entry.ghostProgram = "program"
  if (entry.password != "program") Fiber.abort("ghost storage")
  entry.password = "edited"
  entry.wrenScripts = ["one", "two", "one"]
  entry.bpsRate = 4294967295
  if (entry.ghostProgram != "edited" || entry.wrenScripts.count != 2 || !entry.save() || entry.id != id) Fiber.abort("save model")
  if (Menu.load(null) != 0 || Menu.entries[0].ghostProgram != "edited" || Menu.entries[0].bpsRate != 4294967295) Fiber.abort("reload model")
}
if (true) {
  import "syncterm_menu" for Menu
  var quick = Menu.quickConnect("ghost://example.invalid/program")
  if (quick.id != -1 || quick.ghostProgram != "program") Fiber.abort("quick GHost")
  var copy = Menu.copy(quick, "ZZGhost")
  if (copy == null || copy.id != -1 || copy.password != "program" || !copy.save() || copy.id < 0) Fiber.abort("copy GHost")
  if (Menu.load(null) != 0) Fiber.abort("reload GHost")
  copy = Menu.entries.where {|entry| entry.name == "ZZGhost" }.toList[0]
  if (copy.ghostProgram != "program" || !copy.delete()) Fiber.abort("persist GHost")
}
if (true) {
  import "syncterm_menu" for Menu
  var encoding = Menu.sortFields.where {|row| row[0] == 32 }.toList
  if (encoding.count != 1 || encoding[0][1] != "Encoding" || encoding[0][2]) Fiber.abort("encoding sort catalog")
  var index = Menu.sortProfiles.count
  if (!Menu.addSortProfile(index, "Encoding", [32, 1])) Fiber.abort("encoding sort add")
  if (!Menu.setActiveSortProfile(index)) Fiber.abort("encoding sort select")
  if (!Menu.updateSortProfile(index, "Encoding", [-32, 1]) || Menu.sortProfiles[index][1][0] != -32) Fiber.abort("encoding sort reverse")
  var all = Menu.sortFields.where {|row| row[0] != 30 }.map {|row| row[0] }.toList
  if (all.count != 31 || !Menu.updateSortProfile(index, "Encoding", all)) Fiber.abort("all sort fields")
  if (!Menu.deleteSortProfile(index)) Fiber.abort("encoding sort cleanup")
}
