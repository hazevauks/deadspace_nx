# deadspace_nx — port notes

Port of **Dead Space 1.2.0**, the 2011 mobile game, shown on the console as
*Dead Space: Sabotage* (the name its wiki goes by) to tell it from the 2008
console game (`com.eamobile.deadspace_full_azn`, versionCode
1200, the Amazon Appstore build, armeabi) to Nintendo Switch on the
[android32](https://github.com/aks796/android32) runtime (submodule at
`runtime/`, commit `50b352c`).

State: **playable on hardware.** The game starts, loads its first level and
runs at 60 fps at 720p (handheld, stock clocks) with sound. The controller,
the menus' pointer and motion aiming were confirmed on hardware, handheld.
Released as 0.1.0; `docs/release-completion.md` has what is left and what
0.2.0 brings (three fixes from players' reports and a settings screen, not
yet tested on hardware).

## The game

| | |
| --- | --- |
| Engine | EA's EAMCore / "Blast" framework, EASTL, RenderWare file system (`libDeadSpace.so`, 6.7 MB, ARMv5TE, soft-float) |
| Graphics | OpenGL ES 1.1, fixed function (imports the whole API: 190 `gl*`, among them `OES_matrix_palette` and `OES_draw_texture`) |
| `DT_NEEDED` | libc, libstdc++, libm, liblog, libGLESv1_CM |
| Imports | 398; the runtime covers all but 19: 6 are `PASSTHROUGH` in `tools/imports.cfg`, 13 have a shim in `source/ds_libc.c` |
| Symbols | natives exported by name; `.symtab` kept (20 000 names) |
| Data | 1731 files under `assets/published/` in the APK (328 MB; 104 stored, 1627 deflated), read through Java's `AssetManager` |
| Java called by the engine | 14 classes, all answered in `source/ds_java.c` |

The Vita port (v-atamanenko/deadspace-vita) targets another build: the Xperia
Play release 1.1.33 (`libEAMGameDeadSpace.so`). Its patch offsets do not
apply here; its approach to the controls does.

## Boot sequence (from classes.dex)

1. `System.loadLibrary("DeadSpace")` → 208 constructors, `JNI_OnLoad`
2. `MainActivity.onCreate` → `NativeOnCreate()` (`EA::Blast::PreInit`)
3. `AndroidEAAudioCore.Startup()` → `Init(AudioTrack, bufferBytes, channels, rate)`
4. GL thread: `NativeOnSurfaceChanged(w, h)`, then `NativeOnDrawFrame()` per
   frame, which is `EA::Blast::Loop()`
5. `onWindowFocusChanged` → `NativeOnWindowFocusChanged(bool)`

`EA::Blast::Loop()` has a state: 0 = not started, 1 = running, 2 = exited.
The first call runs `SystemAndroid::Init` (which calls back
`EAIO.Startup(AssetManager)` through Java) and sets 1; later calls post
message `0xE` to the message server (`EA::Messaging::Server`) and process
its queue. Every lifecycle native except `NativeOnSurfaceChanged` returns at
once unless the state is 1.

`0xE` reaches `EA::Blast::UpdateHandler`, which calls the update listeners,
among them `EA::core::CoreApplication::OnUpdate`. **That function asks Java
`com.eamobile.Query.isContentReady()` every frame and does nothing until it
is true**; then it runs `im::System::init()` and creates the game
(`im::IApplication::getApplication()`). On the phone the Java sets it after
the licence check and the asset download. Answering false leaves the loop
spinning with nothing to draw (the first two hardware runs: 80 000 empty
frames a second, a black screen).

`EA::Blast::LifeCycle` (state at `+48`: 0 new, 6 started, 4 resumed, 5
focused, 3 paused): `LifeCycle::Init` sends itself the raw start, which goes
straight to "focused" during `SystemAndroid::Init`, before any listener
exists. Raw messages, posted by the natives: `0x80006` resume
(`NativeOnResume`, focus true), `0x20006` focus gained, `0x40006` focus lost
(`NativeOnPause`, focus false), `0x60006` pause (`NativeOnStop`), `6` exit;
for the display `0x60005` surface created, `5` surface size.

