# The free set

Stand ins for everything in `assets/roccat/`, same file names (and mostly the same sizes). Nothing in here came from ROCCAT, its all my drawings. They are under the same license as the code (GPL-3.0-only).

The mice and keyboards are semi-hand traced outlines of the devices with the details drawn in by hand, the logo is a cat and the lettering is in Michroma. The light zones of a mouse are drawn unlit in `top.png`, every led gets a white mask (`lightN.png`) the gui paints its colour through. Keyboards are the board (`body.png`) and the caps (`keycaps-*.png`), there are no prints of my own yet so the legends are empty.

Not everything is drawn yet. Whatever isnt (or wasnt good enough yet) is a see through picture of the right size, the gui still draws keys, markers and colours by itself in that case. Device icons that arent drawn on their own use the drawing of the mouse or keyboard with that id for now.

`-DROCKCAT_ROCCAT_ASSETS=OFF` makes the build use this folder.

The finished ones are in [SHOWCASE.md](SHOWCASE.md) (the `showcase/` pictures never end up in the build).

Todo : Fix fallbacks for device icons where multiple angles are shown.