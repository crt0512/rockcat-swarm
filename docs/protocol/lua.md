# Lua

`1e7d:2c2e`, the small ambidextrous one from before Swarm. From roccat-tools 5.9.1 (`lua/libroccatlua/`). Code: `src/lib/families/lua.cpp`, pretend one `lua`.

It can be told things but never asked. Everything goes out as one 8 byte feature report, `[03 command data[5] sum]`, sum = the byte sum of command and data. There is no status report to wait on, roccat-tools just leaves 100 ms between two commands and we do the same. Reading report 03 gives zeros on the real one, nothing in there.

So there are no profiles on the mouse and nothing to read: what the gui and the cli show is what we last sent, kept in the config dir (`~/.config/rockcat/2c2e.ini`, `lua_settings`). Until something was sent the factory settings are shown, which is what a Lua nobody touched has. Whatever roccat-tools or Windows sent to it before, we cant know.

A write sends everything, every time, in this order (a bit over a second):

| command | data | what |
|---|---|---|
| `a0` | first five dpi steps that are on, as 1 to 7 | dpi list |
| `a1` | steps six and seven, how many are on | |
| `a2` | the step to use now, 1 to 7 | 250, 500, 1000, 1250, 1500, 1750, 2000 |
| `b0` | left, right, middle, the button behind the wheel, wheel up | what the buttons do. left handed = left and right swapped right here |
| `b1` | wheel down | |
| `c0` | 0 1000 Hz, 1 500, 2 250, 3 125 | polling |
| `d0` | `11` on, `20` breathing, `10` off | light |
| `ee` | | done, 500 ms after the light |

Button types: `01` click, `02` menu, `04` universal scroll, `08` forward, `10` back, `81` dpi cycle, `82` scroll up, `83` scroll down, `85` off. Nothing else, no keys, no shortcuts, no macros.

The dpi steps are fixed, a step can only be switched on or off (at least one stays on).

On the real one: all eight commands went through (factory settings, 2026-11-23). Whether the mouse did what it was told needs somebody holding it: switch the light to breathing, take a dpi step out, swap the hands.
