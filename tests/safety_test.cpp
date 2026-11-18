// Checks the shape math: pole radius, cascade level, coefficient blend,
// and the hold detector. Not a substitute for the host simulator.

#include "shape.h"

#include "dsp/biquad.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int gFails = 0;

static void expect(bool ok, const char* what)
{
    if (ok) return;
    if (gFails < 12)
        std::fprintf(stderr, "FAIL: %s\n", what);
    ++gFails;
}

static void testRadiusGrid()
{
    const double sr = 44100.0;
    float worstR = 0.0f;
    float worstMag = 0.0f;
    int worstShape = 0;
    for (int a = 0; a < zemorph::kShapes; ++a) {
        for (int b = 0; b < zemorph::kShapes; ++b) {
            for (int mi = 0; mi <= 8; ++mi) {
                for (int si = -2; si <= 2; ++si) {
                    for (int di = 0; di <= 4; ++di) {
                        mpc::dsp::BiquadCoeffs c[zemorph::kSections];
                        const float morph = (float)mi / 8.0f;
                        const float offset = (float)si * 12.0f;
                        const float depth = (float)di / 4.0f;
                        zemorph::designCascade(a, b, morph, offset, depth, sr, c);
                        float mag = 1.0f;
                        for (int s = 0; s < zemorph::kSections; ++s) {
                            const float r = zemorph::poleRadius(c[s]);
                            if (r > worstR) worstR = r;
                            expect(std::isfinite(r) && r <= zemorph::kPoleRadiusMax + 1.0e-4f,
                                   "pole radius inside the safe limit");
                            expect(std::isfinite(c[s].b0) && std::isfinite(c[s].a1) && std::isfinite(c[s].a2),
                                   "coefficients finite");
                        }
                        const float nyquist = 0.45f * (float)sr;
                        for (int i = 0; i < 24; ++i) {
                            const float hz = 40.0f * std::pow(nyquist / 40.0f, (float)i / 23.0f);
                            const float w = 2.0f * mpc::dsp::kPi * hz / (float)sr;
                            float m = 1.0f;
                            for (int s = 0; s < zemorph::kSections; ++s)
                                m *= zemorph::sectionMagnitude(c[s], w);
                            if (m > mag) mag = m;
                        }
                        if (mag > worstMag) {
                            worstMag = mag;
                            worstShape = a;
                        }
                        expect(std::isfinite(mag) && mag <= zemorph::kCascadeMax * 1.05f,
                               "cascade stays under the level ceiling");
                    }
                }
            }
        }
    }
    std::printf("radius grid: worst pole %.5f (limit %.3f), worst mag %.2f (%+.1f dB) on shape %d\n",
                worstR, zemorph::kPoleRadiusMax, worstMag,
                20.0 * std::log10(worstMag > 1.0e-8f ? worstMag : 1.0e-8f), worstShape);

    mpc::dsp::BiquadCoeffs blown;
    blown.b0 = 1.0f;
    blown.b1 = 0.0f;
    blown.b2 = 0.0f;
    blown.a1 = 0.0f;
    blown.a2 = 1.2f; // outside the unit circle
    zemorph::enforcePoleRadius(blown);
    expect(zemorph::poleRadius(blown) <= zemorph::kPoleRadiusMax + 1.0e-4f,
           "an unstable section is pulled inside");
}

static void testEndpoints()
{
    const double sr = 44100.0;
    mpc::dsp::BiquadCoeffs shifted[zemorph::kSections];
    mpc::dsp::BiquadCoeffs aOnly[zemorph::kSections];
    mpc::dsp::BiquadCoeffs bOnly[zemorph::kSections];
    mpc::dsp::BiquadCoeffs ignoreB[zemorph::kSections];
    zemorph::designCascade(0, 7, 0.0f, 0.0f, 1.0f, sr, aOnly);
    zemorph::designCascade(0, 0, 0.0f, 0.0f, 1.0f, sr, ignoreB);
    zemorph::designCascade(0, 7, 1.0f, 0.0f, 1.0f, sr, bOnly);
    zemorph::designCascade(0, 7, 0.0f, 3.0f, 1.0f, sr, shifted);
    bool same = true;
    for (int s = 0; s < zemorph::kSections; ++s) {
        if (std::fabs(aOnly[s].b0 - ignoreB[s].b0) > 1.0e-5f) same = false;
        if (std::fabs(aOnly[s].a1 - ignoreB[s].a1) > 1.0e-5f) same = false;
        if (std::fabs(aOnly[s].a2 - ignoreB[s].a2) > 1.0e-5f) same = false;
    }
    expect(same, "morph 0 matches frame A and ignores frame B");
    bool moved = false;
    for (int s = 0; s < zemorph::kSections; ++s)
        if (std::fabs(aOnly[s].a1 - shifted[s].a1) > 1.0e-3f) moved = true;
    expect(moved, "offset moves the coefficients");

    mpc::dsp::BiquadCoeffs flat[zemorph::kSections];
    zemorph::designCascade(1, 5, 0.4f, -6.0f, 0.0f, sr, flat);
    bool isFlat = true;
    for (int s = 0; s < zemorph::kSections; ++s) {
        if (std::fabs(flat[s].b0 - 1.0f) > 1.0e-4f) isFlat = false;
        if (std::fabs(flat[s].a1) > 1.0e-4f || std::fabs(flat[s].a2) > 1.0e-4f) isFlat = false;
    }
    expect(isFlat, "depth 0 is flat");

    // Frame B at morph 1 differs from frame A.
    bool differs = false;
    for (int s = 0; s < zemorph::kSections; ++s)
        if (std::fabs(aOnly[s].a1 - bOnly[s].a1) > 1.0e-3f) differs = true;
    expect(differs, "frame B is a different shape from frame A");
}

