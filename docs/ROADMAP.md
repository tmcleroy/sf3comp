# Roadmap

What comes next, roughly in order. The working notes are [games/sfiii3/PROGRESS.md](../games/sfiii3/PROGRESS.md),
[NATIVE.md](../games/sfiii3/NATIVE.md) and [DEFINITIVE.md](../games/sfiii3/DEFINITIVE.md).

1. **Played on every system by hand.** The Windows download on a Windows PC with a gamepad; more play on the Steam
   Deck and Steam Machine. Versions stay beta until then.
2. **Native to the end.** The picture and sound already come from the game's rewritten drawing and sound code; the
   board's model is kept only as the checks' reference. Next: the routines no mode reaches yet (the endings, the
   staff roll), so that the board's video and sound chips are not needed at all.
3. **The definitive version.** The stage rule checked on every stage, a menu of stages by name, the guest games' own
   stage effects and announcer.
4. **Training mode.** Frame data's recovery and advantage checked against published data; the dummy's guard from the
   boxes; hooks for the round's steps.
5. **Widescreen everywhere**: every stage and screen, and the objects the game leaves out at its 384 columns.
6. **Translation coverage**: more recorded play (every character, supers, the endings), so less runs in the
   interpreter.
7. **The custom soundtrack**: the songs not placed yet (the endings').
