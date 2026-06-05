## Patch files

All commands below assume they are run from the patcher repo root.  
This patcher assumes you are using binaries used on other servers and distributed online. If you are using original binaries from the 2001 ISO files, these patches will not likely work.

### `autorun_exe_region_bypass.toml`

Patches `autorun.exe` to bypass a system-language check and skip a launcher startup branch that can break under Wine. If you're already using an autorun.exe from another server and it already works then you likely don't need to touch this.  

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

Patches `online.exe` to use a custom account URL, patch host, and patch base URL. This is separate from `pso.exe` server patching.

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

Patches `pso.exe` to bypass the Dragon BML selector issue by forcing the game to skip `bm_boss1_dragon_b.bml`.

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

Patches all three `pso.exe` server-address slots to a custom hostname or IPv4 address.

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
