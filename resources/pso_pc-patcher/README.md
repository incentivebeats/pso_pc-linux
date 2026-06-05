# pso_pc-patcher
This patcher is designed to work against a later version of pso.exe that is already used on other servers and assumes you are already using that version (which I believe is a SEGA patch version from 2002). This means the patcher will not work against the pso.exe generated from ISO install yet, but will work against autorun.exe and online.exe generated from ISO install. 

Refer to [Patches](https://github.com/incentivebeats/pso_pc/tree/main/resources/pso_pc-patcher/patches) for an explanation of each patch and how to run the commands.
  
## Usage

Verify one patch:

```bash
python3 pso_pc_patcher.py pso.exe --patch patches/dragon_bml_selector_bypass.toml --verify
```

Apply one patch:

```bash
python3 pso_pc_patcher.py pso.exe --patch patches/dragon_bml_selector_bypass.toml --apply
```

Revert one patch:

```bash
python3 pso_pc_patcher.py pso.exe --patch patches/dragon_bml_selector_bypass.toml --revert
```

## Patch files

Patch files go in:

```text
patches/
```

## Patch format

```toml
name = "PSO PC Dragon BML selector bypass"
image_base = "0x00400000"

[[patches]]
label = "Force Dragon selector to skip bm_boss1_dragon_b.bml"
va = "0x00420326"
expected = "74 14"
wanted = "EB 14"
```

