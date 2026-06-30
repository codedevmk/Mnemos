# Amiga Drives

Amiga floppy-drive and disk-controller integration belongs here.
`amiga_floppy.*` currently owns DD floppy geometry plus drive-local
stream/cache state helpers. Disk DMA register timing and CIA port wiring still
live in `amiga_system.cpp` until they can be split without changing behavior.
