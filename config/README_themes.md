# PCB Theme Configuration

This app can load color themes from a JSON file at:

- Relative to app: ../config/pcb_themes.json (when running from build/)
- Project root: config/pcb_themes.json

If the file is missing or invalid, the app falls back to the three built-in presets: Default, Light, High Contrast.

## File format

The JSON contains an array of theme objects. Each theme can specify:

- name: String. Shown in the toolbar dropdown.
- base: One of "Default", "Light", "HighContrast". Optional. Starts from this preset, then applies overrides.
- overridePinColors: Boolean. If true, pin and net colors are taken from the theme instead of per-net colors.
- colors: Object. Keys are color roles, values can be either:
  - Hex string "#RRGGBB" (alpha optional via separate alphas), or
  - Object with float components { "r": 0..1, "g": 0..1, "b": 0..1 }

Supported color keys (all optional):
- background
- outline
- partOutline
- pin
- sameNetPin
- ncPin
- groundPin
- ratsnet
- partHighlightBorder
- partHighlightFill

- alphas: Object with optional float values (0..1):
  - partAlpha
  - pinAlpha
  - outlineAlpha
  - partOutlineAlpha

## Minimal example

{
  "themes": [
    {
      "name": "My Dark Blue",
      "base": "Default",
      "overridePinColors": true,
      "colors": {
        "background": "#0b0f1a",
        "outline": "#7dafff",
        "pin": { "r": 0.9, "g": 0.9, "b": 1.0 },
        "sameNetPin": "#00ffaa",
        "ncPin": "#777777",
        "groundPin": "#ffaa00",
        "ratsnet": "#4fe1ff",
        "partHighlightBorder": "#ffcc00",
        "partHighlightFill": "#ffcc0033"
      },
      "alphas": {
        "partAlpha": 0.25,
        "pinAlpha": 1.0,
        "outlineAlpha": 1.0,
        "partOutlineAlpha": 1.0
      }
    }
  ]
}

## Usage

- Edit config/pcb_themes.json and save.
- Launch the app and use the toolbar dropdown to select your theme.
- Press T to cycle the built-in presets at any time.

Notes:
- Missing keys are left as-is from the selected base preset.
- If base is omitted, the theme overrides are applied on top of the current preset.
- overridePinColors = false keeps per-net pin colors even when other colors change.