A lesson for the tables in `ds_java.c`: no catch-all handler on a class
whose methods are not all known. The one on `Query` answered
`isContentReady()` with false, silently.

Tools: `perl tools/dexinfo.pl <classes.dex> '<class regex>' [native|code]`,
`perl tools/elfinfo.pl <lib>`, `perl tools/armdis.pl <lib> <hex offset> <hex
length>` (ARM-mode disassembly with names from `.symtab`).

## Decisions

- **Data stays in the APK.** The first start rewrites the APK with `assets/`
  stored (`store_apk_prefix`), then `AssetManager.open` is an offset into it
  through the runtime's block cache. No 328 MB copy on the SD card.
- **Buttons call the game.** This build has no gamepad support (its key
  handler knows Back and Menu only; the Xperia Play touch pad module is
  compiled out), but `Hud::doSpecialAction(action, param)`, where the Xperia
  Play build sent its buttons, is still there. The port calls it by address
  (`source/ds_engine.c`, checked against the build's instructions first).
- **The left stick is a finger** dragged from a fixed point (the Vita port's
  way). **The right stick is not:** a finger that ends a drag is a tap, and a
  tap fires or presses a menu button (the second controller test). It sends
  the player what the touch "look" pad sends: a `Vector2Event` (vtable
  `0x4794b8`, type `0x3ee`, pad 3; pad 5 in one player state) with the pixels
  a finger would have moved, which `GameObjectPlayable::onEvent` turns into
  `adjustYaw` / `adjustPitch` by the game's sensitivity setting.
- **Menus get a pointer.** They have no selection a D-pad could move, so the
  sticks move a pointer drawn over the frame (`ds_cursor.c`, on the runtime's
  `gl_blit`) and A is a finger under it, B the way back. The HUD's state (`+664`: 0 playing,
  1 the RIG, 2 and up paused) says which of the two the controller drives.
- **Sound** at audout's 48 kHz: `AudioTrack.write` goes straight to audout.

## The game's actions

`Hud::doSpecialAction(int action, int param)` at `0x7541c`; the HUD is
`GameObject::getHud()` (`0x21cb2c`), valid while `Application::getInstance()`
(`0x207ee8`) has a world (`+204`) with a player (`+88`) in a state above 1
(`+0x16a0`): the checks `Application::OnKeyDown` makes. Actions are
`0x352fb91 + n`:

| n | action |
| --- | --- |
| 0 | pause (`BTN_PLAY_PAUSE`), or the RIG |
| 1 | the RIG (`BTN_RIG`) |
| 2, 3 | previous / next weapon (which is which: untested) |
| 4 | locator |
| 5 | melee |
| 6 | reload |
| 7 | aim: param 0 pressed, -1 released (honours the aim-toggle setting) |
| 8 | fire while aiming, else melee |
| 9 | stasis while aiming, else the quick turn |
| 10 | kinesis: throw what is held, else take what is near the middle of the screen |
| 11 | accept: a menu's `BTN_UPGRADE` / `BTN_PURCHASE` / `BTN_OK` / `BTN_YES` if one takes it; else struggle when grabbed, else interact with what is near the middle of the screen (doors, items) |
| 12 | cancel: a menu's `BTN_NO` / `BTN_BACK` if one takes it; else as 0 |
| 14 | zero-gravity jump |
| 15 | the weapon's other mode, while aiming |

Most become an `ActionEvent` (type `0x3f0`) for `GameObjectPlayable::onEvent`:
6 fire, 7 alternate fire, 8 quick turn, 9 aim, 10 reload, 11 stasis, 12 jump,
13 stomp, 14 slash.

## From players' reports (0.1.0)

