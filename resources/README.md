## Resources

These resources can help you get started with Phantasy Star Online PC.

### 1. ISOs

I cannot directly link to any ISO files or patched EXEs, but you can easily find them on the Internet Archive. There are also plenty of other resources to find already patched ISOs for different servers. You can also buy a used copy of the game and use your own ISOs too.  

### 2. PSO Palace Mods

The following are recommended from [PSO Palace](https://psopalace.sylverant.net/downloads_pc.html):

- PSO PC Graphical Fix Pack 1.1
- PSO PC Shield Fix Patch
- PSO PC Darkness Patch
- PSO PC Max Stat Patch
- PSO PC Offline Quest Pack

### 3. Ragol Vanilla+ Offline Mod
[Mod from Ragol](https://ragol.org/pc-downloads) that changes offline drop rates so items can be "realistically found". From what I can tell, even though the mod offers six files you really only need two files as the others appear byte-for-byte compatible with originals: 
- `ItemPT.afs`
- `ItemRT.afs`

### 4. PSO PC Patcher  

A [Patcher](https://github.com/incentivebeats/pso_pc-linux/tree/main/resources/pso_pc-patcher) using Python that will help you:
- Fix the Dragon glitch on certain renderers (allowed/works on Sylverant, will not work on Ragol)
- Change DNS addresses in online.exe and pso.exe
- Change the Japanese region in autorun.exe so it properly opens

This patcher is designed to work against a later version of pso.exe that is already used on other servers and assumes you are already using that version. This means the patcher will *not* work against the pso.exe generated from ISO install yet, but will work against autorun.exe and online.exe generated from ISO install.

Also be warned that since PSO PC uses a patch server, your patches may be overwritten depending on what the server forces downstream (this can include overwriting .exe files and other files related to graphics, drops, enemies, etc.,)
