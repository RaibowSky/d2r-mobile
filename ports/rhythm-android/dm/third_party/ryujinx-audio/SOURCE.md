The mono/stereo I3dl2 DSP in `dm/host/ar_reverb3d.inc` adapts the MIT-licensed
Ryujinx Audio Renderer implementation at commit
`515fc32b21f59298ec8ca45f5d3c36e9d3041084`:

- `src/Ryujinx.Audio/Renderer/Dsp/State/Reverb3dState.cs`
- `src/Ryujinx.Audio/Renderer/Dsp/Command/Reverb3dCommand.cs`
- `src/Ryujinx.Audio/Renderer/Dsp/Effect/DelayLineReverb3d.cs`
- `src/Ryujinx.Audio/Renderer/Dsp/Effect/DecayDelay.cs`
- `src/Ryujinx.Audio/Renderer/Dsp/Effect/IDelayLine.cs`
- `src/Ryujinx.Audio/Renderer/Dsp/FloatingPointHelper.cs`

Archived source: https://git.axenov.dev/Museum/ryujinx/src/commit/515fc32b21f59298ec8ca45f5d3c36e9d3041084/src/Ryujinx.Audio/Renderer/Dsp

The float/libm reference itself approximates the console's fixed-point/LUT
operations. This adaptation does not claim bit-exact console DSP output.
Only the game's observed 48 kHz mono/stereo effects are currently supported.
The game's SDK parameter ABI was separately checked against sdk +0x223710.
Allocations occur during effect creation; all processing is after submix
accumulation and continues with zero input so that tails survive source end.
