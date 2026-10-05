# deadspace_nx — port notes

Port of **Dead Space 1.2.0** (`com.eamobile.deadspace_full_azn`, versionCode
1200, the Amazon Appstore build, armeabi) to Nintendo Switch on the
[android32](https://github.com/aks796/android32) runtime (submodule at
`runtime/`, commit `50b352c`).

State: **boots on hardware up to the engine's own start** (setup, loader,
constructors, `JNI_OnLoad`, EGL / GLES 1 context, `EAIO.Startup`); the game
itself has not drawn yet. Every "unverified" below is a guess to settle with
the next `debug.log`.

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
`EAIO.Startup(AssetManager)` through Java) and sets 1; later calls send
message `0xE` to the message dispatcher and pump it. **Every lifecycle native
except `NativeOnSurfaceChanged` returns at once unless the state is 1**, so
what a phone sends before the first frame (`NativeOnResume`,
`NativeOnSurfaceCreated`) is lost, and the game starts on the focus event
that follows the first frame. Sending the focus before it leaves the loop
spinning with nothing to do (the first hardware run: 80 000 empty frames a
second).

Lifecycle messages (`dispatcher->vtbl[8](id, ...)`): `0x80006` resume,
`0x20006` focus gained, `0x40006` pause / focus lost, `0x60006` stop,
`0x60005` surface created, `5` surface size.

Tools: `perl tools/dexinfo.pl <classes.dex> '<class regex>' [native|code]`,
`perl tools/elfinfo.pl <lib>`, `perl tools/armdis.pl <lib> <hex offset> <hex
length>` (ARM-mode disassembly with names from `.symtab`).

## Decisions

- **Data stays in the APK.** The first start rewrites the APK with `assets/`
  stored (`store_apk_prefix`), then `AssetManager.open` is an offset into it
  through the runtime's block cache. No 328 MB copy on the SD card.
- **The engine is told it runs on an Xperia Play (R800i).** That is the one
  phone with a gamepad the game supports (`KeyboardAndroidXperiaPlay`,
  `TouchPadAndroidXperiaPlay`), which gives real button actions.
- **Sticks** as in the Vita port: the left one is a finger dragged on the
  screen, the right one a fresh drag on the Xperia touch pad every frame.
- **Sound** at audout's 48 kHz: `AudioTrack.write` goes straight to audout.

## Unverified (check in the first logs)

- That this build's Xperia Play code switches on for model `R800i`.
- `AndroidEAAudioCore.Init`'s integer order (buffer bytes, channels, rate),
  taken from the Vita port.
- Whether `OES_matrix_palette` is really used (skinned models): Mesa does not
  have it. The GL extension string is logged at start-up.
- Texture formats inside the `.m3g` files (PVRTC / ATC / ETC would need
  decoding in software).
- `sbrk` is refused, `androidGetTmpRoot` / `androidGetExternalRoot` answer
  paths: each logs when called.
- Where saves go (`GetAppDataDirectory` answers `/data/data/<package>/files`,
  which the runtime maps to `data/` in the game folder).
- `Java_com_ea_EAThread_EAThread_Init` and `rwfilesystem.Startup` are not
  called (no caller found in the Java).

## Roadmap

1. Boot to the first frame on hardware; fix what the log shows.
2. Menus and a level: textures, skinning, sound.
3. Controls: tune the sticks; the accelerometer gestures (tilt = alternate
   fire, shake = zero-G jump) on buttons and on the controller's motion
   sensor; gyroscope aiming; rumble.
4. Performance at stock clocks; 1080p docked.
5. Other builds of the game (Google Play, Xperia Play), the console's language.
