#!/usr/bin/env python3
"""Quick-connect persistence through the executable and control bitmap backend."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import contextlib
from pathlib import Path
import selectors
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import fixture, listener, write_script, environment


def run(executable, reports, urls, *, save=True, prompt=True, command_line=False,
        alt_h=False, ssh=False, accept=None):
    expected = len(urls) if prompt else 0
    entries = expected if save else 0
    source = r'''
import "syncterm_menu" for Menu
import "syncterm" for Host
class MainMenu {
  static prepare() {
    if (Menu.load(null) != null) Fiber.abort("load failed")
    return true
  }
  static run(current, connected) {
    if (!prepare()) return null
    if (__started == null) {
      __started = 0
      __offers = 0
    }
    if (__started == URLS.count) {
      if (__offers != OFFERS || Menu.entries.count != ENTRIES) Fiber.abort("disconnect offers or entries missing")
      Host.print("QUICK CONNECT PASS\n")
      Menu.quitApplication()
      return null
    }
    var profile = Menu.quickConnect(URLS[__started])
    if (profile == null) Fiber.abort("quick connect parse failed")
    if (SSH) {
      profile.termName = "ansi"
      profile.encoding = 255
      profile.flowControl = 3
    }
    __address = profile.addr
    __port = profile.port
    __started = __started + 1
    return profile
  }
  static offerSave(source, name) {
    if (__offers == null) __offers = 0
    __offers = __offers + 1
    if (source.addr != ADDRESS || source.port != PORT) Fiber.abort("save lost endpoint")
    if (Menu.entries.count != (SAVE ? __offers - 1 : 0)) Fiber.abort("phantom entry")
    if (SSH && source.sshFingerprint.bytes.count != 32) Fiber.abort("missing accepted host key")
    if (!SAVE) {
      Host.print("QUICK CONNECT OFFER\n")
      return false
    }
    var entry = Menu.copy(source, "Saved %(__offers)")
    if (entry == null || entry.profile.addr != source.addr || entry.profile.port != source.port) Fiber.abort("copy lost endpoint")
    if (entry.save() != null) Fiber.abort("save failed")
    Host.print("QUICK CONNECT OFFER\n")
    return true
  }
}
'''
    # URLs in this fixture are generated loopback ASCII values.
    source = source.replace("URLS", str(urls).replace("'", '"'))
    source = source.replace("OFFERS", str(expected)).replace("ENTRIES", str(entries))
    source = source.replace("SAVE", str(save).lower()).replace("SSH", str(ssh).lower())
    address = '"127.0.0.1"' if command_line else "__address"
    port = urls[0].rsplit(":", 1)[1] if command_line else "__port"
    source = source.replace("ADDRESS", address).replace("PORT", port)

    def prepare(root):
        write_script(root, "main_menu", source, "menu")
        write_script(root, "quick_disconnect", '''
import "syncterm" for Hook, Key, Conn
Hook.onKey(Key.f10) {|event|
  Conn.endSession(false)
  return true
}
''', "connected")

    with fixture(executable, reports=reports, prepare=prepare, quiet=False,
                 settings=f"PromptSave={str(prompt).lower()}\n",
                 extra=[urls[0]] if command_line else []) as (client, root):
        for _ in urls:
            peer = accept() if accept else None
            try:
                client.wait_for(lambda s: s.application.get("connected") == 1)
                if alt_h:
                    client.command_key(35)  # evdev H: real default disconnect flow
                    client.wait_text("Disconnect... Are you sure?")
                    client.text("y")
                else:
                    client.key("f10")
                # The next session can already be starting when we capture.
                if peer:
                    peer.settimeout(10)
                    while peer.recv(4096):
                        pass
            finally:
                if peer:
                    peer.close()
        client.process.wait(timeout=15)
        client.close()
        assert client.process.returncode == 0, client.diagnostics()
        assert b"[wren" not in client.stderr, bytes(client.stderr)
        assert client.stderr.count(b"QUICK CONNECT OFFER") == expected, bytes(client.stderr)
        if not command_line:
            assert b"QUICK CONNECT PASS" in client.stderr, bytes(client.stderr)
        saved = (root / "syncterm.lst").read_text()
        assert saved.count("[Saved ") == entries, saved
        if ssh and entries:
            assert "SSHFingerprint" in saved, saved
        print("quick-connect", dict(save=save, prompt=prompt, command_line=command_line,
                                    alt_h=alt_h, ssh=ssh), "passed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--reports", type=Path, default=Path("reports/test-control"))
    parser.add_argument("--ssh-fixture", type=Path)
    args = parser.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    with listener() as server:
        url = f"raw://127.0.0.1:{server.getsockname()[1]}"
        accept = lambda: server.accept()[0]
        for options in ({}, {"save": False}, {"prompt": False}, {"alt_h": True}, {"command_line": True}):
            run(args.executable, args.reports, [url] if options.get("command_line") else [url, url],
                accept=accept, **options)
    if args.ssh_fixture:
        for options in ({"prompt": False}, {"save": False}, {}):
            with contextlib.ExitStack() as stack:
                log = stack.enter_context(open(args.reports / "quick-connect-ssh.log", "wb"))
                server = subprocess.Popen([str(args.ssh_fixture), "none"], stdout=subprocess.PIPE,
                                          stderr=log, env=environment())
                try:
                    with selectors.DefaultSelector() as ready:
                        ready.register(server.stdout, selectors.EVENT_READ)
                        assert ready.select(10), "SSH fixture did not listen"
                    port = int(server.stdout.readline())
                    run(args.executable, args.reports, [f"ssh://guest@127.0.0.1:{port}"], ssh=True, **options)
                finally:
                    if server.poll() is None:
                        server.terminate()
                    server.wait(timeout=5)
                    server.stdout.close()


if __name__ == "__main__":
    main()
