# Amiga Devices

Keyboard, joystick, mouse, and controller-port integration belongs here. Host
frontend input mapping stays in the player adapter.

`amiga_input.*` currently owns pure controller-port helpers: button masks,
JOYDAT encoding, mouse counter wrapping, and POT counter composition.
`amiga_keyboard.*` owns raw key constants, keyboard queue/matrix helpers, caps
lock state, keyboard SDR byte encoding, and keyboard serial acknowledgement
state transitions. CIA pin routing still lives in `amiga_system.cpp`.
