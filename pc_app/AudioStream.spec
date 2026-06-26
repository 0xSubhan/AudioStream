# PyInstaller spec file for AudioStream PC Broadcaster
# Usage: pyinstaller AudioStream.spec  (from the pc_app/ directory)
#
# Supports both Linux (.so) and Windows (.pyd) compiled modules.

import os
import sys
import glob

block_cipher = None

# ── Locate the compiled C++ Python extension ──────────────────────────────────
# On Linux the pybind11 module is a .so file; on Windows it's a .pyd file.
if sys.platform == "win32":
    so_pattern = os.path.join(SPECPATH, 'audiostream_core*.pyd')
else:
    so_pattern = os.path.join(SPECPATH, 'audiostream_core*.so')

so_files = glob.glob(so_pattern)
binaries = [(so, '.') for so in so_files]

# ── Locate the app icon ───────────────────────────────────────────────────────
# Windows requires a .ico file; Linux/macOS use .png
if sys.platform == "win32":
    icon_path = os.path.join(SPECPATH, '..', 'assets', 'icon.ico')
    # Fallback to PNG if ICO not yet generated
    if not os.path.exists(icon_path):
        icon_path = os.path.join(SPECPATH, '..', 'android_app', 'assets', 'icon.png')
else:
    icon_path = os.path.join(SPECPATH, '..', 'android_app', 'assets', 'icon.png')

# Use icon only if found
_icon_for_exe = icon_path if os.path.exists(icon_path) else None

# ── App icon to bundle as a data asset (for window icon in app.py) ─────────────
_data_icon_src = os.path.join(SPECPATH, '..', 'android_app', 'assets', 'icon.png')
_datas = [(_data_icon_src, 'assets')] if os.path.exists(_data_icon_src) else []

a = Analysis(
    ['app.py'],
    pathex=[SPECPATH],
    binaries=binaries,
    datas=_datas,
    hiddenimports=[
        'zeroconf',
        'zeroconf._utils',
        'zeroconf._utils.ipaddress',
        'zeroconf._utils.net',
        'zeroconf._handlers',
        'zeroconf._handlers.answers',
        'zeroconf._handlers.query_handler',
        'zeroconf._handlers.record_manager',
        'zeroconf._services',
        'zeroconf._services.browser',
        'zeroconf._services.info',
        'PyQt6.QtCore',
        'PyQt6.QtGui',
        'PyQt6.QtWidgets',
        'ifaddr',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['tkinter', 'matplotlib', 'numpy', 'scipy', 'PIL'],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='AudioStream',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,        # No terminal window — GUI only
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=_icon_for_exe,   # .ico on Windows, .png on Linux
)
