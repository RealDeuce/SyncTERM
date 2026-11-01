// Auto-loaded on the `connected` event.  When the BBS has the
// "SFTP Public Key" toggle on (bbs->sftp_public_key) and the SFTP
// subsystem negotiated successfully, append the local SSH public
// key to the remote's ~/.ssh/authorized_keys (if not already
// present) so the next connection can authenticate without a
// password.
//
// Idempotent: scans existing lines for the blob substring (matches
// the C-side semantics) before appending.  Quiet on every error
// path — this is a best-effort enhancement, never a connection
// blocker.  CTerm.sftpActive is held true for the duration so
// disconnect waits for us to finish before tearing down the SFTP
// channel.

import "syncterm" for BBS, FileFlag, Host, SFTP, SFTPError
import "sftp_activity" for SftpActivity

class SftpPubkey {
  static run() {
    if (Host.safeMode) return
    if (!BBS.sftpPublicKey) return
    var key = Host.sshPublicKey
    if (!(key is Map)) return
    var algo = key["algo"]
    var blob = key["blob"]
    if (!(algo is String) || !(blob is String)) return
    if (algo.count == 0 || blob.count == 0) return
    Fiber.new { upload_(algo, blob) }.call()
  }

  static upload_(algo, blob) {
    SftpActivity.set("key", true)
    var ready = SFTP.start().await()
    if (ready is SFTPError) {
      SftpActivity.set("key", false)
      return
    }
    var flags = FileFlag.read | FileFlag.write |
                FileFlag.append | FileFlag.creat
    var h = SFTP.open(".ssh/authorized_keys", flags).await()
    if (h is SFTPError) {
      SftpActivity.set("key", false)
      return
    }
    // Read the file (typically a few KB) and look for our blob in
    // any existing line — matches ssh.c key_not_present semantics:
    // if any line contains the blob, treat as already authorized.
    var content = ""
    var off = 0
    while (true) {
      var b = SFTP.read(h, 4096, off).await()
      if (b == null) break
      if (b is SFTPError) break
      if (b.bytes.count == 0) break
      content = content + b
      off = off + b.bytes.count
    }
    var found = false
    for (line in content.split("\n")) {
      if (line.contains(blob)) {
        found = true
        break
      }
    }
    if (!found) {
      // APPEND flag means the server picks the offset; we pass 0
      // and let the underlying open semantics do the right thing.
      var ln = "%(algo) %(blob) Added by SyncTERM\n"
      SFTP.write(h, 0, ln).await()
    }
    SFTP.close(h).await()
    SftpActivity.set("key", false)
  }
}

SftpPubkey.run()
