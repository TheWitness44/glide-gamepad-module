// gmcl_sdlpad: SDL3 gamepads (Xbox, PlayStation, Switch) for Garry's Mod's client Lua.
//
// Lua (global `sdlpad`, after require( "sdlpad" )):
//   sdlpad.Update()                         poll the pads; returns how many are connected
//   sdlpad.GetPads()                        { { slot, name, type, vendor, product, virtual }, ... }
//   sdlpad.GetState( slot, out )            fills `out` (leftx, lefty, rightx, righty in -1..1, lefttrigger,
//                                           righttrigger in 0..1, buttons = bitmask of sdlpad.BUTTON); false if empty
//   sdlpad.Rumble( slot, low, high, ms )    motors 0..1
//   sdlpad.RumbleTriggers( slot, left, right, ms )
//   sdlpad.Version(), sdlpad.Error()
//
// It talks to Lua through lua_shared.dll's LuaJIT exports, so it needs no GMod headers, and a test harness can load it
// into a plain LuaJIT state from the same DLL.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>

struct lua_State;
using lua_CFunction = int ( * )( lua_State* );
using lua_Number = double;
using lua_Integer = ptrdiff_t;

namespace {

constexpr int LUA_GLOBALSINDEX = -10002;
constexpr int LUA_TTABLE = 5;
constexpr int MAX_SLOTS = 8;
constexpr float AXIS_MAX = 32767.0f;
constexpr int MODULE_VERSION = 1;

struct LuaApi {
    void ( *createtable )( lua_State*, int, int );
    void ( *setfield )( lua_State*, int, const char* );
    void ( *pushcclosure )( lua_State*, lua_CFunction, int );
    void ( *pushnumber )( lua_State*, lua_Number );
    void ( *pushinteger )( lua_State*, lua_Integer );
    void ( *pushboolean )( lua_State*, int );
    void ( *pushstring )( lua_State*, const char* );
    void ( *rawseti )( lua_State*, int, int );
    lua_Integer ( *checkinteger )( lua_State*, int );
    lua_Number ( *optnumber )( lua_State*, int, lua_Number );
    lua_Integer ( *optinteger )( lua_State*, int, lua_Integer );
    void ( *checktype )( lua_State*, int, int );
};

LuaApi lua;

template <typename T>
bool Resolve( HMODULE dll, const char* name, T& out )
{
    out = reinterpret_cast<T>( GetProcAddress( dll, name ) );
    return out != nullptr;
}

bool LoadLuaApi()
{
    HMODULE dll = GetModuleHandleW( L"lua_shared.dll" );
    if ( !dll ) return false;

    return Resolve( dll, "lua_createtable", lua.createtable ) && Resolve( dll, "lua_setfield", lua.setfield )
        && Resolve( dll, "lua_pushcclosure", lua.pushcclosure ) && Resolve( dll, "lua_pushnumber", lua.pushnumber )
        && Resolve( dll, "lua_pushinteger", lua.pushinteger ) && Resolve( dll, "lua_pushboolean", lua.pushboolean )
        && Resolve( dll, "lua_pushstring", lua.pushstring ) && Resolve( dll, "lua_rawseti", lua.rawseti )
        && Resolve( dll, "luaL_checkinteger", lua.checkinteger ) && Resolve( dll, "luaL_optnumber", lua.optnumber )
        && Resolve( dll, "luaL_optinteger", lua.optinteger ) && Resolve( dll, "luaL_checktype", lua.checktype );
}

// Section: the pads

struct Slot {
    SDL_Gamepad* pad;
    SDL_JoystickID id;
};

Slot slots[MAX_SLOTS];
bool sdlReady = false;
char lastError[256] = "";

void KeepError()
{
    std::snprintf( lastError, sizeof( lastError ), "%s", SDL_GetError() );
}

void OpenPad( SDL_JoystickID id )
{
    for ( const Slot& slot : slots ) {
        if ( slot.pad && slot.id == id ) return;
    }

    for ( Slot& slot : slots ) {
        if ( slot.pad ) continue;

        slot.pad = SDL_OpenGamepad( id );
        if ( slot.pad ) {
            slot.id = id;
        } else {
            KeepError();
        }
        return;
    }
}

void ClosePad( Slot& slot )
{
    SDL_CloseGamepad( slot.pad );
    slot = {};
}

void ClosePadById( SDL_JoystickID id )
{
    for ( Slot& slot : slots ) {
        if ( slot.pad && slot.id == id ) ClosePad( slot );
    }
}

void OpenConnectedPads()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads( &count );
    if ( !ids ) return;

    for ( int i = 0; i < count; ++i ) OpenPad( ids[i] );
    SDL_free( ids );
}

