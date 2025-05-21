# mod-iplimit-manager
The AzerothCore module `mod-iplimit-manager` limits connections to only one client from the same IP address.

## Function
- By default, only one connection per IP is allowed.
- You can allow or remove exception IPs with commands.
- You can enable modules and set messages in the configuration file.

## Commands
- `.allowip add <ip>` : Add to allowed IP list
- `.allowip del <ip>` : Remove from allowed IP list

## Installation guide
1. Copy the module to the `modules/` folder.
2. Copy `mod-iplimit-manager.conf.dist` to `mod-iplimit-manager.conf` and modify it.
3. Apply `mod-iplimit-manager.sql` to the world DB.
4. Regenerate CMake and build.
