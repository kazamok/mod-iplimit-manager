## IPLimit Manager

## Important note

## Features

- Allows only one client connection from the same IP address.

- You can add or remove exception IPs via the command.

- You can enable the module and set the message in the configuration file.

## Commands
- `.allowip add <ip>` : Add to allowed IP list

- `.allowip del <ip>` : Remove from allowed IP list

## Installation guide
- Copy the module to the `modules` folder.

- Modify `mod-iplimit-manager.conf.dist`. (Default)

- Apply `mod-iplimit-manager.sql` to the world DB. (Default)

- Regenerate and build CMake.
