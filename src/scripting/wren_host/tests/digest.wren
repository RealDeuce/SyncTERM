// Independent byte vectors; expected results are pinned, never computed by the host under test.
import "syncterm" for Digest, DigestError, DigestErr, Error, FileError, File, Host, IOErr
class DigestChecks {
  static full=(value) { __full = value }
  static errors { __errors }
  static hex(bytes) {
    var digits = "0123456789abcdef"
    return bytes.bytes.map {|b| digits[(b / 16).floor] + digits[b % 16] }.join()
  }
  static rejects(action) {
    var e = Fiber.new { action.call() }.try()
    if (!(e is String)) Fiber.abort("expected programmer error: %(e)")
  }
  static check(value, algorithm, expected) {
    if (!__full && algorithm != Digest.sha256) {
      if (!(value is DigestError) || value.code != DigestErr.disabled) Fiber.abort("disabled algorithm")
    } else {
      if (!(value is String) || value.bytes.count != Digest.size(algorithm) || hex(value) != expected) Fiber.abort("digest vector: %(algorithm), %(value)")
    }
  }
  static fail(value, code) {
    if (!(value is DigestError) || !(value is Error) || value.code != code || value.message == null || !value.toString.startsWith("DigestError")) Fiber.abort("native digest status: %(code), %(value)")
    rejects(Fn.new { value.code = 0 })
    rejects(Fn.new { value.message = "changed" })
    __errors.add([value, code, value.message])
  }
  static bytes() {
    __errors = []
    for (pair in [[Digest.sha256, 1], [Digest.sha384, 2], [Digest.sha512, 3], [Digest.md5, 4], [Digest.sha1, 5]]) {
      if (pair[0] != pair[1]) Fiber.abort("native digest enum")
    }
    if (DigestErr.failed != -1 || DigestErr.disabled != -5) Fiber.abort("digest status constants")
    check(Digest.hash(Digest.sha256, ""), Digest.sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    check(Digest.hash(Digest.sha256, "abc"), Digest.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    check(Digest.hash(Digest.sha256, "\x00\xff\xc3\x28\x80"), Digest.sha256, "539691108a9ce02ddd44b3935564cd5d771d8365b3206f200c6ebb896e738bec")
    check(Digest.hash(Digest.sha256, "é"), Digest.sha256, "4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c")
    check(Digest.hash(Digest.sha256, "e\u0301"), Digest.sha256, "bf12767b0f2a56b2190075bae8169f656e3ce8d6357d4aff184bc6c7ea48f9f6")
    check(Digest.hash(Digest.sha384, ""), Digest.sha384, "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b")
    check(Digest.hash(Digest.sha384, "abc"), Digest.sha384, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7")
    check(Digest.hash(Digest.sha384, "\x00\xff\xc3\x28\x80"), Digest.sha384, "9673dacf7ae5a20b05c4cf76e16f553e70d1b120a7874a63e680e5da66db09ef4f5083a9b25f9e36b03faabdf71547c5")
    check(Digest.hash(Digest.sha384, "é"), Digest.sha384, "41a2ddb34dca0b400fa9b73a368307acbed758e85ce50527d1655a263a10e20eefa18f8116767a128877a97c11160a26")
    check(Digest.hash(Digest.sha384, "e\u0301"), Digest.sha384, "2cdfc954d7a8196d23976d00454d78a0cb675238261fc52251e7863ee76a329085429b2f7e57894ce50e42c02941cc2d")
    check(Digest.hash(Digest.sha512, ""), Digest.sha512, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e")
    check(Digest.hash(Digest.sha512, "abc"), Digest.sha512, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")
    check(Digest.hash(Digest.sha512, "\x00\xff\xc3\x28\x80"), Digest.sha512, "8b41cec1ae8f900fdf0966fb2583709b435319e95f966148de6bcfc6baa8c5b9345dd0e8036ba5cf0b047fb894b3761d70c4d48c0de7e9f1efc7900c9b8e0916")
    check(Digest.hash(Digest.sha512, "é"), Digest.sha512, "9e2ad28633f24451bd4f3c1cb20586a21a44c3aeedbdc01b9cc8fa72917ea7bd689c82b8bf1fef89b911cf8cc46fa2c1ccc10087b2094fd4d3350ecd88526a2c")
    check(Digest.hash(Digest.sha512, "e\u0301"), Digest.sha512, "200a3419436902c073b8169ea08adfc49a55194f71c8194e85606cda3b4133dbf5a4851cdab67febe5315cc7fadc10a85050ee5b3b41afd07d584d2dfae0a39a")
    check(Digest.hash(Digest.md5, ""), Digest.md5, "d41d8cd98f00b204e9800998ecf8427e")
    check(Digest.hash(Digest.md5, "abc"), Digest.md5, "900150983cd24fb0d6963f7d28e17f72")
    check(Digest.hash(Digest.md5, "\x00\xff\xc3\x28\x80"), Digest.md5, "ce7e897c98858de668ad94f76438176e")
    check(Digest.hash(Digest.md5, "é"), Digest.md5, "66ddcd97cfdeabb2f6fb8a999b4bc76f")
    check(Digest.hash(Digest.md5, "e\u0301"), Digest.md5, "5526861fbb1e71a1bda6ac364310a807")
    check(Digest.hash(Digest.sha1, ""), Digest.sha1, "da39a3ee5e6b4b0d3255bfef95601890afd80709")
    check(Digest.hash(Digest.sha1, "abc"), Digest.sha1, "a9993e364706816aba3e25717850c26c9cd0d89d")
    check(Digest.hash(Digest.sha1, "\x00\xff\xc3\x28\x80"), Digest.sha1, "a8fe0e9b5edbabd12bf1be0faab3bbb3ff315c6f")
    check(Digest.hash(Digest.sha1, "é"), Digest.sha1, "bf15be717ac1b080b4f1c456692825891ff5073d")
    check(Digest.hash(Digest.sha1, "e\u0301"), Digest.sha1, "7e3fbcf1b6a69221cd8eef7442ef3b51a253bb49")
    for (algorithm in [null, true, "sha256", 0, -1, 6, 1.5, 0/0, 1/0, 9007199254740992]) {
      rejects(Fn.new { Digest.size(algorithm) })
      rejects(Fn.new { Digest.hash(algorithm, "abc") })
    }
    for (bytes in [null, true, 42, [], {}]) rejects(Fn.new { Digest.hash(Digest.sha256, bytes) })
    if ([Digest.size(Digest.md5), Digest.size(Digest.sha1), Digest.size(Digest.sha256), Digest.size(Digest.sha384), Digest.size(Digest.sha512)].join(",") != "16,20,32,48,64") Fiber.abort("digest sizes")
  }
  static file { __file }
  static granted=(value) { __granted = value }
  static granted { __granted }
  static files() {
    var directory = Host.downloadDir
    directory.delete("digest.bin")
    __file = directory.create("digest.bin")
    if (!(__file is File)) Fiber.abort("file setup")
    check(__file.digest(Digest.sha256), Digest.sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    check(__file.digest(Digest.sha384), Digest.sha384, "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b")
    check(__file.digest(Digest.sha512), Digest.sha512, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e")
    check(__file.digest(Digest.md5), Digest.md5, "d41d8cd98f00b204e9800998ecf8427e")
    check(__file.digest(Digest.sha1), Digest.sha1, "da39a3ee5e6b4b0d3255bfef95601890afd80709")
    if (__file.open() is Error || __file.writeBytes("abc") is Error) Fiber.abort("write setup")
    check(__file.digest(Digest.sha256), Digest.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    if (__file.offset != 3) Fiber.abort("pending-write flush changed position")
    __file.offset = 1
    check(__file.digest(Digest.sha256), Digest.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    check(__file.digest(Digest.sha384), Digest.sha384, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7")
    check(__file.digest(Digest.sha512), Digest.sha512, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")
    check(__file.digest(Digest.md5), Digest.md5, "900150983cd24fb0d6963f7d28e17f72")
    check(__file.digest(Digest.sha1), Digest.sha1, "a9993e364706816aba3e25717850c26c9cd0d89d")
    if (__file.offset != 1 || __file.readBytes(1) != "b") Fiber.abort("digest moved stream offset")
    for (algorithm in [null, false, "sha1", 0, 6, 1.5, 0/0, 1/0]) rejects(Fn.new { __file.digest(algorithm) })
    rejects(Fn.new { __file.sha1 })
    rejects(Fn.new { __file.md5 })
    if (__file.close() is Error || directory.delete("digest.bin") is Error) Fiber.abort("file close/delete")
    var absent = __file.digest(Digest.sha256)
    if (!(absent is FileError) || absent.code != IOErr.notFound) Fiber.abort("missing pathname")
    var replacement = directory.create("digest.bin")
    check(__file.digest(Digest.sha256), Digest.sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
  }
  static binaryFile() {
    check(__file.digest(Digest.sha256), Digest.sha256, "7daca2095d0438260fa849183dfc67faa459fdf4936e1bc91eec6b281b27e4c2")
    check(__file.digest(Digest.sha384), Digest.sha384, "250709c010f8b4554564bc9a26da8cde3470e946d3af85ee7c6f672e4de023725881dd1a816ea377dbfdea65d7fb182a")
    check(__file.digest(Digest.sha512), Digest.sha512, "76a59ba2dd234dfb4136e2e33a7e3b344d82f4885a17e3b297eab9a5ded81043292217b8126b1cfba29170dce2780259dc68ab4f382efe91aa4bb404912741f4")
    check(__file.digest(Digest.md5), Digest.md5, "8f1445bafe2c2095044af7789462f475")
    check(__file.digest(Digest.sha1), Digest.sha1, "f04977267a391b2c8f7ad8e070f149bc19b0fc25")
  }
  static lifetime() {
    System.gc()
    for (saved in __errors) {
      var e = saved[0]
      if (e.code != saved[1] || e.message != saved[2] || !e.toString.startsWith("DigestError")) Fiber.abort("digest error lifetime")
    }
  }
}
