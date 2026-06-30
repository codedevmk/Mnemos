# Amiga Chipset Profiles

Chipset profile code belongs here when it describes how an Amiga model uses
OCS, ECS, or AGA. Agnus, Denise, and Paula implementations remain reusable
chip-library code under `src/chips/`.

`amiga_chipsets.*` is the current profile table. It owns chipset-specific
composition policy, such as the Copper pointer address mask for OCS versus
ECS 1 MiB Agnus systems.
