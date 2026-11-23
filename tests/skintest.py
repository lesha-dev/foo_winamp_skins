#!/usr/bin/env python3
"""skintest: regression and Winamp-fidelity tests for foo_winamp_skins over a selected list of skins.

Runs the same from WSL (python3) and Windows (py -3). The skin list and accepted numbers are in
../skintest/skins.txt, next to the skin art (goldens, captures), outside the repo. See BUILD.md ("Skin tests") for the
workflow. Exit codes: 0 everything passed, 1 a check failed, 2 a setup problem.

  skintest.py check [skin ...] [--fb2k x64|x86|both] [--no-batch] [-v]
  skintest.py fb2k [x64|x86|both] [skin ...]       foobar2000 checks only
  skintest.py add <skin> ... [--wait secs]          capture in Winamp, add as pending, check
  skintest.py recapture [skin ...] [--wait secs]    capture again in Winamp, check
  skintest.py accept <skin ...|--all> [--gate golden,winamp,batch,fb2k] [--note text]
  skintest.py show <skin> [window] [--gate g]       zoomed crops of the differences of the last run
  skintest.py list
  skintest.py stabilize [skin ...] [--arch x64] [--runs 3]  mask what changes between foobar2000 runs by itself
  skintest.py fb2k-setup [x64|x86|both] [--from <profile dir>]
"""
import argparse
import json
import math
import ntpath
import os
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
import time
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngimg  # noqa: E402

TESTS = Path(__file__).resolve().parent
REPO = TESTS.parent
WS = REPO.parent  # the workspace: skins, captures, Winamp, foobar2000 test installs
DATA = Path(os.environ.get('SKINTEST_DATA', WS / 'skintest'))  # goldens, test installs, runs (skin art: not in git)
CAPTURES = DATA / 'winamp_captures'
SKIN_DIRS = {'modern': WS / 'skins_modern', 'classic': WS / 'skins_classic'}
WINAMP = WS / 'winamp-portable'
TRACKS = Path(os.environ.get('SKINTEST_TRACKS', WS / 'vis-test'))  # the first four .mp3 files: the "tracks" state
BUILD = REPO / 'unix' / 'build-x64'
MANIFEST = DATA / 'skins.txt'
BATCH_DIR = DATA / 'batch'  # accepted harness reports (they quote skin text: kept out of the repo)
GOLDENS = DATA / 'goldens'
FB2K_GOLDENS = DATA / 'fb2k_goldens'
RUNS = DATA / 'runs'
LAST = RUNS / 'last.json'
DIFFS = RUNS / 'diffs'  # [<skin>]_<window>_<src>_<dst>.png: current | reference | difference, only where they differ
PLAYLIST_DIFFS = DIFFS / 'playlists'  # the same for modern skins' playlists against Winamp's: not checked, for a look
PLAYLIST_SHEETS = []  # the ones this run wrote

IS_WIN = os.name == 'nt'
GATES = ('golden', 'winamp', 'batch', 'fb2k')
ARCHS = {'x64': ('foobar2000_x64', 'user-components-x64', 'x64'), 'x86': ('foobar2000_x32', 'user-components', 'Win32')}
# foobar2000's core setting for the selected user interface (a GUID blob) and our two UIs' GUIDs (src/common/guids.h)
FB2K_UI_VAR = 'cfg_var.094A28DA-C6E8-4392-BAF1-AD0F29E5B303'
# cfg_modern_store (src/common/guids.h): per-skin state of the modern UI - open windows, positions, layouts, the
# skins' own script data (e.g. "first start done"). Left out of test profiles: every skin starts as on its first
# use, as Winamp's captures do (winamp_capture restores clean Settings).
MODERN_STORE_VAR = 'cfg_var.754669DE-96CF-4CE6-AE24-F91D478EC1D6'
AUTOSCROLL_VAR = 'cfg_var.5B0E7C2A-9D41-4F3B-8E62-1A7C9D30B4E5'  # cfg_autoscroll
# Our settings that match a Winamp.ini [Winamp] value (src/common/config.cpp <- guids.h), set in the test profiles
# from Winamp's Settings.clean so both programs show the same: (our cfg_var, Winamp key, Winamp value -> ours)
WINAMP_SETTINGS = [
    ('cfg_var.1F6EB506-B51F-47E3-800E-3DE1BB3195D4', 'bifont', int),                        # title in the skin's bitmap font
    ('cfg_var.1E770573-31BE-477E-8008-186C79850F2B', 'bifont_alt', int),
    ('cfg_var.2F94C1D7-8B3E-4A60-B1C2-D58E7A0346F9', 'custom_plfont', lambda x: 1 - int(x)),     # playlist font: the skin's or the custom one
    ('cfg_var.B7E2093A-4C61-4D8E-9F05-6A1D3E8C27B1', 'pe_fontsize', int),
    ('cfg_var.DA40491A-0478-4424-B50B-DA6FF05BF627', 'pe_width', lambda x: int(x) - 275),            # playlist size beyond 275x116
    ('cfg_var.D758F65C-82CA-4EBA-BD1E-8010E719323A', 'pe_height', lambda x: int(x) - 116),
    ('cfg_var.E78B1856-403C-44AD-8DFA-640B013663CE', 'timeleftmode', int),
    ('cfg_var.7F8A4DEC-A90E-468F-80C2-C959E61BBC45', 'dsize', int),
]
PL_FONT_VAR = 'cfg_var.E05A7B42-1D9C-4F36-8E7B-3C2A61F9D850'  # playlist_custom_font
EQ_VALUES_VAR = 'cfg_var.99DB8825-5EC7-4740-B293-CF0643FF38FF'  # eq_values: "preamp,b1..b10", 0..100 (50 = 0 dB)
# pl_font_smooth: on, as Winamp always draws the classic playlist (draw.cpp: DRAFT_QUALITY, the system's smoothing)
PL_FONT_SMOOTH_VAR = 'cfg_var.6D1F84B3-A25C-4E97-8B10-F43C2E7D5A68'


