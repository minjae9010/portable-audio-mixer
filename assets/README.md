# Portable Mixer logo

`portable-mixer.png` is the transparent original generated with the built-in ImageGen tool. `portable-mixer.ico` packages the same image at 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixels for Windows. The executable, window, tray, mixer header and recording header share this mark.

To regenerate the ICO with Pillow:

```python
from PIL import Image
Image.open('assets/portable-mixer.png').convert('RGBA').save(
    'assets/portable-mixer.ico',
    sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
```

Generation prompt:

```text
Use case: logo-brand
Asset type: Windows desktop application icon and in-app brand mark for Portable Mixer, an app that mixes program audio and microphones.
Primary request: A polished distinctive compact icon: three vertical mixer faders, whose stems and rounded slider caps create a harmonious audio waveform silhouette, contained in a dark rounded-square tile. Match the existing app palette: dark navy #1D212C tile, lavender #B09DFF and mint #6CD7C3 faders. Simple bold geometric flat vector-like design, crisp clean edges, balanced optical spacing, readable at 16x16 and 32x32. Three faders only, differing heights and slider positions, subtle depth through solid tones only. Square straight-on composition, tile centered filling about 90 percent of canvas, transparent outside the rounded tile. No text, no letters, no extra symbols, no gradients, no glow, no shadows outside the tile, no mockup, no watermark. Generate one finished production app icon.
```
