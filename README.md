# Phantasy Star Online Ver. 2 (PC)
---  

## ⚠️ Important Notes

- **macOS:** This guide is Linux‑only. macOS lacks native Vulkan and Wine has limits (vkBasalt/dgVoodoo are unreliable). On macOS use **CrossOver** or **Wine + MoltenVK**, and keep default rendering.
- **What’s a WINEPREFIX?** A *sandboxed Windows install* just for this game. Installing `dsound` etc. here won’t affect other games.
- **Path placeholders** like `/your/path/to/prefix` are *yours to choose*. Examples: `/home/you/pso_pc` or `/home/you/.local/share/pso_pc`.
- Aside from Ver.2‑specific EXEs, this also works for **PSOBB Ultima/Destiny** (for **Ephinea**, use Lutris).

---

## 1) Requirements (check your distro for compatible packages if not Debian based)

```bash
# Debian/Ubuntu family (adapt package names for your distro)
sudo apt update
sudo apt install -y wine winetricks lutris cabextract unzip p7zip-full ffmpeg
```

If you do not wish to install PSO V2 on PC manually, you can try my Linux install script instead. Be sure to correctly fill out the paths in .env before running.  
```
bash mount_iso.sh
bash install.sh
```
When prompted to install, select English and also select *Normal Install*, do not do a full install. Once complete follow the instructions starting with `## 6) Drop in patched Launchers`.  

---

## 2) Pick Paths & Set Helper Vars (once per shell)

```bash
# Where your per-game Wine sandbox will live:
export PREFIX="$HOME/.local/share/pso_pc"

# Default PSO install path inside the prefix:
export GAMEDIR="$PREFIX/drive_c/Program Files (x86)/SEGA/PhantasyStarOnline"
```

---

## 3) Create WINEPREFIX (32‑bit)

```bash
WINEARCH=win32 WINEPREFIX="$PREFIX" wineboot
```
*(Skip Mono/Gecko prompts—unneeded here.)*

---

## 4) Install PSO (Normal install, not Full/Custom)

Mount your ISO, then run the installer:

```bash
# Example mount
sudo mkdir -p /mnt/psoiso
sudo mount -o loop "/path/to/PSO_PC_disc1.iso" /mnt/psoiso

# Run the installer (choose “Normal”)
WINEPREFIX="$PREFIX" wine "/mnt/psoiso/setup.exe"
```

> Unmount later: `sudo umount /mnt/psoiso`

---

## 5) DirectMusic + DirectSound + Fonts

```bash
WINEPREFIX="$PREFIX" winetricks directmusic dsound cjkfonts corefonts
WINEPREFIX="$PREFIX" winecfg
```

**winecfg → Libraries** → add and set to **native** (or **native,builtin** if needed):
- `dsound`
- `dmusic`
- `dmime`
- `dmsynth`

While in **winecfg**:
- **Audio** tab → test sound.
- *(Optional)* **Graphics** → enable “Emulate a virtual desktop” if you like a fixed window.

---

## 6) Drop in patched Launchers

Copy these from the patched launchers, typically files look like:

```
autorun.exe
online.exe
online_offline.exe
pso.exe
```

**Target path:** `"$GAMEDIR"`

---

## 7) (Optional) Add to Lutris

1. Lutris → **Add locally installed game**  
2. **Runner:** Wine  
3. **Executable:** `"$GAMEDIR/autorun.exe"`  
4. **Wine prefix:** `"$PREFIX"`  
5. Save.

---

## 8) (Optional) High‑Quality BGM

Convert FLAC → WAV (22.05 kHz) and place them in the `22` folder:

```bash
# Convert all FLACs in the 22 folder to 22.05 kHz WAV
cd /path/to/music/patch/22
for f in *.flac; do ffmpeg -y -i "$f" -ar 22050 "${f%.flac}.wav"; done
```

Move the resulting WAVs to:

```
$GAMEDIR/Media/PSO/SoundBGM/22
```

---

## 9) (Optional) Graphics: dgVoodoo2 + PSO PC Graphics Fix

> Stabilizes rendering on modern GPUs and enables higher resolutions.

1. Download `dgvoodooo278.zip` and `PSO_PC_Graphics_Fix.zip` (from your trusted source).
2. Extract **dgVoodoo2** (files from `MS/x86`) into:  
   `"$GAMEDIR"`
3. Extract **PSO_PC_Graphics_Fix.zip** into:  
   `"$GAMEDIR"` (allow it to merge/overwrite as the patch instructs).
4. Open the control panel and set preferences:

```bash
WINEPREFIX="$PREFIX" wine "$GAMEDIR/dgVoodooCpl.exe"
```

**Suggested DirectX tab (1080p/4K starting point):**
- Output API: *Best available (D3D11)*
- Adapter: *Your GPU*
- Fullscreen: *Stretched, keep Aspect Ratio* (or *Centered*)
- Resolution: *Your monitor* (or *Unforced* to let the game decide)
- VSync: *On*  
Apply & OK (writes `dgVoodoo.conf` next to your EXEs).

> Issues? Remove the dgVoodoo DLLs from `$GAMEDIR` to revert.

---

## 10) Play

Launch via **Lutris** or directly:

```bash
WINEPREFIX="$PREFIX" wine "$GAMEDIR/autorun.exe"
```

Pick **Online/Offline** as needed and enjoy.

---

## 11) Troubleshooting Quick Hits

- **No music / choppy audio** → Recheck winetricks; make sure the overrides are **native** for `dsound`, `dmusic`, `dmime`, `dmsynth`. Confirm WAVs are **22.05 kHz** in the `22` folder.
- **Black screen / crash** → Temporarily remove dgVoodoo DLLs; if it fixes it, re‑add and tweak resolution/VSync in `dgVoodooCpl`.
- **Weird speed / tearing** → Enable VSync in dgVoodoo; try Wine’s virtual desktop for better focus/alt‑tab behavior.
- **Installer can’t find CD** → Keep the ISO mounted during first run; some versions check for media.
- **Saves/settings not sticking** → Ensure your prefix path is writable; don’t run Wine as root.

---

## 12) Uninstall / Reset

```bash
# Delete only this game’s sandbox:
rm -rf "$PREFIX"
```