def eq_values(ini):
    """Winamp's EQ (preamp, eq_data: levels 0..63, 0 = +12 dB) as our eq_values (modern_ui.cpp from_wa_level)."""
    levels = [ini.get('preamp', '31')] + ini.get('eq_data', ','.join(['31'] * 10)).split(',')[:10]
    return ','.join(str(100 - (int(l) * 100 + 62) // 63) for l in levels)
FORMAT_VARS = ('cfg_var.85F29194-3EA4-434A-8835-D5395C4EA5F7', 'cfg_var.D64347C5-8AC9-4054-BA28-A0AA4F2BF3E0')  # main, playlist
# The title as Winamp shows it: classic main window "<n>. <artist> - <title> (<length>)  ***  " (our default format,
# None), modern skins' song title "<artist> - <title>"
MAIN_TITLE_FORMAT = {'classic': None, 'modern': '[%artist% - ]%title%'}
UI_GUIDS = {'classic': 'd9066571-6646-4cff-be8d-7a01998ab531', 'modern': 'd72b376d-2506-4cd0-b1f5-1490fef21424'}
# what a test profile keeps of the profile it's made from (settings only: no playlists, library, other components)
PROFILE_KEEP = ('config.sqlite', 'configuration', 'theme.fth', 'version.txt', 'config.fb2k-dsp')


class SetupError(Exception):
    pass


# --- paths and running the Windows tools ------------------------------------------------------------

_winpaths = {}


def winpath(p):
    """A path as the Windows tools take it."""
    p = str(p)
    if IS_WIN:
        return p
    if p not in _winpaths:
        _winpaths[p] = subprocess.run(['wslpath', '-w', p], capture_output=True, text=True, check=True).stdout.strip()
    return _winpaths[p]


# what each tool is built from (src/modern is compiled into modern_harness)
TOOL_SOURCES = {
    'modern_harness': [REPO / 'unix' / 'harness' / 'modern_harness.cpp', REPO / 'src' / 'modern'],
    'winamp_capture': [REPO / 'unix' / 'harness' / 'winamp_capture.cpp', REPO / 'unix' / 'harness' / 'capture_util.h'],
    'fb2k_capture': [REPO / 'unix' / 'harness' / 'fb2k_capture.cpp', REPO / 'unix' / 'harness' / 'capture_util.h'],
}


def newest_source(paths, skip=()):
    t = 0.0
    for p in paths:
        files = [p] if p.is_file() else [f for f in p.rglob('*') if f.suffix in ('.cpp', '.h', '.inc', '.rc') and not any(x in f.parts for x in skip)]
        for f in files:
            t = max(t, f.stat().st_mtime)
    return t


def build(target, build_dir=BUILD):
    """Rebuilds a target in WSL; on Windows the tools come from the WSL build, so a stale one is a setup error."""
    if IS_WIN:
        raise SetupError(f'{target} is older than its sources: rebuild in WSL (cmake --build {build_dir.relative_to(REPO).as_posix()} --target {target})')
    print(f'building {target} ({build_dir.name})...', file=sys.stderr)
    r = subprocess.run(['cmake', '--build', str(build_dir), '--target', target], capture_output=True, text=True)
    if r.returncode != 0:
        raise SetupError(f'build of {target} failed:\n' + '\n'.join(l for l in r.stdout.splitlines() if 'error' in l)[:1500])


_fresh = set()


def tool(name):
    exe = BUILD / (name + '.exe')
    if name not in _fresh:
        if not exe.exists() or exe.stat().st_mtime < newest_source(TOOL_SOURCES[name], skip=('ui',)):
            build(name)
        _fresh.add(name)
    return exe


def run_tool(name, args, timeout, env=None):
    cmd = [str(tool(name))] + [str(a) for a in args]
    e = dict(os.environ)
    if env:
        e.update(env)
        if not IS_WIN:
            e['WSLENV'] = ':'.join(filter(None, [os.environ.get('WSLENV', '')] + list(env)))
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout, env=e)
    except subprocess.TimeoutExpired:
        return None
    r.stdout = r.stdout.decode('utf-8', 'replace')
    r.stderr = r.stderr.decode('utf-8', 'replace')
    return r


# --- the skin list (skintest/skins.txt) -------------------------------------------------------------

class Manifest:
    """skintest/skins.txt: INI-like. A [section] per skin with file, status (pending/accepted), note, and accepted
    values: winamp.<window> / fb2k.<window> (% of pixels differing from Winamp), mask.winamp.<window> /
    mask.fb2k.<window> (x,y,w,h rectangles separated by spaces, excluded from comparisons), tolerance.fb2k.<window>
    (pixels a foobar2000 capture may differ from its golden: a window animating on its own). Anything after ';'
    is a comment, kept when the tool rewrites the file."""

    def __init__(self, path=MANIFEST):
        self.path = path
        self.header = []
        self.skins = {}  # name -> {'comments': [...], 'values': {key: [value, comment]}}
        if not path.exists():
            return
        cur = None
        for line in path.read_text(encoding='utf-8').splitlines():
            s = line.strip()
            m = re.fullmatch(r'\[(.+)\]', s)
            if m:
                cur = self.skins.setdefault(m.group(1), {'comments': [], 'values': {}})
            elif cur is None:
                self.header.append(line)
            elif s.startswith((';', '#')):
                cur['comments'].append(s)
            elif '=' in s:
                k, v = s.split('=', 1)
                v, _, c = v.partition(';')
                cur['values'][k.strip()] = [v.strip(), c.strip()]

    def get(self, skin, key, default=None):
        v = self.skins.get(skin, {}).get('values', {}).get(key)
        return v[0] if v else default

    def set(self, skin, key, value):
        vals = self.skins.setdefault(skin, {'comments': [], 'values': {}})['values']
        if value is None:
            vals.pop(key, None)
        elif key in vals:
            vals[key][0] = str(value)
        else:
            vals[key] = [str(value), '']

    def keys(self, skin, prefix):
        return {k[len(prefix):]: v[0] for k, v in self.skins.get(skin, {}).get('values', {}).items() if k.startswith(prefix)}

    def masks(self, skin, gate, key):
        v = self.get(skin, f'mask.{gate}.{key}')
        if not v:
            return []
        return [tuple(int(n) for n in r.split(',')) for r in v.split()]

    def kind(self, skin):
        return 'modern' if self.get(skin, 'file', '').lower().endswith('.wal') else 'classic'

    def save(self):
        order = {'file': 0, 'status': 1, 'note': 2}
        out = list(self.header)
        while out and not out[-1].strip():
            out.pop()
        for name, sk in self.skins.items():
            out += ['', f'[{name}]'] + sk['comments']
            keys = sorted(sk['values'], key=lambda k: (order.get(k, 3), k.split('.')[0] == 'mask', k.lower()))
            for k in keys:
                v, c = sk['values'][k]
                line = f'{k:<24} = {v}'
                out.append(f'{line:<40} ; {c}' if c else line)
        self.path.write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')


# --- results ------------------------------------------------------------------------------------------

class Results:
    def __init__(self, manifest):
        self.m = manifest
        self.items = []  # dicts: skin, gate, key, state (pass/fail/new/info), msg, ...

    def add(self, skin, gate, key, state, msg='', **extra):
        self.items.append(dict(skin=skin, gate=gate, key=key, state=state, msg=msg, **extra))

    def failing(self):
        return [i for i in self.items if i['state'] in ('fail', 'new') and self.m.get(i['skin'], 'status') == 'accepted']

    def report(self, verbose, started):
        news = {}  # (skin, gate) -> [item]: new windows are listed one line per skin and gate
        for i in self.items:
            pending = self.m.get(i['skin'], 'status') != 'accepted'
            if i['state'] == 'pass' and not verbose:
                continue
            golden = i['gate'] == 'golden' or i['gate'].endswith(':golden')
            if pending and i['state'] == 'new' and golden and not verbose:
                continue  # a pending skin has no golden yet
            if i['state'] == 'new' and not verbose:
                news.setdefault((i['skin'], i['gate']), []).append(i)
                continue
            tag = {'pass': 'ok', 'fail': 'FAIL', 'new': 'NEW', 'info': 'info'}[i['state']]
            if pending and i['state'] in ('fail', 'new'):
                tag = 'pend'
            print(f"{tag:<5} {i['skin']}  {i['gate']}  {i['key']}{': ' + i['msg'] if i['msg'] else ''}")
        for (skin, gate), group in news.items():
            tag = 'pend' if self.m.get(skin, 'status') != 'accepted' else 'NEW'
            if 'share' in group[0]:
                what = ', '.join(f"{i['key']} {pct(i['share']):.2f}%" for i in group)
            else:
                what = f"{len(group)} without {'golden' if 'golden' in gate else 'baseline'}: " + ', '.join(i['key'] for i in group)
            print(f'{tag:<5} {skin}  {gate}  {what}')
        n = {s: sum(1 for i in self.items if i['state'] == s) for s in ('pass', 'fail', 'new', 'info')}
        skins = len({i['skin'] for i in self.items})
        bad = self.failing()
        pend = sum(1 for i in self.items if i['state'] in ('fail', 'new') and self.m.get(i['skin'], 'status') != 'accepted')
        verdict = 'OK' if not bad else 'FAILED' if any(i['state'] == 'fail' for i in bad) else 'NEW RESULTS (review, then accept)'
        sheets = sum(1 for i in self.items if i.get('sheet'))
        if sheets:
            print(f'{sheets} difference sheets (current | reference | difference): {DIFFS}')
        if PLAYLIST_SHEETS:
            print(f'{len(PLAYLIST_SHEETS)} playlist difference sheets (not checked): {PLAYLIST_DIFFS}')
        print(f"skintest: {skins} skins, {len(self.items)} checks: {n['pass']} passed, {n['fail']} failed, {n['new']} new, {n['info']} info"
              f"{f', {pend} of them for pending skins (not counted)' if pend else ''} - {verdict} in {time.time() - started:.1f} s")
        return 1 if bad else 0

    def save(self):
        RUNS.mkdir(parents=True, exist_ok=True)
        old = json.loads(LAST.read_text(encoding='utf-8')) if LAST.exists() else {'items': []}
        done = {(i['skin'], i['gate'].split(':')[0] if i['gate'].startswith('fb2k') else i['gate'], i.get('arch')) for i in self.items}
        keep = [i for i in old['items'] if (i['skin'], i['gate'].split(':')[0] if i['gate'].startswith('fb2k') else i['gate'], i.get('arch')) not in done]
        LAST.write_text(json.dumps({'time': time.time(), 'items': keep + self.items}, indent=1), encoding='utf-8')


def load_last():
    if not LAST.exists():
        raise SetupError('no earlier run: run skintest check first')
    return json.loads(LAST.read_text(encoding='utf-8'))['items']


def pct(share):
    return round(share * 100, 2)


def score(res, skin, gate, key, share, accepted, tol=0.0, **extra):
    """A window against Winamp passes only when it's the same (0%, masks aside; tol: the window's own run-to-run
    variation, in %). Anything else fails, whatever was accepted before: the accepted value (skins.txt) is only
    shown, to tell a regression from an improvement."""
    value = pct(share)
    if value <= 0.005 + tol:
        res.add(skin, gate, key, 'pass', f'{value:.2f}%', share=share, **extra)
        return
    if accepted is None:
        was = ''
    elif value > float(accepted) + 0.005 + tol:
        was = f' (worse: accepted {float(accepted):.2f}%)'
    elif value < float(accepted) - 0.005 - tol:
        was = f' (improved: accepted {float(accepted):.2f}%)'
    else:
        was = f' (as accepted)'
    res.add(skin, gate, key, 'fail', f'{value:.2f}% differ{was}', share=share, **extra)


def golden_check(res, skin, gate, key, img, golden, rects=(), tolerance=0, sheet=True, **extra):
    if not Path(golden).exists():
        res.add(skin, gate, key, 'new', 'no golden image', image=str(img), golden=str(golden), **extra)
        return
    if not rects and Path(img).read_bytes() == Path(golden).read_bytes():
        res.add(skin, gate, key, 'pass', image=str(img), golden=str(golden), **extra)
        return
    a, b = pngimg.load(golden), pngimg.load(img)
    pts, counted = pngimg.diff_points(a, b, rects)
    n, boxes = len(pts), pngimg.clusters(pts)
    if n and sheet:
        extra['sheet'] = diff_sheet(skin, gate, key, b, a, 'golden', rects, pts, counted)
    if n <= tolerance and a[:2] == b[:2]:
        res.add(skin, gate, key, 'pass', f'{n} px changed (tolerance {tolerance})' if n else '', image=str(img), golden=str(golden), **extra)
    else:
        size = '' if a[:2] == b[:2] else f', size {a[0]}x{a[1]} -> {b[0]}x{b[1]}'
        where = ', '.join(f'{x0},{y0}-{x1},{y1}' for x0, y0, x1, y1, _ in boxes[:3])
        res.add(skin, gate, key, 'fail', f'{n} px changed{size} at {where}', image=str(img), golden=str(golden), **extra)


def same_picture(p1, p2, rects=()):
    """Whether two PNGs show the same picture (masked rects aside)."""
    if not p1 or not p2 or not Path(p1).exists() or not Path(p2).exists():
        return False
    a, b = pngimg.load(p1), pngimg.load(p2)
    return a[:2] == b[:2] and not pngimg.diff_points(a, b, rects)[0]


def sheet_sources(gate):
    """A gate's (src, dst) for sheet names: what was captured and what it's compared with. The harness's renders
    are our engine alone ("engine"), foobar2000's captures the DLL in foobar2000 ("fb2k-x64")."""
    parts = gate.split(':')
    if parts[0] == 'fb2k':
        return f'fb2k-{parts[1]}', parts[2]
    return 'engine', gate


def sheet_path(skin, gate, key):
    src, dst = sheet_sources(gate)
    return DIFFS / f'[{skin}]_{fname(key)}_{src}_{dst}.png'


def clear_sheets(skin, gates):
    """Removes a skin's sheets of the gates about to be checked: a check writes one only where it finds a difference."""
    if not DIFFS.exists():
        return
    ends = tuple('_{}_{}.png'.format(*sheet_sources(g)) for g in gates)
    for d in (DIFFS, PLAYLIST_DIFFS):
        if d.exists():
            for f in d.iterdir():
                if f.name.startswith(f'[{skin}]_') and f.name.endswith(ends):
                    f.unlink()


def playlist_sheet(skin, gate, key, png, wa_png, rects):
    """A modern skin's playlist against Winamp's: never checked (foobar2000's playlist differs by design), but its
    difference sheet goes to diffs/playlists, for a look."""
    a, b = pngimg.load(wa_png), pngimg.load(png)
    pts, counted = pngimg.diff_points(a, b, rects)
    if not pts:
        return
    p = PLAYLIST_DIFFS / sheet_path(skin, gate, key).name
    PLAYLIST_DIFFS.mkdir(parents=True, exist_ok=True)
    share = f' ({100.0 * len(pts) / counted:.2f}%)' if counted else ''
    pngimg.save(p, *pngimg.sheet(b, a, [sheet_sources(gate)[0], 'Winamp', f'difference: {len(pts)} px{share}'], rects, pts=pts))
    PLAYLIST_SHEETS.append(p)


def diff_sheet(skin, gate, key, cur, ref, what, rects=(), pts=None, counted=None):
    """Writes runs/diffs/[<skin>]_<window>_<src>_<dst>.png: cur | ref | difference with labels (cur, ref: loaded images).
    Returns its path."""
    if pts is None:
        pts, counted = pngimg.diff_points(ref, cur, rects)
    p = sheet_path(skin, gate, key)
    DIFFS.mkdir(parents=True, exist_ok=True)
    share = f' ({100.0 * len(pts) / counted:.2f}%)' if counted else ''
    pngimg.save(p, *pngimg.sheet(cur, ref, [sheet_sources(gate)[0], what, f'difference: {len(pts)} px{share}'], rects, pts=pts))
    return str(p)


def missing_sheet(skin, gate, key, wa_png):
    """The sheet of a window Winamp shows and we don't: Winamp's capture next to nothing (all of it differs)."""
    if not wa_png or not Path(wa_png).exists():
        return {}
    a = pngimg.load(str(wa_png))
    return {'sheet': diff_sheet(skin, gate, key, (a[0], a[1], bytearray(a[0] * a[1] * 4)), a, 'Winamp')}


# --- harness checks (modern skins: golden, winamp, batch) --------------------------------------------------

def harness_checks(res, m, skins, batch=True):
    if not skins:
        return
    out = RUNS / 'harness'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    r = run_tool('modern_harness', ['--compare', winpath(SKIN_DIRS['modern']), winpath(CAPTURES), winpath(out)] + skins, timeout=60 + 30 * len(skins))
    if r is None or r.returncode != 0:
        raise SetupError(f'modern_harness --compare failed: {"timeout" if r is None else r.stdout[-500:] + r.stderr[-500:]}')
    seen = {s: set() for s in skins}
    for s in skins:
        clear_sheets(s, ('golden', 'winamp'))
    tsv = (out / 'results.tsv').read_text(encoding='utf-8', errors='replace').splitlines()
    for line in tsv:
        f = line.split('\t')
        if len(f) < 6:
            continue
        skin, key, wa, ours, frame_only, share = f[0], f[1], f[2], f[3], f[4] == '1', float(f[5])
        if skin not in seen:
            continue
        if key == '#error':
            res.add(skin, 'winamp', '-', 'fail', 'not compared (no capture, skin not found or load failed): see ' + str(out / 'report.txt'))
            continue
        winamp_png = CAPTURES / skin / ntpath.basename(wa) if wa else None
        if winamp_png and re.match(r'^(\d\d|M\d+)_(Winamp )?Playlist Editor_', winamp_png.name, re.I):
            continue  # playlists: foobar2000 gate only
        if key.startswith('#nomatch:'):
            key = key[len('#nomatch:'):]
            seen[skin].add(key)
            score(res, skin, 'winamp', key, 1.0, m.get(skin, 'winamp.' + key), detail='no matching window or layout in the skin',
                  winamp=str(winamp_png), **missing_sheet(skin, 'winamp', key, winamp_png))
            continue
        if key.startswith('fb2k.harness.'):  # our stand-in frame for a Winamp window (library, playlist editor)
            key = key[len('fb2k.harness.'):] + '-frame'
        seen[skin].add(key)
        ours_png = out / skin / 'ours' / ntpath.basename(ours)
        # mask.golden.<window>: parts left out of the harness golden (a looping animation the engine's timing moves)
        golden_check(res, skin, 'golden', key, ours_png, GOLDENS / skin / ours_png.name, m.masks(skin, 'golden', key))
        if not wa:  # a layout Winamp has no capture of: regression (golden) only
            continue
        rects = m.masks(skin, 'winamp', key) + animated_masks(skin).get(key, []) + winamp_only_masks(skin, key, winamp_png)
        extra = {}
        if rects or share > 0:
            a, b = pngimg.load(winamp_png), pngimg.load(ours_png)
            pts, counted = pngimg.diff_points(a, b, rects)
            if rects:
                share = len(pts) / counted if counted else 0.0
            if pts:
                extra['sheet'] = diff_sheet(skin, 'winamp', key, b, a, 'Winamp', rects, pts, counted)
        score(res, skin, 'winamp', key, share, m.get(skin, 'winamp.' + key), winamp=str(winamp_png), image=str(ours_png),
              compare=str(out / skin / ours_png.name), frame_only=frame_only, **extra)
    for skin in skins:
        for key in m.keys(skin, 'winamp.'):
            if key not in seen[skin] and not any(i['skin'] == skin and i['key'] == '-' for i in res.items):
                res.add(skin, 'winamp', key, 'fail', 'window no longer compared (accepted value but no result)')
        gdir = GOLDENS / skin
        if gdir.exists():
            have = {Path(i['image']).name for i in res.items if i['skin'] == skin and i['gate'] == 'golden'}
            for g in gdir.glob('*.png'):
                if g.name not in have:
                    res.add(skin, 'golden', g.stem, 'fail', 'golden image has no render any more')
    if batch:
        batch_checks(res, m, skins)


def batch_block(report_text, skin_file):
    lines = report_text.splitlines()
    block = []
    for i, line in enumerate(lines):
        if line.startswith(skin_file + ':'):
            block.append(line)
            for x in lines[i + 1:]:
                if not x.startswith('  '):
                    break
                block.append(x.strip())
            break
    return block


def batch_checks(res, m, skins):
    out = RUNS / 'batch'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    for skin in skins:
        rep = out / (skin + '.txt')
        r = run_tool('modern_harness', ['--batch', winpath(SKIN_DIRS['modern']), winpath(rep), skin], timeout=120)
        if r is None:
            res.add(skin, 'batch', '-', 'fail', 'timeout (120 s)')
            continue
        if r.returncode != 0 or 'CRASH' in r.stdout:
            res.add(skin, 'batch', '-', 'fail', f'harness exit code {r.returncode}: ' + ' '.join(r.stdout.split()[-12:]))
            continue
        block = batch_block(rep.read_text(encoding='utf-8', errors='replace'), m.get(skin, 'file'))
        (out / (skin + '.block.txt')).write_text('\n'.join(block) + '\n', encoding='utf-8')
        if not block or 'FAILED' in block[0]:
            res.add(skin, 'batch', '-', 'fail', block[0] if block else 'no report', block=str(out / (skin + '.block.txt')))
            continue
        base = BATCH_DIR / (skin + '.txt')
        if not base.exists():
            res.add(skin, 'batch', '-', 'new', f'no baseline; {block[0]}', block=str(out / (skin + '.block.txt')))
            continue
        old = base.read_text(encoding='utf-8').splitlines()
        added = [x for x in block[1:] if x not in old[1:]]
        gone = [x for x in old[1:] if x not in block[1:]]
        if added:
            res.add(skin, 'batch', '-', 'fail', f'{len(added)} new: ' + ' | '.join(added[:3]), block=str(out / (skin + '.block.txt')))
        elif gone:
            res.add(skin, 'batch', '-', 'info', f'{len(gone)} gone (accept to keep): ' + ' | '.join(gone[:3]), block=str(out / (skin + '.block.txt')))
        else:
            res.add(skin, 'batch', '-', 'pass', block=str(out / (skin + '.block.txt')))


# --- foobar2000 checks (both kinds: fb2k golden, fb2k vs Winamp) ----------------------------------------------

def container_ids(skin):
    """A modern skin's windows by title (as window_key makes it) -> the container id (the plan's "#container"
    lines): windows are keyed by container id everywhere, as the harness does. Titles two containers share stay.
    A container without a name (empty in the plan): Winamp's window has no title, keyed by its class."""
    pf = CAPTURES / skin / 'plan.txt'
    if not pf.exists():
        return {}
    ids = {}
    for ln in pf.read_text(encoding='utf-8', errors='replace').splitlines():
        f = ln.split('\t')
        if f[0] == '#container' and len(f) >= 4:
            ids.setdefault(window_key('', f[3], 0) or 'BaseWindow_RootWnd', []).append(f[2])
    return {t: v[0] for t, v in ids.items() if len(v) == 1}


def read_windows(folder, skin, log='windows.txt', prefix=''):
    """windows.txt of a capture -> [(key, class, title, png path or None, state)]. tracks.txt (prefix "T"): the
    windows with tracks loaded, keys "tracks:<window>". Keys: main, eq, pledit, else the container id."""
    p = folder / log
    if not p.exists():
        return []
    ids = container_ids(skin)
    out = []
    pngs = {f.name[len(prefix):len(prefix) + 2]: f for f in folder.glob(prefix + '[0-9][0-9]_*.png')}
    for line in p.read_text(encoding='utf-8', errors='replace').splitlines():
        f = line.split('\t')
        if len(f) < 6 or not f[0].isdigit():
            continue
        idx, cls, title, rect, ex, state = f[:6]
        if state.startswith('skipped'):  # Media Library, Video, AVS: not tested (capture_util.h untested_window)
            continue
        if cls.startswith('Internet Explorer_'):  # a skin's web page's "Script Error" dialog (now and then): not the skin's
            continue
        ex = int(ex[3:], 16) if ex.startswith('ex=') else 0
        key = window_key(cls, title, ex)
        if key not in ('main', 'eq', 'pledit'):
            key = ids.get(key, key)
        out.append(('tracks:' * bool(prefix) + key, cls, title, pngs.get(idx), state.split()[0]))
    return out


def all_windows(folder, skin):
    """A capture's windows, mode/about captures and tracks-state windows: [(key, class, title, png, state)]."""
    out = read_windows(folder, skin) + read_windows(folder, skin, 'tracks.txt', 'T')
    return out + [(k, '', k, png, 'saved') for k, png in read_extra(folder).items()]


def read_extra(folder):
    """Mode captures (modes.txt: "<id>:<layout>") and the about page ("prefs-about", as the harness names it: not
    to clash with a container "About" in a file name) of a capture -> {key: png}."""
    out = {}
    p = folder / 'modes.txt'
    if p.exists():
        for line in p.read_text(encoding='utf-8', errors='replace').splitlines():
            f = line.split('\t')
            if len(f) >= 3 and (folder / f[2]).exists():
                out[f'{f[0]}:{f[1]}'] = folder / f[2]
    for a in folder.glob('about_*.png'):
        out['prefs-about'] = a
    return out


def fname(key):
    """A window key as a file name (':' isn't allowed on Windows)."""
    return re.sub(r'[\\/:*?"<>|]', '~', key)


def window_key(cls, title, ex):
    if cls in ('Winamp v1.x', 'WinampPortable', 'foo_winamp_skins_main'):
        return 'main'
    if cls == 'Winamp EQ' or (cls == 'foo_winamp_skins_child' and title == 'Winamp Equalizer'):
        return 'eq'
    if cls == 'Winamp PE' or (cls == 'foo_winamp_skins_child' and title == 'Winamp Playlist Editor'):
        return 'pledit'
    if cls == 'BaseWindow_RootWnd' and (re.match(r'Winamp \d', title) or re.search(r' - Winamp( \[\w+\])?$', title)):
        return 'main'  # titled with Winamp's version, or "<song> - Winamp [Paused]"
    if cls == 'foo_winamp_skins_modern_layout' and ex & 0x40000:  # WS_EX_APPWINDOW: the main window
        return 'main'
    return re.sub(r'[^A-Za-z0-9_-]', '_', title) or cls


# Playlist editors: foobar2000's playlist differs from Winamp's by design, and the harness can't draw it. They are
# checked only against their own accepted foobar2000 capture (fb2k golden), never against Winamp or in the harness.
PLAYLIST_TITLE = re.compile(r'^(Winamp )?Playlist Editor$', re.I)


# Winamp's own windows (not declared by the skin): foobar2000 opens its own elements for them on request only
WINAMP_OWN = {'Media_Library', 'Winamp_Library', 'Video', 'Winamp_Video', 'AVS', 'Visualization'}


def is_playlist(key, title=''):
    key = key[len('tracks:'):] if key.startswith('tracks:') else key
    return key.lower() == 'pledit' or bool(PLAYLIST_TITLE.match(title)) or bool(PLAYLIST_TITLE.match(key.replace('_', ' ')))


class KeyMasks(dict):
    """{window key: [rects]} looked up ignoring case: the harness keys windows by container id as declared ("Main"),
    the capture tools' main window is "main"."""
    def get(self, key, default=None):
        return super().get(key.lower(), default)

    def add(self, key, rect):
        self.setdefault(key.lower(), []).append(rect)


def animated_masks(skin, kind='#animated'):
    """Looping animations of the skin (the "#animated" lines of its plan; kinds "#browser"/"#bucket": its embedded web pages
    and component buckets, masked in comparisons with Winamp only) as {window key: [rects]}: masked in
    comparisons with Winamp and in foobar2000 ones, where real time catches them at any frame. Keys: the container
    id ("main" for the main window; "tracks:<id>" too) and "<id>:<layout>" for mode captures."""
    pf = CAPTURES / skin / 'plan.txt'
    if not pf.exists():
        return KeyMasks()
    anim = []
    for ln in pf.read_text(encoding='utf-8', errors='replace').splitlines():
        f = ln.split('\t')
        if f[0] == kind and len(f) >= 5:
            # "#browser"/"#bucket" lines add the layout's size: the rect is sized for it (see winamp_only_masks)
            anim.append((f[2], f[3], tuple(int(v) for v in f[4].split(',')) + (tuple(int(v) for v in f[5].split(',')) if len(f) > 5 else ())))
    out = KeyMasks()
    for cid, layout, rect in anim:
        for key in {cid, f'{cid}:{layout}'}:
            out.add(key, rect)
            if ':' not in key:
                out.add('tracks:' + key, rect)
    return out


def winamp_only_masks(skin, key, png):
    """The skin's embedded web pages (browser objects: Winamp shows the page in Internet Explorer, we its address)
    and component buckets (the Thinger: Winamp's plug-ins, ours foobar2000's windows) in a window, masked in
    comparisons with Winamp. They stretch with their window: the plan's rect is for the layout's size, so it grows
    by what the captured window (png) has more."""
    out = []
    if not png or not Path(png).exists():
        return out
    w, h = png_size(png)
    for r in animated_masks(skin, '#browser').get(key, []) + animated_masks(skin, '#bucket').get(key, []):
        x, y, rw, rh = r[:4]
        lw, lh = r[4:6] if len(r) >= 6 else (w, h)
        out.append((x, y, max(0, rw + w - lw), max(0, rh + h - lh)))
    return out


def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    return int.from_bytes(head[16:20], 'big'), int.from_bytes(head[20:24], 'big')


# Classic skins, idle: Winamp's title area shows its own name and version ("Winamp 5.9.2 Build 10042"), ours
# nothing. (Paused, the time display blinks: the capture tools catch it shown, capture_util.h wait_time_shown.)
# Windowshaded, Winamp's main window stays 39 px tall (Windows' minimum height; ours is 14): rows 14-38 are cut away
# by its shade region, unless the skin's region.txt [WindowShade] is taller (Ace_Attorney_-_Justice_For_All, CC-801):
# then Winamp shows the unshaded window's old picture there, a Winamp bug not copied.
CLASSIC_MASKS = {'main': [(111, 23, 155, 14)], 'main:shade': [(0, 14, 275, 25)]}

# Classic skins: where the tracks state is paused ("#tracks_pos", else 0:05). Winamp places the seek thumb from whole
# seconds (position * 256 / length, the 22.5 s third track as 22), ours from the exact ones: at 3.08 s both give the
# same pixel (Winamp 3 s; ours anywhere from 2.988 to 3.164 s).
CLASSIC_TRACKS_POS_MS = 3080


def fb2k_masks(m, skin, key):
    """Everything left out of a foobar2000 comparison: masks from skins.txt, the skin's looping animations, and
    for classic skins Winamp's title in the idle main window."""
    rects = m.masks(skin, 'fb2k', key) + animated_masks(skin).get(key, [])
    if m.kind(skin) == 'classic':
        rects += CLASSIC_MASKS.get(key, [])
    return rects


def fb2k_paths(arch):
    base = DATA / f'fb2k-{arch}'
    return base / 'install', {k: base / f'template-{k}' for k in UI_GUIDS}


def newest_dll(arch):
    """The newest build of the component (WSL or VS, as redeploy.bat picks), rebuilt in WSL when the sources are newer."""
    _, _, plat = ARCHS[arch]
    wsl = REPO / 'unix' / f'build-{arch}' / 'foo_winamp_skins.dll'
    cands = [wsl, REPO / 'out' / plat / 'Debug' / 'foo_winamp_skins.dll', REPO / 'out' / plat / 'Release' / 'foo_winamp_skins.dll']
    cands = [c for c in cands if c.exists()]
    src = newest_source([REPO / 'src'])
    if not cands or max(c.stat().st_mtime for c in cands) < src:
        if IS_WIN:
            raise SetupError(f'no foo_winamp_skins.dll for {arch} newer than src/: build it (VS, or WSL unix/build-{arch})')
        build('foo_winamp_skins', REPO / 'unix' / f'build-{arch}')
        cands = [wsl]
    return max(cands, key=lambda c: c.stat().st_mtime)


def store_blob(d):
    """cfg_modern_store's format (src/modern/ui/modern_ui.cpp, Store): key, value, ... as u32 length + UTF-8."""
    b = bytearray()
    for k, v in d.items():
        for x in (k.encode('utf-8'), v.encode('utf-8')):
            b += struct.pack('<I', len(x)) + x
    return bytes(b)


# the modern UI's playlist container ids (src/modern/ui/modern_ui.cpp, find_toggle_target / fallback_playlist_id)
SKIN_PLAYLIST_IDS = ('pledit', 'pl', 'playlist', 'playlisteditor')
FALLBACK_PLAYLIST = 'fb2k.playlist'


def open_all_template(template, dest, m, skins, sa_mode='1'):
    """A copy of a modern profile template where every declared window of the skins (the "#container" lines of
    their Winamp capture's plan) is saved as open: the modern UI opens them all at start, as winamp_capture makes
    Winamp do. Test what the skin has, not what happens to open by default."""
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(template, dest)
    # the visualizer, as winamp_capture sets Wasabi's "Spectrum analyzer mode" (modern_ui.cpp cfg_sa_mode): the
    # spectrum without tracks ('1'), off with them ('0'; skins with the plan's "#vis_off" clicks start from the
    # start's template instead and are clicked off: fb2k_capture)
    store = {'sa_mode': sa_mode}
    for s in skins:
        pf = CAPTURES / s / 'plan.txt'
        stem = Path(m.get(s, 'file')).stem
        if pf.exists():
            for ln in pf.read_text(encoding='utf-8', errors='replace').splitlines():
                f = ln.split('\t')
                if f[0] == '#container' and len(f) >= 4:
                    store[f'open:{stem}:{f[2]}'] = '1'
        # No playlist window in the skin: Winamp shows its Playlist Editor in the skin's frame; ours is foobar2000's
        # playlist in the generic frame (FALLBACK_PLAYLIST), opened when Winamp had its window open.
        if not any(k.startswith(f'open:{stem}:') and k.split(':')[-1].lower() in SKIN_PLAYLIST_IDS for k in store):
            wf = CAPTURES / s / 'windows.txt'
            if wf.exists() and any(len(f) > 2 and PLAYLIST_TITLE.match(f[2]) for f in
                                   (ln.split('\t') for ln in wf.read_text(encoding='utf-8', errors='replace').splitlines())):
                store[f'open:{stem}:{FALLBACK_PLAYLIST}'] = '1'
    db = sqlite3.connect(dest / 'config.sqlite')
    db.execute('INSERT OR REPLACE INTO configBlobs(name, value) VALUES(?, ?)', (MODERN_STORE_VAR, store_blob(store)))
    db.commit()
    db.close()
    return dest


# The skins the UIs load at start (cfg_skin_path, cfg_modern_skin_path): none in the test profiles. With one saved,
# foobar2000 first shows that skin (the template's last, e.g. Nokia_3300) until "/winamp_skin:" replaces it.
SKIN_PATH_VARS = ('cfg_var.28B0B177-A606-48EC-93D5-AAC347F74B47', 'cfg_var.4FDBB154-DE14-4F47-B061-52BFB61509D7')


# fallback_skin_path (src/common/guids.h cfg_fallback_skin_path): where a classic skin's missing files come from.
# Winamp takes them from its own built-in skin: the test profiles get that skin, winamp_default.wsz in
# skins_classic (the bitmaps built into Winamp's winamp.exe; test data only, never in git or shipped), also tested.
FALLBACK_SKIN_VAR = 'cfg_var.6A4C81FC-D322-427A-A863-94B298B5D861'
FALLBACK_SKIN = SKIN_DIRS['classic'] / 'winamp_default.wsz'


def fallback_skin():
    if not FALLBACK_SKIN.exists():
        raise SetupError(f'no {FALLBACK_SKIN}: Winamp\'s built-in classic skin, the fallback skin of the tests')
    return FALLBACK_SKIN


def no_saved_skin(template):
    """A profile template as every test run starts it: no skin remembered (the run's is passed on the command line),
    the fallback skin set."""
    db = sqlite3.connect(template / 'config.sqlite')
    db.executemany('DELETE FROM configStrings WHERE name = ?', [(v,) for v in SKIN_PATH_VARS])
    db.execute('INSERT OR REPLACE INTO configStrings(name, value) VALUES(?, ?)', (FALLBACK_SKIN_VAR, winpath(fallback_skin())))
    db.commit()
    db.close()


VIS_MODE_VAR = 'cfg_var.D5B14C0F-9F24-4DF7-A5FD-7C9DFDDB4772'  # vis_mode (classic): spectrum 0, oscilloscope 1, off 2


def classic_vis_off(template, dest):
    """A copy of a classic profile template with the visualizer off: the tracks state, as winamp_capture sets
    Winamp.ini's sa for it (starting a track, the visualizer's animation freezes at any frame when paused)."""
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(template, dest)
    db = sqlite3.connect(dest / 'config.sqlite')
    db.execute('INSERT OR REPLACE INTO configInts(name, value) VALUES(?, 2)', (VIS_MODE_VAR,))
    db.commit()
    db.close()
    return dest


def capture_times(folder):
    """When winamp_capture took each state's screenshots ("t=HH:MM:SS.mmm"): {"start": the first window's,
    "tracks": the first tracks-state window's, "<id>:<layout>": that mode capture's}. foobar2000's test clock is
    set to them (fb2k_capture "#clock" lines), so clocks in skins show the same time."""
    out = {}
    for log, state in (('windows.txt', 'start'), ('tracks.txt', 'tracks')):
        p = folder / log
        if p.exists():
            for ln in p.read_text(encoding='utf-8', errors='replace').splitlines():
                at = next((f[2:] for f in ln.split('\t') if f.startswith('t=')), None)
                if at:
                    out[state] = at
                    break
    p = folder / 'modes.txt'
    if p.exists():
        for ln in p.read_text(encoding='utf-8', errors='replace').splitlines():
            f = ln.split('\t')
            at = next((x[2:] for x in f if x.startswith('t=')), None)
            if len(f) >= 3 and at:
                out[f'{f[0]}:{f[1]}'] = at
    return out


def fb2k_capture(m, skins, arch, out, clear=True):
    """Captures the skins in the foobar2000 test install (newest DLL deployed into its profile templates) into out."""
    install, templates = fb2k_paths(arch)
    if not install.exists() or not all(t.exists() for t in templates.values()):
        raise SetupError(f'no foobar2000 test install for {arch}: run skintest fb2k-setup {arch}')
    dll = newest_dll(arch)
    comp = ARCHS[arch][1]
    for t in templates.values():
        d = t / comp / 'foo_winamp_skins'
        d.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dll, d / dll.name)
        pdb = dll.with_suffix('.pdb')
        if pdb.exists():
            shutil.copy2(pdb, d / pdb.name)
        no_saved_skin(t)
    # foo_skintest (unix/harness/foo_skintest.cpp): sets up the tracks state
    helper = REPO / 'unix' / f'build-{arch}' / 'foo_skintest.dll'
    if not helper.exists() or helper.stat().st_mtime < (REPO / 'unix' / 'harness' / 'foo_skintest.cpp').stat().st_mtime:
        build('foo_skintest', REPO / 'unix' / f'build-{arch}')
    for t in templates.values():
        d = t / comp / 'foo_skintest'
        d.mkdir(parents=True, exist_ok=True)
        shutil.copy2(helper, d / helper.name)
    print(f'fb2k {arch}: {dll}', file=sys.stderr)
    if clear:
        shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    for kind in ('classic', 'modern'):
        group = [s for s in skins if m.kind(s) == kind]
        if not group:
            continue
        files = [winpath(skin_path(m, s)) for s in group]
        plan = out / f'plan-{kind}.txt'
        lines = []
        for s in group:
            pf = CAPTURES / s / 'plan.txt'
            if pf.exists():
                lines += [ln for ln in pf.read_text(encoding='utf-8', errors='replace').splitlines()
                          if ln and (not ln.startswith('#') or ln.startswith(('#about\t', '#settle\t', '#settle_tracks\t', '#tracks_pos\t', '#vis_off\t')))]
            stem = Path(m.get(s, 'file')).stem
            lines += [f'#clock\t{stem}\t{state}\t{at}' for state, at in capture_times(CAPTURES / s).items()]
        plan.write_text('\r\n'.join(lines) + '\r\n', encoding='utf-8', newline='')
        if kind == 'classic':
            tmpl, ttmpl = templates[kind], classic_vis_off(templates[kind], out.parent / f'{out.name}-template-tracks')
        else:
            tmpl = open_all_template(templates[kind], out.parent / f'{out.name}-template', m, group)
            ttmpl = open_all_template(templates[kind], out.parent / f'{out.name}-template-tracks', m, group, sa_mode='0')
        extra = ['--modes', winpath(plan), '--tracks', winpath(TRACKS), '--tracks-profile', winpath(ttmpl)]
        r = run_tool('fb2k_capture', [winpath(install), winpath(tmpl), winpath(out)] + extra + files,
                     timeout=120 + 30 * (len(group) + len(lines)))
        if r is None or r.returncode != 0:
            raise SetupError(f'fb2k_capture failed: {"timeout" if r is None else (r.stdout + r.stderr)[-800:]}')