- **A crash when aiming with the plasma saw** (before the first weapon).
  `Hud::doSpecialAction` notes "the aim button is held" (`player+494`) before
  it checks for a weapon, and `GameObjectPlayable::onAnimEnd` then calls
  `setAiming(player[494])` with no weapon in hand (`player+724[player+744]`
  is null): a fault at `setAiming+0xd4`. The port no longer sends the aim
  action without a weapon in hand, as the touch screen has no aim button
  then.
- **Static on the protagonist's head in the hallucinations** (second chapter:
  `DementiaIdentity` swaps the helmet for `carrie_head.m3g`, whose hair is an
  RGBA texture drawn blended). The textures were decoded and are sound, and
  no GL error is logged. The likely cause, to be confirmed on hardware: the
  hallucination's full-screen static (`FSDementiaEffect`) is blended by
  destination alpha. The game's Java asks for an RGB 565 window, where
  destination alpha reads 1; the Switch's window is RGBA 8888, so the
  static would show wherever something translucent had been drawn. Mesa's
  EGL offers no window without alpha here, so the port changes the blend
  factors instead (`ds_gl.c`): on the window, `GL_DST_ALPHA` is 1 and
  `GL_ONE_MINUS_DST_ALPHA` is 0. The log says so the first time such a blend
  is asked for.
- **A fault while the game loads, on some starts** (0.1.0, and every start of
  the first 0.2.0 builds on the test console): a model that loads as nothing
  (`ModelCache::loadNodeUncached+0x168`), garbage in `Loader::loadObject3D`,
  an allocation of 1.2 GB. Both `AssetManagerJNI::Read` of the engine read
  through one shared `byte[]` of 64 KB, locked with `MonitorEnter` from
  `InputStream.read()` to `GetByteArrayRegion()`; the runtime's monitors lock
  nothing, so two loading threads read each other's bytes. The port gives the
  JNI table real ones (`ds_java.c`). The build tested before 0.1.0 logged the
  first 400 files opened, which kept the threads apart; the release took the
  log out and was not run again. Release what was run.
- newlib's `sbrk` here does not see a request that wraps the address space:
  the 1.2 GB one above was granted and `malloc` faulted writing past 4 GB,
  where a null return was due.
- **A question in the middle of a level** (a power node lock) could only be
  answered yes. Such objects take the HUD's input (`Hud::objectGetInput`:
  `hud+676` becomes 2; also the bench, the store, cinematics); the pointer
  now comes up for them as it does for the menus.

Texture files (`.m3g`, "MPP-M3G-TXCNV"): the image format is the byte at
`0x62`, the data follows a 116 or 120 byte header. 100 is RGBA 8888 without
mipmaps; 116 is ETC1 with a full mipmap chain (110 and 111 would be DXT1, 113
DXT3, 115 DXT5: `OpenGLES11Renderer::bindImage`; this build's files use 100
and 116 only). The models (`IM-M3G`) ask for
mipmapped filtering on every texture.

## Settled by the hardware runs

- `sbrk` refused: the engine's allocator goes on to `mmap` (16 MB blocks).
- `AndroidEAAudioCore.Init(track, bytes, channels, rate)`: sound plays at 48 kHz.
- `OES_matrix_palette` is not used with Mesa's extension list, and the
  textures load: no GL error is logged.

## Open

- The JNI core counts 17 000 live Java objects after a level loads: the
  engine does not delete its local references (a JVM frees them when a
  native returns). About 1 MB; it grows slowly while playing.
- Where saves go (`GetAppDataDirectory` answers `/data/data/<package>/files`,
  which the runtime maps to `data/` in the game folder).
- The runtime's `MonitorEnter` / `MonitorExit` do nothing: worth reporting
  upstream (android32). The port replaces them.
- `Java_com_ea_EAThread_EAThread_Init` and `rwfilesystem.Startup` are not
  called (no caller found in the Java).

## Roadmap

1. Controls: test motion aiming (directions, sensitivity); rumble.
2. A full play-through: saves, later levels, the store and DLC screens.
3. Performance at stock clocks; 1080p docked.
4. Other builds of the game (Google Play, Xperia Play), the console's language.
