// Audit-only helper tests. No interactive console or evaluated user source.
import "wren_console" for WrenConsole

class MenuTest {
  static run() {
    var pass = 0
    var fail = 0
    var check = Fn.new {|ok, label|
      if (ok) {
        pass = pass + 1
      } else {
        fail = fail + 1
        System.print("  FAIL " + label)
      }
    }
    check.call(WrenConsole.quoteString_("abc\n") == "\"abc\\n\"",
        "ASCII quoting escapes newline")
    check.call(WrenConsole.quoteString_("é") == "\"é\"",
        "quoting preserves Unicode text")
    check.call(WrenConsole.killWord_("abc def") == "abc ",
        "ASCII kill-word retains preceding word")
    check.call(WrenConsole.killWord_("é x") == "é ",
        "kill-word retains preceding multibyte word")
    var received = null
    WrenConsole.register("audit-args", "test", Fn.new {|args| received = args })
    WrenConsole.runCommand_("/audit-args éxy", "main_menu")
    check.call(received == "éxy", "registered command receives full Unicode arguments")
    WrenConsole.unregister("audit-args")
    return [pass, fail]
  }
}
