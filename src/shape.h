// Fixed frequency shapes for ZEMORPH.
//
// Each shape is four biquad sections (peaks and dips) baked as centre
// frequency, width and amount. The morph control blends section A into
// section B by interpolating those coefficients, then the result is
// pulled back inside a safe pole radius. Offset shifts every centre
// before that blend, so the whole current shape moves together.
// Depth scales the baked cuts and boosts toward flat.
//
// Nothing in this header allocates. It is safe to call from process().

#ifndef ZEMORPH_SHAPE_H
#define ZEMORPH_SHAPE_H

#include "dsp/biquad.h"
#include "dsp/math.h"

namespace zemorph {

constexpr int kSections = 4;
constexpr int kShapes = 8;

// Strictly inside the unit circle. A pole at 1 would ring forever.
constexpr float kPoleRadiusMax = 0.992f;
// Extra ceiling on top of the radius limit, so a section cannot get
// narrower than this even where the radius would still allow it.
constexpr float kResonanceCeiling = 6.0f;
// Loudest the four sections together may be, after the morph blend.
constexpr float kCascadeMax = 5.0f; // about +14 dB
constexpr float kGainMaxDb = 12.0f;

struct Section {
    float hz;
    float q;
    float db;
};

// Names are the on-screen frame list. Keep each one short.
inline const char* const kShapeNames[kShapes] = {
    "Two Peak",
    "Comb",
    "Formant",
    "Nasal",
    "Hollow",
    "Bright",
    "Dark",
    "Wide",
};

// Home frequencies, before Offset. Positive dB boosts, negative dB dips.
// Amounts sit on the gain cap and the notches are narrow. The pole-radius
// and cascade ceilings still pull anything unsafe back in.
inline constexpr Section kShape[kShapes][kSections] = {
    // Two Peak — a low lift, a hole, then two sharp peaks.
    { {160.0f, 0.8f, 12.0f}, {480.0f, 5.0f, -12.0f}, {1400.0f, 5.0f, 12.0f}, {3600.0f, 5.5f, 12.0f} },
    // Comb — alternating notches and peaks up the mids.
    { {400.0f, 6.0f, -12.0f}, {800.0f, 5.5f, 12.0f}, {1600.0f, 6.0f, -12.0f}, {3200.0f, 6.0f, 12.0f} },
    // Formant — three strong vowel peaks, the bottom pulled out.
    { {180.0f, 0.7f, -10.0f}, {700.0f, 4.5f, 12.0f}, {1600.0f, 5.0f, 12.0f}, {3200.0f, 5.0f, 11.0f} },
    // Nasal — lows and air gone, a hard mid spike.
    { {150.0f, 0.8f, -12.0f}, {1000.0f, 5.5f, 12.0f}, {1800.0f, 6.0f, 12.0f}, {5500.0f, 2.0f, -12.0f} },
    // Hollow — deep scoops, one thin peak left standing.
    { {220.0f, 2.0f, -12.0f}, {700.0f, 4.0f, -12.0f}, {1800.0f, 3.5f, 10.0f}, {5000.0f, 4.0f, -12.0f} },
    // Bright — the bottom is gone, presence and air are up.
    { {140.0f, 0.7f, -12.0f}, {600.0f, 2.5f, -12.0f}, {3500.0f, 4.0f, 12.0f}, {8000.0f, 3.0f, 12.0f} },
    // Dark — one heavy low lift. The rest is cut, kept apart so the
    // lifts do not stack past the cascade ceiling.
    { {120.0f, 0.7f, 12.0f}, {800.0f, 2.0f, -8.0f}, {2500.0f, 4.0f, -12.0f}, {6000.0f, 3.0f, -10.0f} },
    // Wide — two strong humps far apart, a hole between them.
    { {250.0f, 1.2f, 12.0f}, {900.0f, 3.5f, -12.0f}, {2600.0f, 2.2f, 12.0f}, {7000.0f, 1.6f, -8.0f} },
};

inline float poleRadius(const mpc::dsp::BiquadCoeffs& c)
{
    const float disc = c.a1 * c.a1 - 4.0f * c.a2;
    if (disc >= 0.0f && std::isfinite(disc)) {
        const float s = std::sqrt(disc);
        const float z1 = 0.5f * (-c.a1 + s);
        const float z2 = 0.5f * (-c.a1 - s);
        const float r1 = std::fabs(z1);
        const float r2 = std::fabs(z2);
        return r1 > r2 ? r1 : r2;
    }
    // Complex pair: both poles sit at radius sqrt(a2) when a2 is positive.
    return c.a2 > 0.0f ? std::sqrt(c.a2) : 0.0f;
}

// Scale both poles by the same factor so the radius lands on the limit.
// The angle (or the ratio of two real poles) stays put. Zeros are left
// alone, which only makes a boost milder when the pole had to move in.
inline void enforcePoleRadius(mpc::dsp::BiquadCoeffs& c, float rMax = kPoleRadiusMax)
{
    using mpc::dsp::BiquadCoeffs;
    if (!std::isfinite(c.b0) || !std::isfinite(c.b1) || !std::isfinite(c.b2)
        || !std::isfinite(c.a1) || !std::isfinite(c.a2)) {
        c = BiquadCoeffs::identity();
        return;
    }
    const float r = poleRadius(c);
    if (r > rMax && r > 1.0e-8f) {
        const float s = rMax / r;
        c.a1 *= s;
        c.a2 *= s * s;
    }
    const float r2 = rMax * rMax;
    if (c.a2 > r2) c.a2 = r2;
    if (c.a2 < -r2) c.a2 = -r2;
    float lim = rMax + c.a2 / rMax;
    const float limBox = 1.0f + c.a2;
    if (limBox < lim) lim = limBox;
    lim *= 0.999f;
    if (lim < 0.0f) lim = 0.0f;
    if (c.a1 > lim) c.a1 = lim;
    if (c.a1 < -lim) c.a1 = -lim;
}

// Q that keeps the peaking section inside kPoleRadiusMax at this
// frequency and boost. Cuts pull the pole inward, so only boosts tighten it.
inline float resonanceCeiling(float hz, float gainDb, double sampleRate)
{
    using mpc::dsp::kPi;
    const float r = kPoleRadiusMax;
    const float r2 = r * r;
    const float alphaLimit = (1.0f - r2) / (1.0f + r2);
    const float boost = gainDb > 0.0f ? gainDb : 0.0f;
    const float A = std::pow(10.0f, boost / 40.0f);
    const float w = 2.0f * kPi * hz / (float)sampleRate;
    const float s = std::sin(w);
    float qMax = s / (2.0f * alphaLimit * (A > 1.0f ? A : 1.0f));
    if (qMax > kResonanceCeiling) qMax = kResonanceCeiling;
    if (!(qMax > 0.4f)) qMax = 0.4f;
    return qMax;
}

inline mpc::dsp::BiquadCoeffs designSection(const Section& src, float offsetSemitones,
                                            float depth, double sampleRate)
{
    using mpc::dsp::BiquadCoeffs;
    using mpc::dsp::clamp;
    using mpc::dsp::semitonesToRatio;
    const float gain = clamp(src.db * depth, -kGainMaxDb, kGainMaxDb);
    if (std::fabs(gain) < 0.05f || !(sampleRate > 1.0))
        return BiquadCoeffs::identity();
    const float nyquist = 0.45f * (float)sampleRate;
    float hz = src.hz * semitonesToRatio(offsetSemitones);
    hz = clamp(hz, 35.0f, nyquist);
    float q = src.q;
    const float qMax = resonanceCeiling(hz, gain, sampleRate);
    if (q > qMax) q = qMax;
    if (q < 0.4f) q = 0.4f;
    BiquadCoeffs c = BiquadCoeffs::peak(sampleRate, hz, q, gain);
    enforcePoleRadius(c);
    return c;
}

inline mpc::dsp::BiquadCoeffs lerpCoeffs(const mpc::dsp::BiquadCoeffs& a,
                                         const mpc::dsp::BiquadCoeffs& b, float t)
{
    using mpc::dsp::BiquadCoeffs;
    using mpc::dsp::lerp;
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    BiquadCoeffs c;
    c.b0 = lerp(a.b0, b.b0, t);
    c.b1 = lerp(a.b1, b.b1, t);
    c.b2 = lerp(a.b2, b.b2, t);
    c.a1 = lerp(a.a1, b.a1, t);
    c.a2 = lerp(a.a2, b.a2, t);
    enforcePoleRadius(c);
    return c;
}

inline float sectionMagnitude(const mpc::dsp::BiquadCoeffs& c, float omega)
{
    const float cw = std::cos(omega);
    const float sw = std::sin(omega);
    const float c2 = cw * cw - sw * sw;
    const float s2 = 2.0f * sw * cw;
    const float realNum = c.b0 + c.b1 * cw + c.b2 * c2;
    const float imagNum = -c.b1 * sw - c.b2 * s2;
    const float realDen = 1.0f + c.a1 * cw + c.a2 * c2;
    const float imagDen = -c.a1 * sw - c.a2 * s2;
    const float den = realDen * realDen + imagDen * imagDen;
    if (!(den > 1.0e-20f) || !std::isfinite(den))
        return 1.0e6f;
    const float mag2 = (realNum * realNum + imagNum * imagNum) / den;
    return std::isfinite(mag2) && mag2 > 0.0f ? std::sqrt(mag2) : 0.0f;
}

// Pull every numerator down by the same amount if the cascade is louder
// than kCascadeMax anywhere we sample. Poles are not touched.
inline void capCascade(mpc::dsp::BiquadCoeffs* sections, double sampleRate)
{
    using mpc::dsp::kPi;
    if (!(sampleRate > 1.0)) return;
    float peak = 1.0f;
    const float nyquist = 0.45f * (float)sampleRate;
    auto consider = [&](float hz) {
        if (!(hz > 1.0f)) return;
        if (hz > nyquist) hz = nyquist;
        const float w = 2.0f * kPi * hz / (float)sampleRate;
        float mag = 1.0f;
        for (int s = 0; s < kSections; ++s)
            mag *= sectionMagnitude(sections[s], w);
        if (mag > peak) peak = mag;
    };
    for (int i = 0; i < 32; ++i)
        consider(35.0f * std::pow(nyquist / 35.0f, (float)i / 31.0f));
    // Also sit on each section's centre, where a narrow lift actually is.
    for (int s = 0; s < kSections; ++s) {
        const float r = poleRadius(sections[s]);
        if (r < 1.0e-4f) continue;
        const float cosW = mpc::dsp::clamp(-sections[s].a1 / (2.0f * r), -1.0f, 1.0f);
        consider(std::acos(cosW) * (float)sampleRate / (2.0f * kPi));
    }
    if (!(peak > kCascadeMax) || !std::isfinite(peak))
        return;
    const float scale = kCascadeMax / peak;
    for (int s = 0; s < kSections; ++s) {
        sections[s].b0 *= scale;
        sections[s].b1 *= scale;
        sections[s].b2 *= scale;
    }
}

// morph 0 = frame A, morph 1 = frame B. depth 0 = flat, depth 1 = full shape.
inline void designCascade(int frameA, int frameB, float morph, float offsetSemitones,
                          float depth, double sampleRate, mpc::dsp::BiquadCoeffs* out)
{
    using mpc::dsp::clamp;
    frameA = clamp(frameA, 0, kShapes - 1);
    frameB = clamp(frameB, 0, kShapes - 1);
    morph = clamp(morph, 0.0f, 1.0f);
    depth = clamp(depth, 0.0f, 1.0f);
    offsetSemitones = clamp(offsetSemitones, -24.0f, 24.0f);
    for (int s = 0; s < kSections; ++s) {
        const mpc::dsp::BiquadCoeffs a = designSection(kShape[frameA][s], offsetSemitones, depth, sampleRate);
        const mpc::dsp::BiquadCoeffs b = designSection(kShape[frameB][s], offsetSemitones, depth, sampleRate);
        out[s] = lerpCoeffs(a, b, morph);
    }
    capCascade(out, sampleRate);
    for (int s = 0; s < kSections; ++s)
        enforcePoleRadius(out[s]);
}

// Watches the filter, before the output knob and before the limiter.
// A stable shape holds a steady ratio of output to input. A howl keeps
// climbing. Eight rising blocks, or a block that already had to kill a
// sample, moves toward the hold. One loud stable block does not.
struct GuardDetector {
    float prevRatio = 1.0f;
    int streak = 0;
    bool held = false;

    void clear()
    {
        prevRatio = 1.0f;
        streak = 0;
        held = false;
    }

    // Returns true on the block the hold engages.
    bool observe(float inPeak, float wetPeak, bool sampleTripped)
    {
        if (held) return false;
        if (!std::isfinite(inPeak) || !std::isfinite(wetPeak) || sampleTripped) {
            ++streak;
        } else {
            const float floor = inPeak > 1.0e-3f ? inPeak : 1.0e-3f;
            const float ratio = wetPeak / floor;
            const bool climbing = ratio > 6.0f && ratio > prevRatio * 1.12f;
            if (climbing) ++streak;
            else if (streak > 0) --streak;
            prevRatio = std::isfinite(ratio) ? ratio : 1.0f;
        }
        if (streak >= 8) {
            held = true;
            return true;
        }
        return false;
    }
};

} // namespace zemorph

#endif
