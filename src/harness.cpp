// sdlpad_harness: loads GMod's lua_shared.dll, opens gmcl_sdlpad in a plain LuaJIT state, and runs a test script.
// The script may define Tick(), which is called every 100 ms for the given number of seconds.
//
//   sdlpad_harness.exe <GarrysMod\bin\win64> <gmcl_sdlpad_win64.dll> <test.lua> [seconds]

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

struct lua_State;

namespace {

constexpr int LUA_GLOBALSINDEX = -10002;

using NewState = lua_State* ( * )();
using OpenLibs = void ( * )( lua_State* );
using LoadFile = int ( * )( lua_State*, const char* );
using PCall = int ( * )( lua_State*, int, int, int );
using ToLString = const char* ( * )( lua_State*, int, size_t* );
using GetField = void ( * )( lua_State*, int, const char* );
using Type = int ( * )( lua_State*, int );
using SetTop = void ( * )( lua_State*, int );
using Close = void ( * )( lua_State* );
using ModuleFunction = int ( * )( lua_State* );

using GetTop = int ( * )( lua_State* );
using PushCClosure = void ( * )( lua_State*, ModuleFunction, int );
using SetField = void ( * )( lua_State*, int, const char* );

// GMod's print goes to the engine console, which a plain process doesn't have: print to stdout instead.
GetTop gGetTop;
ToLString gToString;

int HarnessPrint( lua_State* L )
{
    const int count = gGetTop( L );
    for ( int i = 1; i <= count; ++i ) {
        const char* text = gToString( L, i, nullptr );
        std::printf( "%s%s", i > 1 ? "\t" : "", text ? text : "(non-string)" );
    }
    std::printf( "\n" );
    return 0;
}

template <typename T>
T Get( HMODULE dll, const char* name )
{
    T function = reinterpret_cast<T>( GetProcAddress( dll, name ) );
    if ( !function ) {
        std::fprintf( stderr, "missing export %s\n", name );
        std::exit( 2 );
    }
    return function;
}

} // namespace

int wmain( int argc, wchar_t** argv )
{
    if ( argc < 4 ) {
        std::fprintf( stderr, "usage: sdlpad_harness <bin\\win64> <module.dll> <test.lua> [seconds]\n" );
        return 1;
    }

    const std::wstring bin = argv[1];
    SetDllDirectoryW( bin.c_str() );
    HMODULE shared = LoadLibraryW( ( bin + L"\\lua_shared.dll" ).c_str() );
    if ( !shared ) {
        std::fprintf( stderr, "could not load lua_shared.dll (%lu)\n", GetLastError() );
        return 2;
    }

    auto newState = Get<NewState>( shared, "luaL_newstate" );
    auto openLibs = Get<OpenLibs>( shared, "luaL_openlibs" );
    auto loadFile = Get<LoadFile>( shared, "luaL_loadfile" );
    auto pcall = Get<PCall>( shared, "lua_pcall" );
    auto toString = Get<ToLString>( shared, "lua_tolstring" );
    auto getField = Get<GetField>( shared, "lua_getfield" );
    auto type = Get<Type>( shared, "lua_type" );
    auto setTop = Get<SetTop>( shared, "lua_settop" );
    auto close = Get<Close>( shared, "lua_close" );

    std::fprintf( stderr, "[harness] lua_shared loaded\n" );
    HMODULE module = LoadLibraryW( argv[2] );
    if ( !module ) {
        std::fprintf( stderr, "could not load the module (%lu)\n", GetLastError() );
        return 2;
    }
    auto open = Get<ModuleFunction>( module, "gmod13_open" );
    auto shut = Get<ModuleFunction>( module, "gmod13_close" );

    std::fprintf( stderr, "[harness] module loaded\n" );
    lua_State* L = newState();
    std::fprintf( stderr, "[harness] state %p\n", static_cast<void*>( L ) );
    openLibs( L );
    gGetTop = Get<GetTop>( shared, "lua_gettop" );
    gToString = toString;
    Get<PushCClosure>( shared, "lua_pushcclosure" )( L, HarnessPrint, 0 );
    Get<SetField>( shared, "lua_setfield" )( L, LUA_GLOBALSINDEX, "print" );
    std::fprintf( stderr, "[harness] libs open\n" );
    open( L );
    std::fprintf( stderr, "[harness] module open\n" );

    char script[MAX_PATH];
    WideCharToMultiByte( CP_UTF8, 0, argv[3], -1, script, sizeof( script ), nullptr, nullptr );
    if ( loadFile( L, script ) != 0 || pcall( L, 0, 0, 0 ) != 0 ) {
        std::fprintf( stderr, "script error: %s\n", toString( L, -1, nullptr ) );
        return 3;
    }

    const int seconds = argc > 4 ? _wtoi( argv[4] ) : 0;
    for ( int tick = 0; tick < seconds * 10; ++tick ) {
        Sleep( 100 );
        getField( L, LUA_GLOBALSINDEX, "Tick" );
        if ( type( L, -1 ) == 0 ) {
            setTop( L, 0 );
            break;
        }
        if ( pcall( L, 0, 0, 0 ) != 0 ) {
            std::fprintf( stderr, "Tick error: %s\n", toString( L, -1, nullptr ) );
            return 3;
        }
        std::fflush( stdout );
    }

    shut( L );
    close( L );
    FreeLibrary( module );
    std::printf( "closed cleanly\n" );
    return 0;
}
