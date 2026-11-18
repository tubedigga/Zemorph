# ZEMORPH

Version 1.1.0

A morphing filter that blends two fixed frequency shapes. Stereo insert for Akai MPC and Force standalone hardware.

[tubedigga.com](https://www.tubedigga.com)

## What it does

ZEMORPH is an insert. You pick two frames, A and B, from a fixed list of shapes. The shapes are peaks and dips baked in. You do not move one peak on its own.

Morph blends A into B. Offset moves the whole current shape up or down. Depth sets how hard the shape cuts and boosts. Output is the level, so a dark shape can be turned up, then morphed back toward the frequencies you want. The shapes cut and boost hard enough that a sweep has a sound of its own.

## Compatibility

Built for MPC OS 3.10.1 and later, with External Plugin Support.

Turn on both **Enable Enhancements** and **External Plugin Support** under Menu, Preferences, Enhancements (Beta).

The plugin is a stereo insert. It does not load in MPC Software on a computer. That application hosts a different plugin format.

32-bit machines (MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force) load the arm32 build. 64-bit machines (MPC Live III, MPC XL, and later) load the arm64 build. Both are in the folder.

## Installation

The install folder is already in this repo. You do not need to compile it.

Download `build/Tubedigga - VST - ZEMORPH` from the branch. On GitHub that path is:

https://github.com/tubedigga/Zemorph/tree/cursor/zemorph-morph-filter-d103/build/Tubedigga%20-%20VST%20-%20ZEMORPH

On the root of a USB stick, SD card, or the internal drive, create a folder named exactly `VST2 Plugins` if it is not there already.

Copy the whole folder `Tubedigga - VST - ZEMORPH` into `VST2 Plugins`. The drive has to be inserted whenever you want the effect.

In an insert slot, ZEMORPH is listed under External Plugins.

This copy has not been loaded on hardware. See the note at the end.

## Screen

One page, MORPH. Knobs and labels from the MPC skin. No custom drawing.

Top row, left to right: Morph, Offset, Depth, Output.

Second row: Frame A, Frame B, Guard, and a line that says Held steps the filter out. Guard's two positions read Live and Held.

Frame A and Frame B open a list when you double-tap the knob. The names are Two Peak, Comb, Formant, Nasal, Hollow, Bright, Dark, and Wide.

In Program Q-Link mode the first four Q-Links are Morph, Offset, Depth, and Output. On the plugin screen those same four sit on the top row of Q-Links.

## Controls

### Morph

0% is frame A. 100% is frame B. In between, the filter blends the two shapes. Default is 0%.

### Offset

Moves the whole current shape up or down, from −24 to +24 semitones. 0 is the pitch the shape was baked at. Default is 0.

### Depth

How hard the shape cuts and boosts. 0% is flat. 100% is the full baked shape. Default is 75%.

### Output

Makeup gain, from −18 dB to +18 dB. 0 dB leaves the level alone. Use it when a dark frame has pulled the level down. Default is 0 dB.

### Frame A and Frame B

The two shapes being blended. The list is fixed.

### Guard

The two positions are labelled Live and Held. Live means the filter is in the path. Held means it has stepped out and you are hearing the input (still with the output knob, the ducker, and the limiter).

Turn Guard back to Live to bring the filter in again. If the level is still climbing it will hold again.

## Safety

These stay on. There is no switch for them.

- Resonances stay inside a fixed limit, short of a runaway ring.
- The shape cannot get louder than a fixed ceiling, even where a blend would pile peaks on top of each other.
- A peak detector ducks the output when it goes over full scale.
- A ceiling limiter holds the output just under full scale.
- If the filter level keeps rising anyway, the filter steps out and Guard shows Held.

Depth at 0% and Output at 0 dB is a flat insert. Bring the other controls home and the sound returns to the input, apart from the limiter on a signal that is already over.

## Programs

Two Peak, Mid Blend, Comb, Vowels, Dark Low, and To Bright. They are starting points. The MPC stores the one you pick with the project.

## Building

The source is an MPC Plugin Builder SDK project. From a checkout of [mpc-plugin-builder-sdk](https://github.com/inmusic-software/mpc-plugin-builder-sdk):

```
python3 tools/build.py /path/to/Zemorph
```

The folder to copy onto the MPC is `build/Tubedigga - VST - ZEMORPH` inside this repo.

After that build, run `python3 tools/show_guard_labels.py "build/Tubedigga - VST - ZEMORPH/Plugin Skins/TUI.json"`. It keeps the Guard switch labelled Live and Held. Without it, both positions come up as Guard.

`tests/safety_test.cpp` checks the coefficient limits and the hold detector. The SDK host simulator checks the plugin the way the MPC loads it. Neither of those is a run on the hardware.

## Licence

MIT. Copyright 2026 Tubedigga.

Built against the MPC Plugin Builder SDK (MIT, Copyright 2026 inMusic Brands, Inc.).

VST is a trademark of Steinberg Media Technologies GmbH. MPC is an Akai product.

## What has not been tried on an MPC

The previous build was heard on an MPC. This 1.1.0 build has not been loaded. Not on a Live III, and not on any other model.

Still to confirm on a device: the plugin shows up under External Plugins, the MORPH page and the Q-Links match this note, Guard's two positions read Live and Held and it flips to Held on screen when the filter holds, a project saves and reopens with the same frames, and the load stays comfortable on a 2 GB machine with a normal project beside it. The CPU figure from the host simulator is a desktop number. The design target is the slower 2 GB machines, well under the SDK's guide of about 15% of one core, but that has not been measured on the device.
