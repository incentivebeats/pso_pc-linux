## Patch files

All commands below assume they are run from the patcher repo root.  
  
This patcher is designed to work against a later version of pso.exe that is already used on other servers and assumes you are already using that version (which I believe is a SEGA patch version from 2002). This means the patcher will not work against the 2001 pso.exe generated from ISO install yet, but will work against autorun.exe and online.exe generated from ISO install. 

### `autorun_exe_region_bypass.toml`

Patches `autorun.exe` to bypass a system-language check and skip a launcher startup branch that can break under Wine. Works on any version of of PSO PC, but is probably unncessary to run if you're already using patched files for another server.

Verify:

```bash
python3 pso_pc_patcher.py /path/to/autorun.exe \
  --patch patches/autorun_exe_region_bypass.toml \
  --verify
```

Apply:

```bash
python3 pso_pc_patcher.py /path/to/autorun.exe \
  --patch patches/autorun_exe_region_bypass.toml \
  --apply
```

### `online_exe_server.toml`

Patches `online.exe` to use a custom account URL, patch host, and patch base URL. This is separate from `pso.exe` server patching. Patching `/account` here is probably not needed but done anyway. 

Verify:

```bash
python3 pso_pc_patcher.py /path/to/online.exe \
  --patch patches/online_exe_server.toml \
  --set account_url=http://YOUR_HOST_OR_IP/account/ \
  --set patch_host=YOUR_HOST_OR_IP \
  --set patch_base_url=http://YOUR_HOST_OR_IP \
  --verify
```

Apply:

```bash
python3 pso_pc_patcher.py /path/to/online.exe \
  --patch patches/online_exe_server.toml \
  --set account_url=http://YOUR_HOST_OR_IP/account/ \
  --set patch_host=YOUR_HOST_OR_IP \
  --set patch_base_url=http://YOUR_HOST_OR_IP \
  --apply
```

### `pso_exe_dragon.toml`

Patches `pso.exe` to bypass the Dragon BML selector issue by forcing the game to skip `bm_boss1_dragon_b.bml`. No more glitched out dragon for certain renderers. This patch will work and is allowed on Sylverant's pso.exe, but will not work on Ragol's pso.exe (they will force their own pso.exe to overwrite once their patch server detects your binary isn't 1:1 with what the server expects).

Verify:

```bash
python3 pso_pc_patcher.py /path/to/pso.exe \
  --patch patches/pso_exe_dragon.toml \
  --verify
```

Apply:

```bash
python3 pso_pc_patcher.py /path/to/pso.exe \
  --patch patches/pso_exe_dragon.toml \
  --apply
```

### `pso_exe_server.toml`

Patches all three `pso.exe` server-address slots to a custom hostname or IPv4 address. This is only tested and working against pso.exe files from Sylverant/Ragol and will likely not work against the base `pso.exe` from a fresh install.

Verify:

```bash
python3 pso_pc_patcher.py /path/to/pso.exe \
  --patch patches/pso_exe_server.toml \
  --set address=YOUR_HOST_OR_IP \
  --verify
```

Apply:

```bash
python3 pso_pc_patcher.py /path/to/pso.exe \
  --patch patches/pso_exe_server.toml \
  --set address=YOUR_HOST_OR_IP \
  --apply
```
