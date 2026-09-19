// Audit-only main-menu helper checks in an isolated, empty headless host.
import "main_menu" for MainMenuApp

class MenuTest {
  static run() {
    var app = MainMenuApp.new(null, false)
    var pass = 0
    var fail = 0
    var checks = [
      ["bbs.example:2323", "bbs.example", 2323],
      ["bbs.example:1", "bbs.example", 1],
      ["bbs.example:65535", "bbs.example", 65535],
      ["bbs.example:0", "bbs.example:0", null],
      ["bbs.example:65536", "bbs.example:65536", null],
      ["bbs.example:", "bbs.example:", null],
      ["::1", "::1", null],
      ["2001:db8::1", "2001:db8::1", null],
      ["é.example:23", "é.example", 23]
    ]
    for (check in checks) {
      var actual = null
      var fiber = Fiber.new { actual = app.splitAddressPort_(check[0]) }
      fiber.try()
      if (fiber.error == null && actual[0] == check[1] && actual[1] == check[2]) {
        pass = pass + 1
      } else {
        fail = fail + 1
        System.print("  FAIL address split: " + check[0])
      }
    }
    return [pass, fail]
  }
}
