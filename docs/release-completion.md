# deadspace_nx release completion list

The source list for release notes.

Process:
1. Add finished work under "Ready for changelog", one concise, player-facing
   line each.
2. When cutting a release, move the shipped lines into the release notes and
   under the release's heading below.
3. Keep what is not done under "Carry forward".

## Ready for changelog

For 0.2.0 (checked lines: tested on hardware, handheld):

- [x] Fixed: a crash while the game loads, on some starts (two loading
      threads read each other's files).
- [x] Fixed: a crash to the HOME menu when aiming with the plasma saw before
      the first weapon is picked up.
- [ ] Fixed: static in place of the protagonist's hair in the hallucinations,
      from the second chapter on (textures without mipmaps were not drawn).
- [x] The pointer also comes up for what a level asks in the middle of play
      (spending a power node on a lock, the bench, the store): A chooses
      under it, B answers no.
- [x] The port's own settings screen, + and - together: camera sensitivity,
      motion aiming, its sensitivity and directions, saved to config.ini.
- [x] + (pause) and - (the RIG) now act when the button is let go.

## Released

### 0.1.0

Tested on hardware, handheld, 720p, stock clocks:

- [x] Dead Space 1.2.0 (Amazon build, armeabi) runs from the player's own
      APK at 60 fps, with sound. Nothing is copied out of the APK but the
      engine: the game's data is read in place.
- [x] Full controller play: both sticks, and a button for every action the
      touch screen had (aim, fire, reload, stasis, kinesis, melee, weapons,
      locator, RIG, pause).
- [x] A pointer for the menus: sticks and D-pad move it, A selects, B goes
      back.
- [x] Motion aiming with the gyroscope, switched with the left stick's click
      (`[motion]` in config.ini).
- [x] The touch screen works as on a phone.

## Carry forward

- [ ] Docked 1080p, a Pro Controller and a detached pair of Joy-Cons have had
      little or no play testing (the gyroscope's directions among them).
- [ ] Weapon change on the D-pad and the zero-gravity jump: little tested.
- [ ] HOME and sleep in the middle of a level: not yet seen in a log.
- [ ] A full play-through: later levels, saves, the store screens.
- [ ] The game's language follows the phone it believes it is (English), not
      the console's.
- [ ] Rumble.
- [ ] Other builds of the game (Google Play, Xperia Play) are not supported.
- [ ] The count of live Java objects grows slowly while playing (NOTES.md).
