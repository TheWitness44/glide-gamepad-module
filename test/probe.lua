-- Harness probe: the module's API, the pads, and slot 1's state whenever it changes.
print( sdlpad.Version(), "error: '" .. sdlpad.Error() .. "'", "slots " .. sdlpad.MAX_SLOTS, "SOUTH=" .. sdlpad.BUTTON.SOUTH )
print( "connected at open:", sdlpad.Update() )
for _, pad in ipairs( sdlpad.GetPads() ) do
    print( string.format( "  slot %d: %s [%s] %04x:%04x virtual=%s", pad.slot, pad.name, pad.type, pad.vendor,
        pad.product, tostring( pad.virtual ) ) )
end
local state, last, ticks = {}, "", 0
function Tick()
    ticks = ticks + 1
    local count = sdlpad.Update()
    if not sdlpad.GetState( 1, state ) then
        if ticks % 20 == 0 then print( "no pad in slot 1; connected " .. count ) end
        return
    end
    local line = string.format( "L %.2f %.2f R %.2f %.2f LT %.2f RT %.2f buttons %d", state.leftx, state.lefty,
        state.rightx, state.righty, state.lefttrigger, state.righttrigger, state.buttons )
    if line ~= last then print( line ) last = line end
end