def no_focus(m, skin, out):
    p = out / Path(m.get(skin, 'file')).stem / 'windows.txt'
    return p.exists() and '#foreground\tfailed' in p.read_text(encoding='utf-8', errors='replace')


def fb2k_checks(res, m, skins, arch):
    out = RUNS / f'fb2k-{arch}'
    t0 = time.time()
    fb2k_capture(m, skins, arch, out)
    print(f'fb2k {arch}: captured in {time.time() - t0:.1f} s', file=sys.stderr)
    retry = [s for s in skins if no_focus(m, s, out)]
    if retry:  # another program kept the focus: once more for those
        fb2k_capture(m, retry, arch, out, clear=False)
    gate = f'fb2k:{arch}'
    for skin in skins:
        stem = Path(m.get(skin, 'file')).stem
        ours = all_windows(out / stem, skin)
        wa = {k: png for k, cls, title, png, state in all_windows(CAPTURES / skin, skin)}
        if not ours:
            res.add(skin, gate, '-', 'fail', 'no windows captured in foobar2000', arch=arch)
            continue
        focus = no_focus(m, skin, out)
        if focus:
            res.add(skin, gate, '-', 'info', 'another program kept the focus (twice): title bars may be drawn inactive; '
                    'golden differences of this skin are reported as info', arch=arch)
        first = len(res.items)
        seen = set()
        clear_sheets(skin, (gate + ':golden', gate + ':winamp'))
        idle = {k: p for k, cls, title, p, state in ours if not k.startswith('tracks:')}
        gdir = FB2K_GOLDENS / arch / skin

        def as_idle(key, png, ref, ref_idle, rects):
            """A tracks-state window that looks just as it does idle, and so does its reference: the idle
            window's sheet shows it already (e.g. a Thinger, which shows nothing of the track)."""
            base = key[len('tracks:'):]
            return key.startswith('tracks:') and same_picture(png, idle.get(base), rects) and same_picture(ref, ref_idle, rects)
        for key, cls, title, png, state in ours:
            if state == 'other' and cls == 'tooltips_class32':
                continue  # the real mouse happened to rest on a window: not part of the test
            if state == 'other':
                res.add(skin, gate, key, 'fail', f'unexpected window {cls} "{title}" (an error message?)', arch=arch)
                continue
            if not png:
                res.add(skin, gate, key, 'fail', f'capture {state}', arch=arch)
                continue
            seen.add(key)
            rects = fb2k_masks(m, skin, key)
            tol = int(m.get(skin, 'tolerance.fb2k.' + key, 0))
            golden = gdir / (fname(key) + '.png')
            golden_check(res, skin, gate + ':golden', key, png, golden, rects, tol,
                         sheet=not as_idle(key, png, golden, gdir / (fname(key[len('tracks:'):]) + '.png'), rects), arch=arch)
            if m.kind(skin) == 'modern' and is_playlist(key, title):
                # modern playlists: compared with their own accepted capture only; their sheet against Winamp's
                # playlist goes to diffs/playlists (not a check)
                if wa.get(key) and not as_idle(key, png, wa[key], wa.get(key[len('tracks:'):]), rects):
                    playlist_sheet(skin, gate + ':winamp', key, png, wa[key], rects + winamp_only_masks(skin, key, wa[key]))
                continue
            if key in wa and wa[key]:
                rects = rects + winamp_only_masks(skin, key, wa[key])  # web pages (Winamp's IE, our link), Thinger icons
                a, b = pngimg.load(wa[key]), pngimg.load(png)
                pts, counted = pngimg.diff_points(a, b, rects)
                n = len(pts)
                dup = n and as_idle(key, png, wa[key], wa.get(key[len('tracks:'):]), rects)
                sheet = {'sheet': diff_sheet(skin, gate + ':winamp', key, b, a, 'Winamp', rects, pts, counted)} if n and not dup else {}
                score(res, skin, gate + ':winamp', key, n / counted if counted else 0.0, m.get(skin, 'fb2k.' + key),
                      tol=100.0 * tol / counted if counted else 0.0, arch=arch, winamp=str(wa[key]), image=str(png), **sheet)
            elif key != 'prefs-about':  # an about page without skin.about.group: Winamp shows none to capture
                res.add(skin, gate + ':winamp', key, 'info', 'no such window in the Winamp capture', arch=arch, image=str(png))
        if focus:  # inconclusive, not a regression
            for i in res.items[first:]:
                if i['state'] == 'fail' and i['gate'].endswith(':golden'):
                    i['state'], i['msg'] = 'info', 'inconclusive (no focus): ' + i['msg']
        for key in wa:  # Winamp shows it, foobar2000 doesn't: 100% different until fixed or accepted
            if key not in seen and not (m.kind(skin) == 'modern' and is_playlist(key)) and key.split(':')[-1] not in WINAMP_OWN:
                score(res, skin, gate + ':winamp', key, 1.0, m.get(skin, 'fb2k.' + key), arch=arch, detail='window missing in foobar2000',
                      **missing_sheet(skin, gate + ':winamp', key, wa[key]))