// Nothing reads SDL's event queue but this, so the per-frame axis and button events are switched off; the state is
// polled instead.
void DisableStateEvents()
{
    const SDL_EventType noisy[] = {
        SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION, SDL_EVENT_JOYSTICK_HAT_MOTION,
        SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP, SDL_EVENT_JOYSTICK_BATTERY_UPDATED,
        SDL_EVENT_JOYSTICK_UPDATE_COMPLETE, SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        SDL_EVENT_GAMEPAD_BUTTON_UP, SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION,
        SDL_EVENT_GAMEPAD_TOUCHPAD_UP, SDL_EVENT_GAMEPAD_SENSOR_UPDATE, SDL_EVENT_GAMEPAD_UPDATE_COMPLETE
    };

    for ( SDL_EventType type : noisy ) SDL_SetEventEnabled( type, false );
}

void Poll()
{
    SDL_UpdateGamepads();

    SDL_Event events[16];
    int count;
    while ( ( count = SDL_PeepEvents( events, 16, SDL_GETEVENT, SDL_EVENT_GAMEPAD_ADDED,
        SDL_EVENT_GAMEPAD_REMOVED ) ) > 0 ) {
        for ( int i = 0; i < count; ++i ) {
            if ( events[i].type == SDL_EVENT_GAMEPAD_ADDED ) {
                OpenPad( events[i].gdevice.which );
            } else if ( events[i].type == SDL_EVENT_GAMEPAD_REMOVED ) {
                ClosePadById( events[i].gdevice.which );
            }
        }
    }
    SDL_FlushEvents( SDL_EVENT_FIRST, SDL_EVENT_LAST );

    for ( Slot& slot : slots ) {
        if ( slot.pad && !SDL_GamepadConnected( slot.pad ) ) ClosePad( slot );
    }
}

// Lua slots are 1-based; nullptr for an empty or out-of-range one.
SDL_Gamepad* PadAt( lua_State* L, int arg )
{
    const lua_Integer slot = lua.checkinteger( L, arg );
    if ( slot < 1 || slot > MAX_SLOTS ) return nullptr;
    return slots[slot - 1].pad;
}

float Axis( SDL_Gamepad* pad, SDL_GamepadAxis axis )
{
    const float value = SDL_GetGamepadAxis( pad, axis ) / AXIS_MAX;
    return value < -1.0f ? -1.0f : ( value > 1.0f ? 1.0f : value );
}

Uint16 Motor( lua_State* L, int arg )
{
    const lua_Number value = lua.optnumber( L, arg, 0.0 );
    if ( value <= 0.0 ) return 0;
    if ( value >= 1.0 ) return 0xFFFF;
    return static_cast<Uint16>( value * 0xFFFF );
}

// Section: Lua functions

int L_Update( lua_State* L )
{
    if ( sdlReady ) Poll();

    int count = 0;
    for ( const Slot& slot : slots ) {
        if ( slot.pad ) ++count;
    }
    lua.pushinteger( L, count );
    return 1;
}

int L_GetPads( lua_State* L )
{
    lua.createtable( L, MAX_SLOTS, 0 );
    int index = 0;
    for ( int i = 0; i < MAX_SLOTS; ++i ) {
        SDL_Gamepad* pad = slots[i].pad;
        if ( !pad ) continue;

        const char* name = SDL_GetGamepadName( pad );
        const char* type = SDL_GetGamepadStringForType( SDL_GetGamepadType( pad ) );
        const Uint16 vendor = SDL_GetGamepadVendor( pad );

        lua.createtable( L, 0, 6 );
        lua.pushinteger( L, i + 1 );
        lua.setfield( L, -2, "slot" );
        lua.pushstring( L, name ? name : "" );
        lua.setfield( L, -2, "name" );
        lua.pushstring( L, type ? type : "unknown" );
        lua.setfield( L, -2, "type" );
        lua.pushinteger( L, vendor );
        lua.setfield( L, -2, "vendor" );
        lua.pushinteger( L, SDL_GetGamepadProduct( pad ) );
        lua.setfield( L, -2, "product" );
        // Valve's vendor id: Steam Input's virtual pad.
        lua.pushboolean( L, vendor == 0x28DE );
        lua.setfield( L, -2, "virtual" );
        lua.rawseti( L, -2, ++index );
    }
    return 1;
}

