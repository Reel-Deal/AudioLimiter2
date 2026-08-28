## Description

**AudioLimiter2** is a reimplementation of dimzon's AudioLimiter plugin. It provides 4 modes for dynamic range compression and peak limiting.

### Requirements:

- AviSynth+ 3.7.0 or greater
- Microsoft Visual C++ Redistributable Package 2022 (can be downloaded from [here](https://github.com/abbodi1406/vcredist/releases))

----

### Syntax & Parameters

```
ExpotencialLimiter2 (clip, float "factor")
LinearLimiter2 (clip, float "factor")
WavGainLimiter2 (clip, float "factor")
SoftClipperFromAudX2 (clip, float "curve")
```

##### ***`clip`***
Input clip; must contain an audio track and it must be 32-bit float.<br>
> **Note: handling out-of-bounds audio**<br>
> Audio exceeding `[-1.0, 1.0]` is not clipped prior to processing. It is processed normally and may result in heavy compression or flattening depending on the amplitude. The final output is clamped to legal `[-1.0, 1.0]` bounds, except when `factor=0` bypasses processing entirely (see below). If needed, use [`Amplify()`](https://avisynthplus.readthedocs.io/en/latest/avisynthdoc/corefilters/amplify.html) or [`Normalize()`](https://avisynthplus.readthedocs.io/en/latest/avisynthdoc/corefilters/normalize.html) before `AudioLimiter2`.

##### ***`factor`***
Scales the audio input; controls the overall strength of the compression effect.<br>
Recommended range: `1.0` to `5.0`<br>
Default: `1.0`.<br>
> **Note:** For `ExpotencialLimiter2` and `LinearLimiter2`, `factor=0` bypasses processing; audio passes through unchanged and unclamped.

##### ***`curve`***
The threshold for the soft-clipping knee. Audio within ±curve of zero passes unmodified.<br>
Recommended range: `0.0` to `1.0`<br>
Default: `0.7`.

----

### Modes Explained

#### 1. ExpotencialLimiter2
A log-domain hybrid. It converts the audio to a logarithmic scale, applies normalized tanh compression, and transforms it back to linear space.

> output = sign(input) * (10^(tanh(factor * log10(1 + 9 * |input|)) / tanh(factor)) - 1) / 9

#### 2. LinearLimiter2
Applies a continuous S-curve (tanh) to the entire audio signal. It boosts quiet sounds while gently rounding off loud peaks rather than hard-clipping them.

> output = tanh(factor * input) / tanh(factor)

#### 3. WavGainLimiter2
A soft-knee limiter. `input * factor` stays linear while within ±0.5; beyond that, it's tanh-compressed toward ±1.0. The linear zone shrinks as `factor` grows. At `factor=2.0`, only inputs within ±0.25 stay linear.

#### 4. SoftClipperFromAudX2
A piecewise clipper. Audio within `[-curve, curve]` passes through untouched. Beyond that, it's sine-rounded toward full scale, then hard-clipped. Positive peaks stop just under full scale (32767.0 / 32768.0), negative peaks stop at exactly -1.0.

---

### Usage Example

```
# Load your source
BSAudioSource("audio.wav")

# Convert audio to 32-bit float
ConvertAudioToFloat()

# Apply the Linear Limiter with a factor of 2.0
LinearLimiter2(factor=2.0)
```