# --- commands ------------------------------------------------------------------------------------------

def skin_path(m, skin):
    f = m.get(skin, 'file')
    p = SKIN_DIRS[m.kind(skin)] / f
    if not p.exists():
        raise SetupError(f'{skin}: {p} not found')
    return p


def select(m, names):
    if not names:
        return list(m.skins)
    out = []
    for n in names:
        if n not in m.skins:
            raise SetupError(f'{n}: not in {MANIFEST.name} (add it with skintest add)')
        out.append(n)
    return out


def archs_of(a):
    return ['x64', 'x86'] if a == 'both' else [a]


def cmd_check(args, fb2k_only=False):
    started = time.time()
    m = Manifest()
    skins = select(m, args.skins)
    res = Results(m)
    if not fb2k_only:
        harness_checks(res, m, [s for s in skins if m.kind(s) == 'modern'], batch=not args.no_batch)
    for arch in archs_of(args.fb2k) if args.fb2k else []:
        fb2k_checks(res, m, skins, arch)
    res.save()
    return res.report(args.verbose, started)


def find_skin_file(name):
    p = Path(name)
    if p.exists():
        kind = 'modern' if p.suffix.lower() == '.wal' else 'classic'
        if p.resolve().parent != SKIN_DIRS[kind].resolve():
            shutil.copy2(p, SKIN_DIRS[kind] / p.name)
            print(f'copied {p.name} into {SKIN_DIRS[kind]}')
        return p.stem, p.name
    for kind, d in SKIN_DIRS.items():
        for ext in ('', '.wal', '.wsz', '.zip'):
            if (d / (name + ext)).is_file():
                return Path(name + ext).stem, name + ext
    raise SetupError(f'{name}: no such skin in {SKIN_DIRS["modern"]} or {SKIN_DIRS["classic"]}')


