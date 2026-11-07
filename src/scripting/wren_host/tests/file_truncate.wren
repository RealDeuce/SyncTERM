// Independent byte and stream expectations for File.truncate.
import "syncterm" for File, FileError, Error, Host, IOErr

class TruncateChecks {
  static file { __file }
  static granted=(value) { __granted = value }
  static granted { __granted }
  static check(value) {
    if (!value) Fiber.abort("File.truncate contract")
  }
  static rejects(action) {
    var fiber = Fiber.new { action.call() }
    fiber.try()
    check(fiber.error != null)
  }
  static setup() {
    if (__file is File && __file.isOpen) __file.close()
    Host.downloadDir.delete("truncate.bin")
    __file = Host.downloadDir.create("truncate.bin")
    check(__file is File)
    check(__file.open() == null)
    check(__file.writeBytes("abcdef") == null)
    __file.offset = 3
    __errors = []
  }
  static successes() {
    // A read-ahead buffer must not return the removed tail.
    __file.offset = 0
    check(__file.readBytes(1) == "a")
    check(__file.truncate(2) == null && __file.offset == 1)
    check(__file.readBytes(10) == "b" && __file.readBytes(1) == null)
    // Resizing after EOF clears stale EOF/buffer state, retaining position.
    check(__file.truncate(5) == null && __file.offset == 2)
    check(__file.readBytes(10) == "\x00\x00\x00")
    __file.offset = 9
    check(__file.truncate(1) == null && __file.offset == 9)
    check(__file.readBytes(1) == null)
    check(__file.truncate(11) == null && __file.offset == 9)
    check(__file.readBytes(2) == "\x00\x00")
    // Buffered output is committed before truncation, not flushed over it later.
    check(__file.write("abcdef") == null)
    check(__file.truncate(2) == null && __file.offset == 6)
    check(__file.close() == null && __file.open() == null)
    check(__file.read() == "ab")
    check(__file.write("\x00\xff\xc3\x28\x80") == null)
    check(__file.truncate(3) == null && __file.offset == 5)
    check(__file.read() == "\x00\xff\xc3")
    check(__file.truncate(3) == null && __file.offset == 3)
    check(__file.writeBytes("Z") == null && __file.read() == "\x00\xff\xc3Z")
    check(__file.truncate(0) == null && __file.offset == 4)
    check(__file.read() == "" && __file.size == 0)
    check(__file.close() == null)
    rejects(Fn.new { __file.truncate(0) })
    setup()
  }
  static invalid() {
    for (size in [null, false, "1", [], {}, File, -1, 0.5, Num.nan, Num.infinity, -Num.infinity, 9007199254740992]) {
      rejects(Fn.new { __file.truncate(size) })
    }
    rejects(Fn.new { File.new().truncate(0) })
  }
  static intact() {
    check(__file.isOpen && __file.offset == 3 && __file.size == 6)
    check(__file.readBytes(6, 0) == "abcdef" && __file.offset == 3)
  }
  static fail(size, code, nativeError) {
    var error = __file.truncate(size)
    check(error is FileError && error is Error)
    check(error.code == code && error.errno == nativeError && error.message is String)
    check(error.toString.startsWith("FileError"))
    rejects(Fn.new { error.code = 0 })
    rejects(Fn.new { error.errno = 0 })
    rejects(Fn.new { error.message = "changed" })
    __errors.add([error, code, nativeError, error.message])
  }
  static errorsSurvive() {
    System.gc()
    for (record in __errors) {
      check(record[0].code == record[1] && record[0].errno == record[2] && record[0].message == record[3])
    }
  }
  static denied() { rejects(Fn.new { __granted.truncate(0) }) }
  static grantOpen() { check(__granted.open() == null) }
  static grantClose() { check(__granted.close() == null) }
  static consent() {
    check(__granted.writeBytes("abcdef") == null)
    check(__granted.truncate(2) == null && __granted.offset == 6)
    check(__granted.size == 2)
    grantClose()
    rejects(Fn.new { __granted.open() })
    denied()
  }
  static openStale() {
    check(__granted.truncate(1) == null)
    grantClose()
    rejects(Fn.new { __granted.open() })
  }
  static collected() {
    Host.downloadDir.delete("collected.bin")
    var file = Host.downloadDir.create("collected.bin")
    check(file.open() == null && file.truncate(1) == null)
  }
  static keepOpen(name) {
    check(__file.close() == null)
    Host.downloadDir.delete(name)
    __file = Host.downloadDir.create(name)
    check(__file.open() == null && __file.truncate(1) == null)
  }
}
