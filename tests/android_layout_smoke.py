"""Screen/cutout QA on a dedicated Android emulator, with live Activity resizing.

Run only on a disposable test emulator: temporarily changes display settings and
restores defaults in finally. The installed, signed release APK is exercised.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import time
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--adb', required=True)
p.add_argument('--device', default='emulator-5556')
p.add_argument('--artifacts', type=Path, required=True)
p.add_argument('--case', help='Run one named display configuration')
a = p.parse_args()
adb = [a.adb, '-s', a.device]
a.artifacts.mkdir(parents=True, exist_ok=True)
prefix = 'com.android.internal.display.cutout.emulation.'
routes = ['任务队列', '高级处理', '设置', '运行日志']
passed = 0

def shell(*args):
    return subprocess.check_output([*adb, 'shell', *map(str, args)], timeout=25)

def snapshot():
    shell('uiautomator', 'dump', '/sdcard/mb-layout.xml')
    return ET.fromstring(shell('cat', '/sdcard/mb-layout.xml'))

def matching(tree, text):
    return [n for n in tree.iter('node') if text in (n.get('text'), n.get('content-desc'))]

def bounds(node):
    return tuple(map(int, re.findall(r'-?\d+', node.get('bounds'))))

def tap(node):
    x1, y1, x2, y2 = bounds(node)
    shell('input', 'tap', (x1+x2)//2, (y1+y2)//2)

def ready():
    for _ in range(12):
        tree = snapshot()
        if matching(tree, '开始处理'):
            return tree
        time.sleep(.5)
    raise AssertionError('Application did not display queue controls')

def screen(name, expected=None):
    png = subprocess.check_output([*adb, 'exec-out', 'screencap', '-p'], timeout=20)
    assert png[:8] == b'\x89PNG\r\n\x1a\n'
    if expected:
        assert struct.unpack('!II', png[16:24]) == expected, (name, 'display rotation/size mismatch')
    (a.artifacts/(name+'.png')).write_bytes(png)

def reset():
    for kind in ('corner', 'hole'):
        shell('cmd', 'overlay', 'disable', prefix+kind)
    shell('wm', 'size', 'reset')
    shell('wm', 'density', 'reset')
    shell('settings', 'put', 'system', 'accelerometer_rotation', '0')
    shell('settings', 'put', 'system', 'user_rotation', '0')

# Pixel sizes and densities produce small, narrow, square and large dp windows.
cases = [
    ('phone', '1080x1920', 420, 0, None),
    ('small', '720x960', 360, 0, None),
    ('fold-cover', '800x1900', 400, 0, None),
    ('fold-inner', '1600x1600', 360, 0, None),
    ('tablet', '1600x2560', 320, 0, None),
    ('corner-portrait', '1080x1920', 420, 0, 'corner'),
    ('corner-landscape-left', '1080x1920', 420, 1, 'corner'),
    ('corner-landscape-right', '1080x1920', 420, 3, 'corner'),
    ('hole-portrait', '1080x2160', 420, 0, 'hole'),
]
if a.case:
    cases = [case for case in cases if case[0] == a.case]
    assert cases, 'Unknown display configuration'
try:
    shell('am', 'force-stop', 'org.mediabridge.converter')
    reset()
    shell('am', 'start', '-n', 'org.mediabridge.converter/org.qtproject.qt.android.bindings.QtActivity')
    ready()
    previous_cutout = None
    for name, size, density, rotation, cutout in cases:
        # Android resource overlays force Activity recreation. Establish each
        # hardware cutout before cold start; rotate/resize live within that case.
        cutout_changed = previous_cutout != cutout
        if cutout_changed:
            shell('am', 'force-stop', 'org.mediabridge.converter')
            for kind in ('corner', 'hole'):
                shell('cmd', 'overlay', 'disable', prefix+kind)
            if cutout:
                shell('cmd', 'overlay', 'enable-exclusive', '--category', prefix+cutout)
        shell('wm', 'size', size)
        shell('wm', 'density', density)
        shell('settings', 'put', 'system', 'user_rotation', rotation)
        if cutout_changed:
            shell('am', 'start', '-n', 'org.mediabridge.converter/org.qtproject.qt.android.bindings.QtActivity')
        previous_cutout = cutout
        time.sleep(1.5)
        tree = ready()
        # The emulator can restore its prior user rotation during Qt cold start.
        # Request the intended rotation after startup and verify the PNG extent.
        shell('settings', 'put', 'system', 'user_rotation', rotation)
        time.sleep(1)
        tree = ready()
        expected = tuple(map(int, size.split('x')))
        if rotation in (1, 3):
            expected = expected[::-1]
        for route in routes:
            candidates = [n for n in matching(tree, route) if n.get('clickable') == 'true']
            assert candidates, (name, route, 'navigation missing')
            tap(candidates[0])
            time.sleep(.2)
            tree = ready()
            assert len(matching(tree, route)) >= 2, (name, route, 'page heading did not change')
            start = matching(tree, '开始处理')[0]
            x1, y1, x2, y2 = bounds(start)
            assert x1 >= 0 and y1 > 0 and x2-x1 >= density/160*72 and y2-y1 >= density/160*39, (name, bounds(start))
            assert any(n.get('clickable') == 'true' for n in matching(tree, route)), (name, route)
            passed += 1
            if route == '任务队列':
                screen(name, expected)
            if route == '高级处理' and 'landscape' in name:
                screen(name+'-processing')
        print('PASS Android live display', name, flush=True)
    print(f'{passed} Android navigation/control checks across {len(cases)} display configurations', flush=True)
finally:
    shell('am', 'force-stop', 'org.mediabridge.converter')
    reset()
    shell('am', 'start', '-n', 'org.mediabridge.converter/org.qtproject.qt.android.bindings.QtActivity')
    ready()
    shell('settings', 'put', 'system', 'user_rotation', '0')