def winamp_capture(m, skins, wait):
    """Winamp captures (+ the other layouts of modern skins) into skintest/winamp_captures/<skin>."""
    modern = [s for s in skins if m.kind(s) == 'modern']
    RUNS.mkdir(parents=True, exist_ok=True)
    plan = RUNS / 'plan.txt'
    lines = []
    if modern:  # every window the skin's clicks open and every layout they switch to (modern_harness --modes)
        r = run_tool('modern_harness', ['--modes', winpath(SKIN_DIRS['modern']), winpath(plan)] + modern, timeout=120 + 60 * len(modern))
        if r is None or r.returncode != 0:
            raise SetupError('modern_harness --modes failed')
        lines = plan.read_text(encoding='utf-8', errors='replace').splitlines()
    for s in skins:  # classic: windowshade of the main window, equalizer and playlist editor (double-click on the title bar)
        if m.kind(s) == 'classic':
            stem = Path(m.get(s, 'file')).stem
            lines += [f'{stem}\tmain\tmain\tshade\tmain|main|dblclick|100|5||',
                      f'{stem}\teq\tWinamp Equalizer\tshade\teq|Winamp Equalizer|dblclick|100|5||',
                      f'{stem}\tpledit\tWinamp Playlist Editor\tshade\tpledit|Winamp Playlist Editor|dblclick|100|5||',
                      f'#tracks_pos\t{stem}\t{CLASSIC_TRACKS_POS_MS}']
    plan.write_text('\r\n'.join(lines) + '\r\n', encoding='utf-8', newline='')
    args = [winpath(WINAMP), winpath(CAPTURES), '--close-running', '--wait', str(wait), '--modes', winpath(plan), '--tracks', winpath(TRACKS)]
    print(f'capturing {len(skins)} skin(s) in Winamp (takes over the desktop)...', file=sys.stderr)
    r = run_tool('winamp_capture', args + [winpath(skin_path(m, s)) for s in skins], timeout=120 + 60 * len(skins))
    if r is None or r.returncode != 0:
        raise SetupError(f'winamp_capture failed: {"timeout" if r is None else (r.stdout + r.stderr)[-800:]}')
    for s in skins:
        if not (CAPTURES / s / 'windows.txt').exists():
            raise SetupError(f'{s}: Winamp capture produced nothing ({(r.stdout + r.stderr)[-300:]})')
        # the plan stays with the capture: foobar2000 runs replay the same clicks
        mine = [ln for ln in lines if ln.split('\t')[0] == Path(m.get(s, 'file')).stem or (ln.startswith('#') and ln.split('\t')[1:2] == [s])]
        (CAPTURES / s / 'plan.txt').write_text('\r\n'.join(mine) + '\r\n', encoding='utf-8', newline='')
    errs = [ln for ln in (r.stdout + r.stderr).splitlines() if "didn't open" in ln or 'no window' in ln]
    for ln in errs:
        print('winamp_capture: ' + ln, file=sys.stderr)


