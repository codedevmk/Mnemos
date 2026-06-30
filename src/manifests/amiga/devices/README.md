# Amiga Devices

Keyboard, joystick, mouse, and controller-port integration belongs here. Host
frontend input mapping stays in the player adapter.

`amiga_input.*` currently owns pure controller-port helpers: button masks,
JOYDAT encoding, mouse counter wrapping, and POT counter composition. CIA pin
routing and keyboard serial state still live in `amiga_system.cpp`.
