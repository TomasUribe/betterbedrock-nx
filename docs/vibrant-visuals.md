# Vibrant Visuals on a Switch 1

How BetterBedrock NX turns Vibrant Visuals on in Minecraft for the original Switch,
and what each part changes. Measured on Minecraft **1.26.44** (main NSO build id
`457118E249B45EA50CA0F4CF70594461613CFDFA`), a Mariko Switch, firmware 22.5.0 and
Atmosphère 1.11.2.

## What the game already has

The Switch version shares its data with the Switch 2 version, and the Switch 1
build compiles the whole Vibrant Visuals renderer in:

- **Shaders.** All of Vibrant Visuals' materials ship in `renderer/materials/`
  (deferred shading, the G-buffer prepasses, screen-space reflections, volumetric
  fog, light clustering, shadows, sky probes...). The Switch uses **Vulkan**: the
  shaders are SPIR-V, compiled by the console's driver when the game runs, so the
  ones made for Switch 2 run on the Switch 1's GPU too.
- **Textures.** The PBR resource pack (`resource_packs/vanilla_1.21.90`, capability
  `pbr`, 2,730 texture sets) is there.
- **Settings.** `renderer/platform_config/switch/` holds Vibrant Visuals settings for
  "switch" - Switch 2 numbers: 720p/1080p handheld, 1080p/1440p docked.

What keeps it off is code, in four places.

## The patch

An Atmosphère exefs patch (IPS32, in `/atmosphere/exefs_patches/betterbedrock-nx-vv/`,
named after the build id so Atmosphère applies it to that build only). Addresses are
offsets in the decompressed main NSO; the IPS records add Atmosphère's 0x100-byte
NSO header.

1. **The device's "deferred supported" flag.** `RayTracingHardwareOptions`' constructor
   asks the renderer's feature query whether deferred shading is supported (on Vulkan
   it says yes), then ignores the answer and stores `false` - the Switch 1 build's
   compile-time switch.
   - `0x5c93068` `blr x8` -> `mov w0, #1`
   - `0x5c93070` `strb wzr, [x19, #25]` -> `strb w0, [x19, #25]`

   With this alone, Vibrant Visuals becomes selectable in Video > Mode.
2. **The availability checks.** `RayTracingOptions` allows deferred shading only when
   the hardware flag is set *and* the resource packs pass a capability check. Four
   copies (two checks and their second-interface thunks) return true once the hardware
   check passes:
   - `0x5c94234`, `0x5c94298`, `0x5c94548`, `0x5c945ac`: `ldr x8, [xN]` -> `mov w0, #1`
   - the next instruction of each: `mov x0, xN` -> `b` to the function's epilogue
3. **Reading the mode back.** `OptionRegistry::getGraphicsMode` returns
   `isRayTracingActive() ? 3 : (mode != 0)` - so Vibrant Visuals (2) reads back as
   Fancy (1), and the menu resets to Fancy.
   - `0x5c90b9c` `cset w0, ne` -> `mov w0, w8` (the stored mode)
4. **The lighting model.** `MinecraftGame::_updateLightingModel` only chooses between
   ray traced and classic lighting. It now maps the graphics mode to the lighting
   model: 3 -> ray traced (2), 2 -> deferred (1), anything else -> classic (0).
   - `0x41cd26c` `ldr x8, [x8, #600]` -> `ldr x8, [x8, #112]` (call getGraphicsMode)
   - `0x41cd278` `tst w0, #1` -> `subs w9, w0, #1`
   - `0x41cd27c` `mov w9, #2` -> `csel w1, w9, wzr, gt`
   - `0x41cd280` `csel w1, w9, wzr, ne` -> `nop`

Graphics modes: 0 simple, 1 fancy, 2 Vibrant Visuals, 3 ray traced. Lighting models:
0 classic, 1 deferred, 2 ray traced.

The app carries these bytes in `common/graphics.c`; `tests/check_graphics.py` checks
them against this list.

## The profiles

LayeredFS replaces three files under
`/atmosphere/contents/0100D71004694000/romfs/renderer/platform_config/switch/`:

- `platform_configuration.switch.json` - the tiers. The game has two per mode
  (Performance and Quality) and a preset menu to choose between them, which the
  Switch 1 build doesn't show; both tiers get the same values, so whichever one the
  game uses gets the profile. Values the game's own configs use: resolutions
  480p/540p/720p/1080p/1440p/2160p, upscaling bilinear or taau, lighting at half or
  full resolution.
- `shadow_configuration.switch.json` - one cascade; map size, distance, and
  `update_frequency` (redraw every N frames: Mojang's Android settings use 2).
- `render_distance_configuration.switch.json` - the steps of the Vibrant Visuals
  render distance slider.

The app writes these from its own templates and values (`common/graphics.c`); they
follow the game's format, and a few fixed tuning constants (shadow bias, light
quantization) are the game's own. No game files are distributed.

## Porting to a new Minecraft version

The patch matches one build; after a game update Atmosphère skips it. To port it:
dump the update's ExeFS (nxdumptool), decompress `main`, and find the same code:

- `17RayTracingOptions` and `25RayTracingHardwareOptions` (type names) lead to their
  vtables through the RELR pointer table; the hardware options' constructor stores
  `wzr` at `+25` right after calling the feature query.
- The availability checks are the `RayTracingOptions` vtable entries that call entry
  17 after entry 10.
- `getGraphicsMode` is the `OptionRegistry` entry that returns 3 when ray tracing is
  active and otherwise `option(711) != 0` (option 711 = `graphics_mode`).
- `_updateLightingModel` is what option 711's observer calls; it sets the lighting
  model through `RayTracingOptions`' `setLightingModel` (vtable `+184`).