int L_GetState( lua_State* L )
{
    SDL_Gamepad* pad = PadAt( L, 1 );
    lua.checktype( L, 2, LUA_TTABLE );
    if ( !pad ) {
        lua.pushboolean( L, 0 );
        return 1;
    }

    lua.pushnumber( L, Axis( pad, SDL_GAMEPAD_AXIS_LEFTX ) );
    lua.setfield( L, 2, "leftx" );
    lua.pushnumber( L, Axis( pad, SDL_GAMEPAD_AXIS_LEFTY ) );
    lua.setfield( L, 2, "lefty" );
    lua.pushnumber( L, Axis( pad, SDL_GAMEPAD_AXIS_RIGHTX ) );
    lua.setfield( L, 2, "rightx" );
    lua.pushnumber( L, Axis( pad, SDL_GAMEPAD_AXIS_RIGHTY ) );
    lua.setfield( L, 2, "righty" );
    lua.pushnumber( L, Axis( pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER ) );
    lua.setfield( L, 2, "lefttrigger" );
    lua.pushnumber( L, Axis( pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ) );
    lua.setfield( L, 2, "righttrigger" );

    double buttons = 0;
    double bit = 1;
    for ( int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button ) {
        if ( SDL_GetGamepadButton( pad, static_cast<SDL_GamepadButton>( button ) ) ) buttons += bit;
        bit *= 2;
    }
    lua.pushnumber( L, buttons );
    lua.setfield( L, 2, "buttons" );

    lua.pushboolean( L, 1 );
    return 1;
}

int L_Rumble( lua_State* L )
{
    SDL_Gamepad* pad = PadAt( L, 1 );
    const Uint32 ms = static_cast<Uint32>( lua.optinteger( L, 4, 100 ) );
    lua.pushboolean( L, pad && SDL_RumbleGamepad( pad, Motor( L, 2 ), Motor( L, 3 ), ms ) );
    return 1;
}

int L_RumbleTriggers( lua_State* L )
{
    SDL_Gamepad* pad = PadAt( L, 1 );
    const Uint32 ms = static_cast<Uint32>( lua.optinteger( L, 4, 100 ) );
    lua.pushboolean( L, pad && SDL_RumbleGamepadTriggers( pad, Motor( L, 2 ), Motor( L, 3 ), ms ) );
    return 1;
}

int L_Version( lua_State* L )
{
    char text[64];
    const int sdl = SDL_GetVersion();
    std::snprintf( text, sizeof( text ), "sdlpad %d (SDL %d.%d.%d)", MODULE_VERSION, SDL_VERSIONNUM_MAJOR( sdl ),
        SDL_VERSIONNUM_MINOR( sdl ), SDL_VERSIONNUM_MICRO( sdl ) );
    lua.pushstring( L, text );
    return 1;
}

int L_Error( lua_State* L )
{
    lua.pushstring( L, lastError );
    return 1;
}

void SetFunction( lua_State* L, const char* name, lua_CFunction function )
{
    lua.pushcclosure( L, function, 0 );
    lua.setfield( L, -2, name );
}

void PushButtons( lua_State* L )
{
    static const char* const names[] = {
        "SOUTH", "EAST", "WEST", "NORTH", "BACK", "GUIDE", "START", "LEFT_STICK", "RIGHT_STICK", "LEFT_SHOULDER",
        "RIGHT_SHOULDER", "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "MISC1", "RIGHT_PADDLE1", "LEFT_PADDLE1",
        "RIGHT_PADDLE2", "LEFT_PADDLE2", "TOUCHPAD", "MISC2", "MISC3", "MISC4", "MISC5", "MISC6"
    };
    static_assert( sizeof( names ) / sizeof( names[0] ) == SDL_GAMEPAD_BUTTON_COUNT, "button names out of date" );

    lua.createtable( L, 0, SDL_GAMEPAD_BUTTON_COUNT );
    for ( int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button ) {
        lua.pushinteger( L, button );
        lua.setfield( L, -2, names[button] );
    }
}

bool StartSDL()
{
    // Device detection and raw input run on SDL's own thread, away from the game's message loop.
    SDL_SetHint( SDL_HINT_JOYSTICK_THREAD, "1" );
    SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );

    if ( !SDL_Init( SDL_INIT_GAMEPAD ) ) {
        KeepError();
        return false;
    }

    DisableStateEvents();
    OpenConnectedPads();
    return true;
}

} // namespace

extern "C" __declspec( dllexport ) int gmod13_open( lua_State* L )
{
    if ( !LoadLuaApi() ) return 0;

    sdlReady = StartSDL();

    lua.createtable( L, 0, 10 );
    SetFunction( L, "Update", L_Update );
    SetFunction( L, "GetPads", L_GetPads );
    SetFunction( L, "GetState", L_GetState );
    SetFunction( L, "Rumble", L_Rumble );
    SetFunction( L, "RumbleTriggers", L_RumbleTriggers );
    SetFunction( L, "Version", L_Version );
    SetFunction( L, "Error", L_Error );
    PushButtons( L );
    lua.setfield( L, -2, "BUTTON" );
    lua.pushinteger( L, MAX_SLOTS );
    lua.setfield( L, -2, "MAX_SLOTS" );
    lua.setfield( L, LUA_GLOBALSINDEX, "sdlpad" );
    return 0;
}

extern "C" __declspec( dllexport ) int gmod13_close( lua_State* )
{
    for ( Slot& slot : slots ) {
        if ( slot.pad ) ClosePad( slot );
    }
    if ( sdlReady ) SDL_Quit();
    sdlReady = false;
    return 0;
}