static void testCharacter()
{
    // Full depth, no offset. v1's frames barely moved the level. Each frame
    // here has to push or cut by at least 8 dB somewhere, still after the
    // cascade ceiling has done its job.
    const double sr = 44100.0;
    const float nyquist = 0.45f * (float)sr;
    int strong = 0;
    for (int a = 0; a < zemorph::kShapes; ++a) {
        mpc::dsp::BiquadCoeffs c[zemorph::kSections];
        zemorph::designCascade(a, a, 0.0f, 0.0f, 1.0f, sr, c);
        float peak = 0.0f;
        float dip = 1.0e6f;
        for (int i = 0; i < 96; ++i) {
            const float hz = 40.0f * std::pow(nyquist / 40.0f, (float)i / 95.0f);
            const float w = 2.0f * mpc::dsp::kPi * hz / (float)sr;
            float m = 1.0f;
            for (int s = 0; s < zemorph::kSections; ++s)
                m *= zemorph::sectionMagnitude(c[s], w);
            if (m > peak) peak = m;
            if (m < dip) dip = m;
        }
        const float peakDb = 20.0f * std::log10(peak > 1.0e-8f ? peak : 1.0e-8f);
        const float dipDb = 20.0f * std::log10(dip > 1.0e-8f ? dip : 1.0e-8f);
        std::printf("shape %d %-9s peak %+.1f dB  dip %+.1f dB\n",
                    a, zemorph::kShapeNames[a], peakDb, dipDb);
        if (peakDb >= 8.0f || dipDb <= -8.0f)
            ++strong;
        expect(std::isfinite(peakDb) && std::isfinite(dipDb), "shape response is finite");
    }
    expect(strong == zemorph::kShapes, "every frame moves the level by at least 8 dB");
}

static void testDetector()
{
    zemorph::GuardDetector g;
    bool tripped = false;
    for (int i = 0; i < 20; ++i)
        tripped = g.observe(0.2f, 0.5f, false) || tripped;
    expect(!tripped && !g.held, "a steady level does not hold");

    zemorph::GuardDetector climb;
    int engagedAt = -1;
    for (int i = 0; i < 16; ++i) {
        const float wet = 0.5f * std::pow(1.2f, (float)i);
        if (climb.observe(0.2f, wet, false) && engagedAt < 0)
            engagedAt = i;
    }
    expect(climb.held && engagedAt >= 7, "a climbing level holds");

    zemorph::GuardDetector spike;
    expect(!spike.observe(0.2f, 0.4f, true), "one killed sample does not hold yet");
    spike.clear();
    expect(!spike.held && spike.streak == 0, "clear returns to live");

    bool many = false;
    for (int i = 0; i < 8; ++i)
        many = spike.observe(0.1f, 0.1f, true) || many;
    expect(many && spike.held, "eight killed blocks hold");
}

static void testTime()
{
    const double sr = 44100.0;
    mpc::dsp::Biquad bq[zemorph::kSections];
    mpc::dsp::BiquadCoeffs c[zemorph::kSections];
    zemorph::designCascade(1, 3, 0.5f, 12.0f, 1.0f, sr, c);
    for (int s = 0; s < zemorph::kSections; ++s) {
        bq[s].setCoeffs(c[s]);
        bq[s].reset();
    }
    unsigned seed = 1;
    float peak = 0.0f;
    bool ok = true;
    for (int i = 0; i < 44100; ++i) {
        seed = seed * 1664525u + 1013904223u;
        float x = ((int)(seed >> 16) / 32768.0f) - 1.0f;
        x *= 0.25f;
        for (int s = 0; s < zemorph::kSections; ++s)
            x = bq[s].process(x);
        if (!std::isfinite(x)) ok = false;
        const float a = std::fabs(x);
        if (a > peak) peak = a;
    }
    expect(ok, "a noisy second stays finite");
    expect(peak < 20.0f, "a noisy second stays bounded");
    std::printf("time: peak %.2f on comb/nasal morph at +12 st\n", peak);
}

int main()
{
    testRadiusGrid();
    testEndpoints();
    testCharacter();
    testDetector();
    testTime();
    if (gFails) {
        std::fprintf(stderr, "%d failure(s)\n", gFails);
        return 1;
    }
    std::printf("safety_test: all checks passed\n");
    return 0;
}
