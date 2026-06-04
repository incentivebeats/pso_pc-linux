# Phantasy Star Online Ver. 2 (PC) — Linux Setup Guide

---

## ⚠️ Notes

- **macOS is not supported.** No native Vulkan; vkBasalt/dgVoodoo are unreliable under Wine. Use CrossOver or Wine + MoltenVK and leave rendering at default.
- **WINEPREFIX** = a sandboxed Windows install scoped to this game. Changes here don't affect other titles.
- This also works for **PSOBB Ultima/Destiny**. For Ephinea, use Lutris instead.

---

## 1. Requirements

```bash
# Debian/Ubuntu — adapt package names for your distro
sudo apt update
sudo apt install -y wine winetricks lutris cabextract unzip p7zip-full ffmpeg
```

---

## 2. Install PSO

**Option A — Install scripts:** Edit the `.env` file in the same directory as the scripts with the following contents:

```bash
# Path to your PSO PC installer ISO
ISO_PATH="/path/to/Phantasy Star Online PC - Installation Disc.iso"
# Where the script should mount it
MOUNT_PATH="/mnt/psoiso"
# Wine prefix, change this to your desired path
PREFIX="$HOME/.local/share/pso_pc"
# Uncomment below if not using Wine64
#WINEARCH=wine32
```

```bash
bash mount_iso.sh
bash install.sh
```

When prompted, select **English** and **Normal Install**. Then skip to [§6](#6-drop-in-patched-launchers).

**Option B — Manual:** Set path variables for your shell session, then continue with §3.

```bash
export PREFIX="$HOME/.local/share/pso_pc"
export GAMEDIR="$PREFIX/drive_c/Program Files (x86)/SEGA/PhantasyStarOnline"
```

Mount the ISO and run the installer. Choose **Normal** (not Full or Custom).

```bash
sudo mkdir -p /mnt/psoiso
sudo mount -o loop "/path/to/PSO_PC_disc1.iso" /mnt/psoiso

WINEPREFIX="$PREFIX" wine "/mnt/psoiso/setup.exe"

# Unmount when done:
sudo umount /mnt/psoiso
```

---

## 3. Create WINEPREFIX (32-bit)

```bash
WINEARCH=win32 WINEPREFIX="$PREFIX" wineboot
```

Dismiss any Mono/Gecko prompts — they're not needed.

---

## 4. DirectMusic, DirectSound & Fonts

```bash
WINEPREFIX="$PREFIX" winetricks directmusic dsound cjkfonts corefonts
WINEPREFIX="$PREFIX" winecfg
```

In **winecfg → Libraries**, add the following and set each to **native** (or **native,builtin** if needed):

- `dsound`
- `dmusic`
- `dmime`
- `dmsynth`

Also in winecfg:
- **Audio** tab → test sound
- *(Optional)* **Graphics** → enable "Emulate a virtual desktop" for a fixed window

---

## 5. Drop in Patched Launchers

Copy the following into `$GAMEDIR`, they are usually named as the following. Sometimes you may see an online_offline.exe file and sometimes not:

```
autorun.exe
online.exe
pso.exe
```

---

## 6. (Optional) Add to Lutris

1. Lutris → **Add locally installed game**
2. Runner: **Wine**
3. Executable: `$GAMEDIR/autorun.exe`
4. Wine prefix: `$PREFIX`
5. Save.

---

## 7. (Optional) High-Quality BGM

Convert FLAC tracks to 22.05 kHz WAV and place them in `$GAMEDIR/Media/PSO/SoundBGM/22`:

```bash
cd /path/to/music/patch/22
for f in *.flac; do ffmpeg -y -i "$f" -ar 22050 "${f%.flac}.wav"; done
```

---

## 8. (Optional) dgVoodoo2 + PSO PC Graphics Fix

Stabilizes rendering on modern GPUs and enables higher resolutions.

1. Extract the `MS/x86` contents of `dgvoodoo278.zip` into `$GAMEDIR`.
2. Extract `PSO_PC_Graphics_Fix.zip` into `$GAMEDIR`, allowing overwrites.
3. Configure:

```bash
WINEPREFIX="$PREFIX" wine "$GAMEDIR/dgVoodooCpl.exe"
```

**Recommended DirectX settings (1080p/4K):**

| Setting | Value |
|---|---|
| Output API | Best available (D3D11) |
| Adapter | Your GPU |
| Fullscreen | Stretched, keep Aspect Ratio |
| Resolution | Your monitor (or Unforced) |
| VSync | On |

To revert, remove the dgVoodoo DLLs from `$GAMEDIR`.

---

## 9. Launch

```bash
WINEPREFIX="$PREFIX" wine "$GAMEDIR/autorun.exe"
```

Or launch via Lutris.

---

## 10. Troubleshooting

| Symptom | Fix |
|---|---|
| No music / choppy audio | Verify winetricks overrides are **native**; confirm WAVs are 22.05 kHz in the `22` folder |
| Black screen / crash | Remove dgVoodoo DLLs to isolate; re-add and adjust resolution/VSync if they're the cause |
| Tearing / wrong speed | Enable VSync in dgVoodoo; try Wine's virtual desktop mode |
| Installer can't find CD | Keep ISO mounted during first run |
| Saves/settings not sticking | Confirm prefix path is writable; don't run Wine as root |

---

## 11. Uninstall

```bash
rm -rf "$PREFIX"
```
