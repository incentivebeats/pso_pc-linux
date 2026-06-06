# Phantasy Star Online PC — Getting Started

Resources for setting up PSO PC Version 2 across servers and offline play.

---

## 1. ISOs

No direct links are provided for ISOs or pre-patched EXEs, but they are readily available:

- **Internet Archive** search for PSO PC Version 2
- **Server communities** many host pre-patched ISOs for their specific server
- **Retail disc** used copies work fine, rip your own ISOs

---

## 2. PSO Palace Mods

Recommended patches from [PSO Palace](https://psopalace.sylverant.net/downloads_pc.html):

- PSO PC Graphical Fix Pack 1.1
- PSO PC Shield Fix Patch
- PSO PC Darkness Patch
- PSO PC Max Stat Patch
- PSO PC Offline Quest Pack

---

## 3. Ragol Vanilla+ Offline Mod

[Available at ragol.org](https://ragol.org/pc-downloads). Adjusts offline drop rates so items can be realistically found.

The mod ships six files- you only need two, as the others are byte-for-byte identical to originals:

- `ItemPT.afs`
- `ItemRT.afs`

---

## 4. PSO PC Patcher

[PSO PC Patcher](https://github.com/incentivebeats/pso_pc-linux/tree/main/resources/pso_pc-patcher)- a Python patcher for common setup tasks:

- **Fix the Dragon boss rendering glitch** on certain renderers
  - ✅ Allowed on Sylverant
  - ❌ Will not work on Ragol
- **Change DNS addresses** in `online.exe` and `pso.exe`
- **Fix the Japanese region** in `autorun.exe` so it opens correctly

### Compatibility note

This patcher targets a later version of `pso.exe` already in use on other servers. It will **not** patch a `pso.exe` generated directly from a fresh ISO install but it does work against `autorun.exe` and `online.exe` from an ISO install.

### ⚠️ Patch server warning

PSO PC uses a patch server that may push updates downstream, potentially overwriting `.exe` files, graphics, drop tables, enemy data, and other files. Your patches may be reverted depending on what the server enforces.
