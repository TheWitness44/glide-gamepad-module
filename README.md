# glide-gamepad-module (gmcl_sdlpad)

A Garry's Mod binary module that reads Xbox and PlayStation gamepads through [SDL 3](https://github.com/libsdl-org/SDL)
and hands them to client Lua. Garry's Mod's own joystick support turns the pad into movement keys. This module gives
Lua the raw sticks, triggers and buttons, plus rumble.

It was written for the **Glide Gamepad** add-on, which drives every [Glide](https://github.com/StyledStrike/gmod-glide)
vehicle from a pad: analog steering and pedals, GTA-style flight, and a free-look camera. Any client Lua can use it.

## Install

1. Download `gmcl_sdlpad_win64.dll` from [Releases](../../releases).
2. Put it in `steamapps\common\GarrysMod\garrysmod\lua\bin\`. Create the `bin` folder if it doesn't exist.
3. Restart Garry's Mod.

Requirements:

- Windows and **64-bit Garry's Mod** (the `x86-64` beta branch).
- The module is client-side only. A server never needs it.

Notes:

- SDL is linked statically, so there's nothing else to install. The DLL imports only Windows system libraries.
- Turn the engine's own joystick off (`joystick 0`) so it doesn't read the pad as well.
- Steam Input can be on or off. With it on, Steam tells SDL to skip the real pad, and SDL reads Steam's virtual Xbox pad.
  With it off, SDL reads PlayStation pads directly over HID.
- Like any native module, it's an unsigned DLL. Get it from this repository's Releases or build it yourself.

## Lua API

`require( "sdlpad" )` puts a table in the global `sdlpad`:

| Function | Returns |
|---|---|
| `sdlpad.Update()` | Polls the pads. Returns how many are connected. Call it once per frame. |
| `sdlpad.GetPads()` | `{ { slot, name, type, vendor, product, virtual }, ... }`. `type` is SDL's (`"xboxone"`, `"ps5"`, ...); `virtual` means a Steam Input pad. |
| `sdlpad.GetState( slot, out )` | Fills `out`, as described below. Returns false for an empty slot. |
| `sdlpad.Rumble( slot, low, high, ms )` | Sets the low- and high-frequency motors, 0..1. |
| `sdlpad.RumbleTriggers( slot, left, right, ms )` | Sets the trigger motors (Xbox One / Series, DualSense), 0..1. |
| `sdlpad.Version()`, `sdlpad.Error()` | The version string, and the last SDL error. |
| `sdlpad.BUTTON.SOUTH` .. `MISC6` | Button indices, by position: `SOUTH` is A on Xbox and Cross on PlayStation. |
| `sdlpad.MAX_SLOTS` | 8 |

`GetState` fills `out` with:

- `leftx`, `lefty`, `rightx`, `righty`: -1..1, with y positive downward;
- `lefttrigger`, `righttrigger`: 0..1;
- `buttons`: a bitmask, `2 ^ sdlpad.BUTTON[name]` per button.

`GetState` reuses your table, so it makes no garbage each frame.

```lua
require( "sdlpad" )
local state = {}

hook.Add( "Think", "PadExample", function()
    if sdlpad.Update() > 0 and sdlpad.GetState( 1, state ) then
        local south = bit.band( state.buttons, 2 ^ sdlpad.BUTTON.SOUTH ) ~= 0
        -- state.leftx, state.righttrigger, ...
    end
end )
```

## Build

Visual Studio 2022 (or the Build Tools) with CMake 3.24 or newer:

```
git clone --recursive https://github.com/TheWitness44/glide-gamepad-module.git
cd glide-gamepad-module
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

SDL 3.4.18 is a submodule in `third_party/SDL`. It's built static, with only its joystick, gamepad and HIDAPI parts.
The output is `build/Release/gmcl_sdlpad_win64.dll`.

The module talks to Lua through the LuaJIT functions that Garry's Mod's `lua_shared.dll` exports, so it needs no
Garry's Mod headers.

## Test outside the game

```
build\Release\sdlpad_harness.exe "<GarrysMod>\bin\win64" build\Release\gmcl_sdlpad_win64.dll test\probe.lua 6
```

The harness loads the game's own `lua_shared.dll` and opens the module in a plain LuaJIT state. It then calls the
script's `Tick()` every 100 ms for the given seconds. `test/probe.lua` prints the pads it finds and the first pad's
state whenever it changes.

## License

zlib (see `LICENSE`). SDL is zlib too.