def cmd_add(args):
    m = Manifest()
    names = []
    for n in args.skins:
        stem, file = find_skin_file(n)
        if stem not in m.skins:
            m.set(stem, 'file', file)
            m.set(stem, 'status', 'pending')
        names.append(stem)
    m.save()
    if not args.no_capture:
        winamp_capture(m, names, args.wait)
    # foobar2000 too when its test install is set up (classic skins are checked only there)
    have_fb2k = fb2k_paths('x64')[0].exists() and not args.no_fb2k
    args.skins, args.fb2k, args.no_batch, args.verbose = names, 'x64' if have_fb2k else None, False, False
    if not have_fb2k and not args.no_fb2k:
        print('no foobar2000 test install (skintest fb2k-setup): foobar2000 checks skipped', file=sys.stderr)
    rc = cmd_check(args)
    print('review the differences (skintest show <skin>), then: skintest accept ' + ' '.join(names))
    return rc


def refresh_plans(m, skins):
    """Rewrites the plan.txt of the skins' Winamp captures from the current harness, keeping their click entries
    (what Winamp was captured with): only the "#" lines (containers, animations, about) are updated."""
    modern = [s for s in skins if m.kind(s) == 'modern']
    if not modern:
        return
    plan = RUNS / 'plan.txt'
    RUNS.mkdir(parents=True, exist_ok=True)
    r = run_tool('modern_harness', ['--modes', winpath(SKIN_DIRS['modern']), winpath(plan)] + modern, timeout=120 + 60 * len(modern))
    if r is None or r.returncode != 0:
        raise SetupError('modern_harness --modes failed')
    lines = plan.read_text(encoding='utf-8', errors='replace').splitlines()
    for s in modern:
        pf = CAPTURES / s / 'plan.txt'
        old = pf.read_text(encoding='utf-8', errors='replace').splitlines() if pf.exists() else []
        stem = Path(m.get(s, 'file')).stem
        new = [ln for ln in old if not ln.startswith('#')] + [ln for ln in lines if ln.startswith('#') and ln.split('\t')[1:2] == [stem]]
        pf.write_text('\r\n'.join(new) + '\r\n', encoding='utf-8', newline='')


