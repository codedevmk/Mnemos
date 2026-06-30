# Amiga Devices

Keyboard, joystick, mouse, and controller-port integration belongs here. Host
frontend input mapping stays in the player adapter.

`amiga_input.*` currently owns pure controller-port helpers: button masks,
JOYDAT encoding, mouse counter wrapping, and POT counter composition.
`amiga_keyboard.*` owns raw key constants, keyboard queue/matrix helpers, caps
lock state, and keyboard SDR byte encoding. CIA pin routing and serial
acknowledgement state still live in `amiga_system.cpp`.
