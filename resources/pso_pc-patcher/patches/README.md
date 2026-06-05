## Patch files

### `autorun_exe_region_bypass.toml`

Patches `autorun.exe` with the Sylverant-style compatibility fixes. This bypasses the Japanese system-language check and skips a startup mismatch branch that can break the launcher under Wine.

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

Patches the IVES/Sylverant-style `online.exe` launcher/update wrapper to use a custom account URL, patch host, and patch base URL. This is separate from `pso.exe` server patching.

Verify:

```bash
python3 pso_pc_patcher.py /path/to/online.exe \
  --patch patches/online_exe_server.toml \
  --set account_url=http://108.175.11.140/account/ \
  --set patch_host=108.175.11.140 \
  --set patch_base_url=http://108.175.11.140 \
  --verify
```

Apply:

```bash
python3 pso_pc_patcher.py /path/to/online.exe \
  --patch patches/online_exe_server.toml \
  --set account_url=http://108.175.11.140/account/ \
  --set patch_host=108.175.11.140 \
  --set patch_base_url=http://108.175.11.140 \
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

Patches all three 2002-layout `pso.exe` server-address slots to a custom hostname or IPv4 address. This is intended for the Sylverant/Ives/Ragol 2002-layout `pso.exe`, not the older 2001 install-layout `pso.exe`.

Verify:

```bash
python3 pso_pc_patcher.py /path/to/pso.exe \
  --patch patches/pso_exe_server.toml \
  --set address=108.175.11.140 \
  --verify
```

Apply:

```bash
python3 pso_pc_patcher.py /path/to/pso.exe \
  --patch patches/pso_exe_server.toml \
  --set address=108.175.11.140 \
  --apply
```

