# pso_pc-patcher
This patcher assumes you are using binaries used on other servers and distributed online. If you are using original binaries from the 2001 ISO files, these patches will not likely work.  
  
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

