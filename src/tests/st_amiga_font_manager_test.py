#!/usr/bin/env python3
"""Real menu/picker transactions against independent synthetic Amiga files."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'testing'))
from syncterm_fixture import fixture,install_suite
SOURCE='''
import "syncterm" for Host
import "syncterm_menu" for Menu, MenuError, AmigaFontFamily
Host.print("AMIGA-STAGE cancel")
if (Menu.installAmigaFont() != null) Fiber.abort("picker cancellation")
Host.print("AMIGA-STAGE install")
var installed = Menu.installAmigaFont()
if (!(installed is AmigaFontFamily) || installed.name != "Test.font" || installed.source != "installed" || installed.sizes != [8]) Fiber.abort("install result")
Host.print("AMIGA-STAGE replaceCancel")
if (Menu.installAmigaFont() != null) Fiber.abort("replacement cancellation")
Host.print("AMIGA-STAGE replace")
var replacement = Menu.installAmigaFont()
if (!(replacement is AmigaFontFamily)) Fiber.abort("replacement result: %(replacement)")
if (!(Menu.removeAmigaFont(installed) is MenuError)) Fiber.abort("stale family removal")
if (installed.name != "Test.font" || installed.sizes != [8]) Fiber.abort("retained metadata")
if (Menu.removeAmigaFont(replacement) != null) Fiber.abort("family removal")
Host.print("AMIGA-STAGE invalid")
if (!(Menu.installAmigaFont() is MenuError)) Fiber.abort("malformed family accepted")
Host.print("AMIGA-PASS")
'''
# Wren collections use identity equality. Compare their elements explicitly.
SOURCE=SOURCE.replace('installed.sizes != [8]','(installed.sizes.count != 1 || installed.sizes[0] != 8)')

def prepare(root):
    image=bytearray(256)
    def word(at,value):struct.pack_into('>H',image,at,value)
    def long(at,value):struct.pack_into('>I',image,at,value)
    for at,value in ((0,1011),(8,1),(20,55),(24,1001),(28,55),(252,1010)):long(at,value)
    for at,value in ((18,0x0f80),(78,8),(82,8),(84,6),(96,2),(190,8),(192,8),(194,8)):word(32+at,value)
    image[32+90:32+92]=b'AA'
    long(32+92,160);long(32+98,188)
    image[32+160:32+176]=bytes([0x80,0]*8)
    source=root/'downloads'/'Test'
    source.mkdir()
    (source/'8').write_bytes(image)
    descriptor=bytearray(264)
    struct.pack_into('>HH',descriptor,0,0x0f00,1)
    descriptor[4:11]=b'Test/8\0'
    struct.pack_into('>H',descriptor,260,8)
    (root/'downloads'/'Test.font').write_bytes(descriptor)
    install_suite(root,SOURCE,'amiga')

def run(executable,reports):
    with fixture(executable,prepare=prepare,settings='AudioModes=0\n',reports=reports) as (client,root):
        def stage(name):
            return client.wait_for(lambda s: f'AMIGA-STAGE {name}\n'.encode() in client.stderr
                                   and 'Test.font' in s.text)
        stage('cancel');client.key('escape')
        assert not (root/'fonts'/'amiga').exists()
        stage('install');client.key('enter')
        stage('replaceCancel');client.key('enter')
        client.wait_text('Replace Amiga font family');client.key('escape')
        assert (root/'fonts'/'amiga'/'test.font').is_dir()
        stage('replace');client.key('enter')
        client.wait_text('Replace Amiga font family')
        client.text('y')
        stage('invalid')
        (root/'downloads'/'Test.font').write_bytes(b'bad')
        client.key('enter')
        client.wait_for(lambda s:b'AMIGA-PASS\n' in client.stderr)
        assert (root/'downloads'/'Test'/'8').is_file()
        assert not (root/'fonts'/'amiga'/'test.font').exists()
        assert not list((root/'fonts'/'amiga').glob('*.stage'))
        assert not list((root/'fonts'/'amiga').glob('*.backup'))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--reports',default='reports/test-control')
    args=parser.parse_args();run(args.executable,args.reports)
