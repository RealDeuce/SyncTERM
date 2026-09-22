// Main-menu helper checks in an isolated, empty headless host.
import "main_menu" for MainMenuApp

class MenuTest {
  static run() {
    var app = MainMenuApp.new(null, false)
    var pass = 0
    var fail = 0
    var checks = [
      ["bbs.example", ["bbs.example", null]],
      ["bbs.example:2323", ["bbs.example", 2323]],
      ["bbs.example:1", ["bbs.example", 1]],
      ["bbs.example:65535", ["bbs.example", 65535]],
      ["bbs.example:0", null],
      ["bbs.example:65536", null],
      ["bbs.example:", null],
      ["bbs.example:abc", null],
      ["bbs.example:+23", null],
      ["bbs.example:23.0", null],
      ["bbs.example:2e1", null],
      ["::1", ["::1", null]],
      ["2001:db8::1", ["2001:db8::1", null]],
      ["é.example:23", ["é.example", 23]]
    ]
    for (check in checks) {
      var actual = null
      var fiber = Fiber.new { actual = app.splitAddressPort_(check[0]) }
      fiber.try()
      var expected = check[1]
      var matches = actual == null && expected == null
      if (actual != null && expected != null) {
        matches = actual[0] == expected[0] && actual[1] == expected[1]
      }
      if (fiber.error == null && matches) {
        pass = pass + 1
      } else {
        fail = fail + 1
        System.print("  FAIL address split: " + check[0])
      }
    }
    return [pass, fail]
  }
}
