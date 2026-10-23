# Runtime provenance

Adapted from tinypoint/ra2-native commit aa7a5867843aa6e9d6d01e07ac83d5389f3ba463, runtime/win32hle-arm64 and runtime/arm64. The HLE derives from sp00nznet/pcrecomp bd26806fbd4714d670d2dee75973c5eaff64674c; its MIT LICENSE is retained.

Generic CPU, guest memory, Win32 kernel/file/module/timer/network/registry infrastructure, window/message/control bookkeeping, GDI, DirectDraw, DirectSound and COM support are reused here. RA2 game code, boot-specific objects, menu geometry, game-specific UI hooks, and game assets are not imported. Local copies allow independent adaptation without editing the RA2 checkout. This is an incomplete Win32 interface subset, not a replacement game engine.

DirectSound is adapted independently for CoreAudio through SDL; local changes add PCM validation, shared sample ownership, locked host snapshots and interface/cursor checks. No RA2 audio assets or game code are copied.
