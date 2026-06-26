# Irem GA20 PCM Notes

`mnemos::chips::audio::irem_ga20` models the Irem/Nanao GA20 PCM chip used by
late Irem boards including M92 and M107.

The public MAME GA20 device was consulted only as a behavioral reference for
the observable register map: four channels, eight registers per channel,
16-byte-unit start/end addresses, rate, hyperbolic volume, control bit 1
key-on/off, status bit 0 active, and zero-byte sample termination. No MAME
source code is vendored or copied.