def cmd_recapture(args):
    m = Manifest()
    skins = select(m, args.skins)
    if args.plan_only:
        refresh_plans(m, skins)
        print(f'plans of {len(skins)} skin(s) refreshed')
        return 0
    winamp_capture(m, skins, args.wait)
    args.skins, args.fb2k, args.no_batch, args.verbose = skins, None, False, False
    return cmd_check(args)


def cmd_accept(args):
    m = Manifest()
    items = load_last()
    skins = list(m.skins) if args.all else select(m, args.skins)
    gates = set(args.gate.split(',')) if args.gate else set(GATES)
    for skin in skins:
        mine = [i for i in items if i['skin'] == skin]
        if not mine:
            print(f'{skin}: no results in the last run: skipped')
            continue
        # the images and reports of a run are replaced by the next run of any skin: accept only what is still there
        files = [i[k] for i in mine for k in ('image', 'block') if i.get(k) and (i['gate'].split(':')[0] in gates)]
        if any(not Path(f).exists() for f in files):
            print(f'{skin}: the last run\'s files for it were replaced by a later run: run check {skin} (with --fb2k for fb2k) again')
            continue
        done = []
        if 'golden' in gates and any(i['gate'] == 'golden' for i in mine):
            d = GOLDENS / skin
            shutil.rmtree(d, ignore_errors=True)
            d.mkdir(parents=True)
            for i in mine:
                if i['gate'] == 'golden' and i.get('image') and Path(i['image']).exists():
                    shutil.copy2(i['image'], d / Path(i['image']).name)
            clear_sheets(skin, ('golden',))
            done.append('golden')
        if 'winamp' in gates and any(i['gate'] == 'winamp' for i in mine):
            for k in m.keys(skin, 'winamp.'):
                m.set(skin, 'winamp.' + k, None)
            for i in mine:
                if i['gate'] == 'winamp' and 'share' in i:
                    m.set(skin, 'winamp.' + i['key'], f"{pct(i['share']):.2f}")
            done.append('winamp')
        if 'batch' in gates:
            for i in mine:
                if i['gate'] == 'batch' and i.get('block') and Path(i['block']).exists():
                    BATCH_DIR.mkdir(exist_ok=True)
                    shutil.copy2(i['block'], BATCH_DIR / (skin + '.txt'))
                    done.append('batch')
        if 'fb2k' in gates and any(i['gate'].startswith('fb2k') for i in mine):
            worst = {}
            for arch in {i['arch'] for i in mine if i.get('arch')}:  # replaced, not added to: no stale windows
                shutil.rmtree(FB2K_GOLDENS / arch / skin, ignore_errors=True)
                clear_sheets(skin, (f'fb2k:{arch}:golden',))
            for i in mine:
                if i['gate'].endswith(':golden') and i.get('image') and Path(i['image']).exists():
                    d = FB2K_GOLDENS / i['arch'] / skin
                    d.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(i['image'], d / (fname(i['key']) + '.png'))
                elif i['gate'].endswith(':winamp') and 'share' in i:
                    worst[i['key']] = max(worst.get(i['key'], 0.0), i['share'])
            if worst:
                for k in m.keys(skin, 'fb2k.'):
                    m.set(skin, 'fb2k.' + k, None)
                for k, v in worst.items():
                    m.set(skin, 'fb2k.' + k, f'{pct(v):.2f}')
            done.append('fb2k')
        m.set(skin, 'status', 'accepted')
        if args.note:
            m.set(skin, 'note', args.note)
        print(f"{skin}: accepted {', '.join(done) or 'nothing (no results for those gates)'}")
    m.save()
    return 0


