# PyInstaller spec file for AudioStream PC Broadcaster
# Usage: pyinstaller AudioStream.spec  (from the pc_app/ directory)

import os
import glob

block_cipher = None

# Find the compiled C++ .so module (architecture-specific filename)
so_files = glob.glob(os.path.join(SPECPATH, 'audiostream_core*.so'))
binaries = [(so, '.') for so in so_files]

# Try to include the app icon
icon_path = os.path.join(SPECPATH, '..', 'android_app', 'assets', 'icon.png')

a = Analysis(
    ['app.py'],
    pathex=[SPECPATH],
    binaries=binaries,
    datas=[
        (icon_path, 'assets') if os.path.exists(icon_path) else None,
    ],
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

# Filter out None entries from datas
a.datas = [d for d in a.datas if d is not None]

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
    console=False,       # No terminal window — GUI only
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=icon_path if os.path.exists(icon_path) else None,
)
