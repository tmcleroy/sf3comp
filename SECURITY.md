# Security

The game and its launcher read files the player chooses (the ROM sets, `settings.ini`, save states, the settings
memory, music for the custom soundtrack). Over the network, online play exchanges inputs with the other player (or
the lobby), and the launcher asks GitHub for the list of releases when its update check is on. A file or a packet
that makes the game or the launcher read or write outside its own memory, or start another program, is a real bug
and worth a private report.

Please report such a thing privately through GitHub ("Report a vulnerability" on the repository's Security tab) or
to tmcleroy@gmail.com, and not in a public issue.

Only the latest version is supported.