def cmd_show(args):
    items = [i for i in load_last() if i['skin'] == args.skin]
    if args.window:
        items = [i for i in items if i['key'] == args.window]
    else:
        items = [i for i in items if i['state'] in ('fail', 'new', 'info')]
    if args.gate:
        items = [i for i in items if i['gate'].startswith(args.gate)]
    out = RUNS / 'show'
    out.mkdir(parents=True, exist_ok=True)
    m = Manifest()
    n = 0
    for i in items:
        ref = i.get('golden') if i['gate'].endswith('golden') else i.get('winamp')
        img = i.get('image')
        if not ref or not img or not Path(ref).exists() or not Path(img).exists():
            print(f"{i['gate']} {i['key']}: {i['msg'] or i['state']} (no images)")
            continue
        a, b = pngimg.load(ref), pngimg.load(img)
        # the masks the check used
        g, key = i['gate'], i['key']
        if g.startswith('fb2k'):
            rects = fb2k_masks(m, args.skin, key) + (winamp_only_masks(args.skin, key, ref) if g.endswith('winamp') else [])
        elif g == 'golden':
            rects = m.masks(args.skin, 'golden', key)
        else:
            rects = m.masks(args.skin, 'winamp', key) + animated_masks(args.skin).get(key, []) + winamp_only_masks(args.skin, key, ref)
        _, _, boxes = pngimg.diff(a, b, rects)
        what = 'golden' if i['gate'].endswith('golden') else 'Winamp'
        print(f"{i['gate']} {i['key']}: {i['msg']}")
        for k, (x0, y0, x1, y1, px) in enumerate(boxes[:args.boxes]):
            w = x1 - x0 + 9
            scale = max(1, min(8, 480 // max(1, w)))
            cw, ch, cp = pngimg.crop_pair(a, b, (x0, y0, x1, y1), scale)
            p = out / f"{args.skin}_{i['gate'].replace(':', '-')}_{fname(i['key'])}_{k}.png"
            pngimg.save(p, cw, ch, cp)
            print(f'  {x0},{y0}-{x1},{y1} ({px} px): {p}  ({what} above, ours below, x{scale})')
            n += 1
    if not n:
        print('nothing to show')
    return 0


def cmd_list(args):
    m = Manifest()
    for s in m.skins:
        wa = m.keys(s, 'winamp.')
        fb = m.keys(s, 'fb2k.')
        worst = max([float(v) for v in wa.values()] or [0])
        print(f"{s:<32} {m.kind(s):<8} {m.get(s, 'status', '?'):<9} winamp {len(wa)} windows (worst {worst:.2f}%), fb2k {len(fb)}"
              f"{'  ; ' + m.get(s, 'note') if m.get(s, 'note') else ''}")
    return 0


def cmd_stabilize(args):
    """Captures in foobar2000 several times without changes; what differs between runs (clocks, real-time
    animations) becomes mask.fb2k.<window> rectangles."""
    m = Manifest()
    skins = select(m, args.skins)
    runs = []
    for k in range(args.runs):
        out = RUNS / f'stabilize-{args.arch}-{k}'
        fb2k_capture(m, skins, args.arch, out)
        runs.append(out)
        if k + 1 < args.runs:
            time.sleep(args.pause)
    changed = 0
    for skin in skins:
        stem = Path(m.get(skin, 'file')).stem
        first = {key: png for key, cls, title, png, state in all_windows(runs[0] / stem, skin) if png}
        for key, png in first.items():
            old_rects = set(fb2k_masks(m, skin, key))
            old_tol = int(m.get(skin, 'tolerance.fb2k.' + key, 0))
            boxes, worst = [], 0
            a = pngimg.load(png)
            for other in runs[1:]:
                pngs = {k2: p2 for k2, c2, t2, p2, st2 in all_windows(other / stem, skin) if p2}
                if key in pngs:
                    n, _, bx = pngimg.diff(a, pngimg.load(pngs[key]), sorted(old_rects))
                    boxes += bx
                    worst = max(worst, n)
            if not boxes:
                continue
            if len(boxes) <= 4:  # a clock, a counter: mask those spots
                rects = set(m.masks(skin, 'fb2k', key)) | {(max(0, x0 - 2), max(0, y0 - 2), x1 - x0 + 5, y1 - y0 + 5) for x0, y0, x1, y1, _ in boxes}
                v = ' '.join(f'{x},{y},{w},{h}' for x, y, w, h in sorted(rects))
                m.set(skin, f'mask.fb2k.{key}', v)
                print(f'{skin}  mask.fb2k.{key} = {v}')
            else:  # scattered (an animation): a tolerance of three times the worst run-to-run change
                tol = max(old_tol, 3 * worst)
                m.set(skin, f'tolerance.fb2k.{key}', tol)
                print(f'{skin}  tolerance.fb2k.{key} = {tol} px ({len(boxes)} changing spots, up to {worst} px)')
            changed += 1
    m.save()
    print(f'{changed} window(s) masked or given a tolerance; then: skintest fb2k both ... and accept --gate fb2k')
    return 0


def winamp_ini():
    """[Winamp] of the portable Winamp's Settings.clean (what every Winamp capture starts from)."""
    p = WINAMP / 'Settings.clean' / 'Winamp.ini'
    out, sec = {}, ''
    if p.exists():
        for line in p.read_text(encoding='utf-8', errors='replace').splitlines():
            line = line.strip()
            if line.startswith('['):
                sec = line
            elif sec == '[Winamp]' and '=' in line:
                k, _, val = line.partition('=')
                out[k.strip()] = val.strip()
    return out


def cmd_fb2k_setup(args):
    for arch in archs_of(args.arch):
        src_dir, comp, _ = ARCHS[arch]
        src = WS / src_dir
        if not (src / 'foobar2000.exe').exists():
            raise SetupError(f'{src}: no foobar2000 install to copy')
        install, templates = fb2k_paths(arch)
        if not install.exists():
            print(f'{arch}: copying {src} (without its profile) to {install}')
            shutil.copytree(src, install, ignore=lambda d, names: ['profile'] if Path(d) == src else [])
            (install / 'portable_mode_enabled').touch()
        # the x64 install's settings for both: the same settings make x64 and x86 results comparable (fb2k.<window>
        # values are shared), and that profile is the one in daily use (knows both UIs, so no "new UI" prompt)
        profile = Path(args.source) if args.source else WS / ARCHS['x64'][0] / 'profile'
        if not (profile / 'config.sqlite').exists():
            raise SetupError(f'{profile}: no config.sqlite')
        for kind, t in templates.items():
            shutil.rmtree(t, ignore_errors=True)
            t.mkdir(parents=True)
            for name in PROFILE_KEEP:
                p = profile / name
                if p.is_dir():
                    shutil.copytree(p, t / name)
                elif p.exists():
                    shutil.copy2(p, t / name)
            db = sqlite3.connect(t / 'config.sqlite')
            db.execute('INSERT OR REPLACE INTO configBlobs(name, value) VALUES(?, ?)', (FB2K_UI_VAR, uuid.UUID(UI_GUIDS[kind]).bytes_le))
            db.execute('DELETE FROM configBlobs WHERE name = ?', (MODERN_STORE_VAR,))
            # as Winamp's tests: the song title stands still (autoscroll off; Winamp: autoscrollname=0) and the
            # title formats are the defaults (Winamp's own)
            db.execute('INSERT OR REPLACE INTO configInts(name, value) VALUES(?, 0)', (AUTOSCROLL_VAR,))
            for var in FORMAT_VARS:
                for table in ('configStrings', 'configBlobs'):
                    db.execute(f'DELETE FROM {table} WHERE name = ?', (var,))
            if MAIN_TITLE_FORMAT[kind]:
                db.execute('INSERT OR REPLACE INTO configStrings(name, value) VALUES(?, ?)', (FORMAT_VARS[0], MAIN_TITLE_FORMAT[kind]))
            ini = winamp_ini()
            for var, key, conv in WINAMP_SETTINGS:
                if key in ini:
                    db.execute('INSERT OR REPLACE INTO configInts(name, value) VALUES(?, ?)', (var, conv(ini[key])))
            db.execute('INSERT OR REPLACE INTO configStrings(name, value) VALUES(?, ?)', (PL_FONT_VAR, ini.get('playlist_custom_font', 'Arial')))
            db.execute('INSERT OR REPLACE INTO configStrings(name, value) VALUES(?, ?)', (EQ_VALUES_VAR, eq_values(ini)))
            db.execute('INSERT OR REPLACE INTO configInts(name, value) VALUES(?, 1)', (PL_FONT_SMOOTH_VAR,))
            # Winamp's volume (0..255) in foobar2000 through our slider curve (player::slider_to_db: 10 log2(slider))
            vol = int(ini.get('volume', 200)) / 255.0
            db.execute("UPDATE configReals SET value = ? WHERE name LIKE 'core.output.%volume'", (max(-100.0, 10 * math.log2(vol)) if vol > 0 else -100.0,))
            db.commit()
            db.close()
            (t / comp / 'foo_winamp_skins').mkdir(parents=True)
            print(f'{arch}: {kind} template {t} (settings of {profile})')
    print('foobar2000 goldens depend on these templates: after a new fb2k-setup, check and accept with --gate fb2k again')
    return 0


def main():
    ap = argparse.ArgumentParser(prog='skintest', description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('check', help='run the checks (harness gates; --fb2k adds foobar2000)')
    p.add_argument('skins', nargs='*')
    p.add_argument('--fb2k', choices=['x64', 'x86', 'both'])
    p.add_argument('--no-batch', action='store_true')
    p.add_argument('-v', '--verbose', action='store_true')
    p = sub.add_parser('fb2k', help='foobar2000 checks only')
    p.add_argument('fb2k', nargs='?', choices=['x64', 'x86', 'both'], default='x64')
    p.add_argument('skins', nargs='*')
    p.add_argument('-v', '--verbose', action='store_true')
    p = sub.add_parser('add', help='capture skins in Winamp and add them as pending')
    p.add_argument('skins', nargs='+')
    p.add_argument('--wait', type=float, default=1, help="seconds after the start where the plan has no measure (#settle)")
    p.add_argument('--no-capture', action='store_true', help='use the existing Winamp capture')
    p.add_argument('--no-fb2k', action='store_true', help='skip the foobar2000 checks (e.g. while the DLL is older than src/)')
    p = sub.add_parser('recapture', help='capture skins in Winamp again')
    p.add_argument('skins', nargs='*')
    p.add_argument('--plan-only', action='store_true', help='only refresh the "#" lines of the stored plans (no Winamp run)')
    p.add_argument('--wait', type=float, default=1, help="seconds after the start where the plan has no measure (#settle)")
    p = sub.add_parser('accept', help="make the last run's results the expected ones")
    p.add_argument('skins', nargs='*')
    p.add_argument('--all', action='store_true')
    p.add_argument('--gate', help='comma-separated: ' + ','.join(GATES))
    p.add_argument('--note')
    p = sub.add_parser('show', help='zoomed crops of the differences of the last run')
    p.add_argument('skin')
    p.add_argument('window', nargs='?')
    p.add_argument('--gate')
    p.add_argument('--boxes', type=int, default=3)
    sub.add_parser('list', help='the tested skins')
    p = sub.add_parser('stabilize', help='mask what changes between foobar2000 runs by itself (clocks, animations)')
    p.add_argument('skins', nargs='*')
    p.add_argument('--arch', choices=['x64', 'x86'], default='x64')
    p.add_argument('--runs', type=int, default=3)
    p.add_argument('--pause', type=float, default=20, help='seconds between runs (lets clocks move)')
    p = sub.add_parser('fb2k-setup', help='create the foobar2000 test installs and profiles')
    p.add_argument('arch', nargs='?', choices=['x64', 'x86', 'both'], default='both')
    p.add_argument('--from', dest='source', help='profile to take the settings from (default: foobar2000_x64\'s)')
    args = ap.parse_args()
    if args.cmd == 'accept' and not args.all and not args.skins:
        ap.error('accept: name skins or pass --all')
    try:
        if args.cmd == 'check':
            return cmd_check(args)
        if args.cmd == 'fb2k':
            return cmd_check(args, fb2k_only=True)
        return {'add': cmd_add, 'recapture': cmd_recapture, 'accept': cmd_accept, 'show': cmd_show, 'list': cmd_list,
                'stabilize': cmd_stabilize, 'fb2k-setup': cmd_fb2k_setup}[args.cmd](args)
    except SetupError as e:
        print(f'skintest: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
