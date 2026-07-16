/*
 * Demo DSP regression test — how to unit-test the plugin's audio processing
 * with no host, using DPF's PluginExporter harness. `make test` builds and
 * runs every tests/test_*.cpp under AddressSanitizer, so out-of-bounds
 * reads/writes in DSP buffers become hard failures instead of noise.
 * See "Testing the DSP" in INSTRUCTIONS.md.
 *
 * The pattern:
 *   1. Set the d_next* globals BEFORE constructing PluginExporter — the
 *      Plugin constructor reads them (it asserts if they are unset).
 *   2. Drive parameters with setParameterValue(). Indices are the
 *      plugin-side enum values (kParamGain == 0); the LV2 port numbers you
 *      see in the TTL are offset by the audio ports — don't mix them up.
 *   3. activate(), then run() over buffers you synthesize, and assert on
 *      the output. Parameter changes take effect on the next run() call.
 *   4. Assert exact samples for simple DSP (gain, panning, clamping);
 *      for resonant or smoothed DSP measure RMS over a window after a
 *      warm-up instead of single samples.
 *
 * Each test is a plain main() that prints FAIL lines and returns nonzero
 * on failure — no test framework.
 */

#include "src/DistrhoPluginInternal.hpp"

#include <cmath>
#include <cstdio>

USE_NAMESPACE_DISTRHO

static bool requestStub(void*, uint32_t, float)
{
    return true;
}

static int gFailures = 0;

static void expectNear(const char* what, float actual, float expected, float tol)
{
    if (std::fabs(actual - expected) > tol)
    {
        std::printf("FAIL: %s — expected %.6f, got %.6f\n", what, expected, actual);
        ++gFailures;
    }
}

int main()
{
    d_nextBufferSize = 512;
    d_nextSampleRate = 48000.0;
    d_nextCanRequestParameterValueChanges = true;

    PluginExporter plugin(nullptr, nullptr, requestStub, nullptr);
    plugin.activate();

    const uint32_t kBlock = 512;
    const uint32_t kParamGain = 0;

    float inBuf[kBlock], outL[kBlock], outR[kBlock];
    const float* inputs[1]  = { inBuf };
    float*       outputs[2] = { outL, outR };

    // A quarter-scale 440 Hz sine as the probe signal.
    for (uint32_t i = 0; i < kBlock; ++i)
        inBuf[i] = 0.25f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * i / 48000.0));

    // Default gain (0 dB): output equals input, duplicated to both channels.
    plugin.run(inputs, outputs, kBlock);
    expectNear("unity gain at default", outL[100], inBuf[100], 1e-6f);
    expectNear("mono -> stereo duplication", outR[100], outL[100], 0.0f);

    // +6 dB is a linear factor of 10^(6/20) ~= 1.9953.
    plugin.setParameterValue(kParamGain, 6.0f);
    plugin.run(inputs, outputs, kBlock);
    expectNear("+6 dB boost", outL[100], inBuf[100] * 1.99526f, 1e-4f);

    // -12 dB ~= x0.2512.
    plugin.setParameterValue(kParamGain, -12.0f);
    plugin.run(inputs, outputs, kBlock);
    expectNear("-12 dB cut", outL[100], inBuf[100] * 0.251189f, 1e-4f);

    // Out-of-range host writes clamp to the declared range.
    plugin.setParameterValue(kParamGain, 99.0f);
    expectNear("clamp to +24 dB", plugin.getParameterValue(kParamGain), 24.0f, 0.0f);

    if (gFailures != 0)
    {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }

    std::printf("ok: gain scales, duplicates to stereo, and clamps\n");
    return 0;
}
