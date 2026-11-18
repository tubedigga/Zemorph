// ZEMORPH — a morphing filter that blends two fixed frequency shapes.
//
// Stereo insert for MPC OS 3.10.1 and later. Two frames are picked from
// a fixed list. Morph blends frame A into frame B. Offset moves the whole
// blended shape. Depth sets how hard the cuts and boosts are. Output is
// the makeup gain.
//
// Safety stays on: poles are kept inside a fixed radius, resonance has a
// ceiling, a peak detector ducks the output, a ceiling limiter follows,
// and if the filter level keeps rising the filter steps out. Guard on
// the screen flips to Held. Turn it back to Live to try again.
//
// process() does not allocate, lock, log, or touch the disk. The
// framework flushes denormals around the call.

#include "mpc_plugin.h"
#include "shape.h"

#include "dsp/biquad.h"
#include "dsp/math.h"
#include "dsp/smoother.h"

class Zemorph : public mpc::Plugin
{
public:
    enum Params
    {
        kMorph = 0,   // parameter 0 is continuous; the host wiggles it on prepare
        kOffset,
        kDepth,
        kOutput,
        kFrameA,
        kFrameB,
        kGuard,
        kNumParams
    };

    static constexpr int kChannels = 2;
    // A single sample this far above unity is not a musical peak any more.
    // It is replaced with the dry sample and the section state is cleared.
    static constexpr float kSampleTrip = 12.0f;
    static constexpr float kDuckLevel = 1.0f;
    static constexpr float kCeiling = 0.98f;

    Zemorph() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam("morph", "Morph", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("offset", "Offset", -24.0f, 24.0f, 0.0f, "st", 1));
        addParam(floatParam("depth", "Depth", 0.0f, 100.0f, 75.0f, "%", 0));
        addParam(floatParam("output", "Output", -18.0f, 18.0f, 0.0f, "dB", 1));

        std::vector<std::string> frames;
        frames.reserve(zemorph::kShapes);
        for (int i = 0; i < zemorph::kShapes; ++i)
            frames.emplace_back(zemorph::kShapeNames[i]);
        addParam(choiceParam("framea", "Frame A", frames, 0));
        addParam(choiceParam("frameb", "Frame B", std::move(frames), zemorph::kShapes - 1));
        addParam(choiceParam("guard", "Guard", {"Live", "Held"}, 0));
        if (numParams() != kNumParams) mpc::log("ZEMORPH: parameter count mismatch");

        // Plain values follow the parameter order above.
        addProgram("Two Peak", {0.0f, 0.0f, 75.0f, 0.0f, 0.0f, 7.0f, 0.0f});
        addProgram("Mid Blend", {50.0f, 0.0f, 80.0f, 0.0f, 0.0f, 7.0f, 0.0f});
        addProgram("Comb", {0.0f, 0.0f, 70.0f, 0.0f, 1.0f, 4.0f, 0.0f});
        addProgram("Vowels", {35.0f, 2.0f, 75.0f, 0.0f, 2.0f, 3.0f, 0.0f});
        addProgram("Dark Low", {0.0f, -12.0f, 90.0f, 6.0f, 6.0f, 5.0f, 0.0f});
        addProgram("To Bright", {100.0f, 0.0f, 80.0f, 3.0f, 6.0f, 5.0f, 0.0f});
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
        outGain_.setTime(20.0f, sampleRate_);
        wetMix_.setTime(12.0f, sampleRate_);
        duckRelease_ = mpc::dsp::onePoleCoeff(100.0f, sampleRate_);
        limRelease_ = mpc::dsp::onePoleCoeff(40.0f, sampleRate_);
        reset();
    }

    void reset() override
    {
        for (int s = 0; s < zemorph::kSections; ++s)
            section_[s].reset();
        duck_ = 1.0f;
        lim_ = 1.0f;
        const bool held = getInt(kGuard) == 1;
        if (held) guard_.held = true;
        else guard_.clear();
        designBlock(0.0f);
        for (int s = 0; s < zemorph::kSections; ++s)
            current_[s] = target_[s];
        outGain_.snap(mpc::dsp::dbToGain(get(kOutput)));
        wetMix_.snap(held ? 0.0f : 1.0f);
        bypassed_ = held;
    }

    void parameterChanged(int index, float plainValue) override
    {
        // Live is the clear. Held is only a flag, whether the user set it
        // or the detector did. Nothing else here has a side effect, and
        // parameter 0 (Morph) in particular does not.
        if (index == kGuard && plainValue < 0.5f)
            clearReq_.store(1, std::memory_order_release);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        if (n <= 0 || ctx.outputs == nullptr || ctx.outputs[0] == nullptr)
            return;

        const bool cleared = clearReq_.exchange(0, std::memory_order_acq_rel) != 0;
        if (cleared) {
            guard_.clear();
            resetSections();
        }

        const bool userHeld = getInt(kGuard) == 1;
        bool bypass = userHeld || guard_.held;
        if (bypass != bypassed_) {
            resetSections();
            bypassed_ = bypass;
        }
        wetMix_.setTarget(bypass ? 0.0f : 1.0f);
        outGain_.setTarget(mpc::dsp::dbToGain(get(kOutput)));

        if (!bypass)
            designBlock(static_cast<float>(n));

        const float* inL = (ctx.inputs != nullptr && ctx.numInputs > 0) ? ctx.inputs[0] : nullptr;
        const float* inR = (ctx.inputs != nullptr && ctx.numInputs > 1) ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        float blockIn = 0.0f;
        float blockWet = 0.0f;
        bool sampleTripped = false;

        mpc::dsp::BiquadCoeffs step[zemorph::kSections];
        if (!bypass) {
            const float inv = 1.0f / static_cast<float>(n);
            for (int s = 0; s < zemorph::kSections; ++s) {
                step[s].b0 = (target_[s].b0 - current_[s].b0) * inv;
                step[s].b1 = (target_[s].b1 - current_[s].b1) * inv;
                step[s].b2 = (target_[s].b2 - current_[s].b2) * inv;
                step[s].a1 = (target_[s].a1 - current_[s].a1) * inv;
                step[s].a2 = (target_[s].a2 - current_[s].a2) * inv;
            }
        }

        for (int i = 0; i < n; ++i) {
            // Read this frame before writing it. The host processes in place.
            const float dryL = inL != nullptr ? mpc::dsp::sanitize(inL[i]) : 0.0f;
            const float dryR = inR != nullptr ? mpc::dsp::sanitize(inR[i]) : dryL;
            const float inAbs = std::fabs(dryL) > std::fabs(dryR) ? std::fabs(dryL) : std::fabs(dryR);
            if (inAbs > blockIn) blockIn = inAbs;

            float wetL = dryL;
            float wetR = dryR;
            if (!bypass) {
                for (int s = 0; s < zemorph::kSections; ++s) {
                    current_[s].b0 += step[s].b0;
                    current_[s].b1 += step[s].b1;
                    current_[s].b2 += step[s].b2;
                    current_[s].a1 += step[s].a1;
                    current_[s].a2 += step[s].a2;
                    section_[s].setCoeffs(current_[s]);
                    section_[s].process(wetL, wetR);
                }
                const float wetAbs = std::fabs(wetL) > std::fabs(wetR) ? std::fabs(wetL) : std::fabs(wetR);
                if (wetAbs > blockWet) blockWet = wetAbs;
                if (!std::isfinite(wetL) || !std::isfinite(wetR) || wetAbs > kSampleTrip) {
                    sampleTripped = true;
                    wetL = dryL;
                    wetR = dryR;
                    resetSections();
                }
            }

            const float mix = wetMix_.process();
            const float gain = outGain_.process();
            float yL = (dryL + (wetL - dryL) * mix) * gain;
            float yR = (dryR + (wetR - dryR) * mix) * gain;

            const float outAbs = std::fabs(yL) > std::fabs(yR) ? std::fabs(yL) : std::fabs(yR);
            float duckTarget = 1.0f;
            if (outAbs > kDuckLevel && outAbs > 1.0e-8f)
                duckTarget = kDuckLevel / outAbs;
            if (duckTarget < duck_) duck_ = duckTarget;
            else duck_ += duckRelease_ * (duckTarget - duck_);
            if (duck_ > 1.0f) duck_ = 1.0f;
            if (duck_ < 0.02f) duck_ = 0.02f;
            yL *= duck_;
            yR *= duck_;

            const float limAbs = std::fabs(yL) > std::fabs(yR) ? std::fabs(yL) : std::fabs(yR);
            if (limAbs > kCeiling && limAbs > 1.0e-8f) {
                const float need = kCeiling / limAbs;
                if (need < lim_) lim_ = need;
            } else if (lim_ < 1.0f) {
                lim_ += limRelease_ * (1.0f - lim_);
            }
            if (lim_ > 1.0f) lim_ = 1.0f;
            if (lim_ < 0.001f) lim_ = 0.001f;
            yL *= lim_;
            yR *= lim_;

            outL[i] = mpc::dsp::flushDenormal(mpc::dsp::sanitize(yL));
            if (outR != nullptr)
                outR[i] = mpc::dsp::flushDenormal(mpc::dsp::sanitize(yR));
        }

        if (!bypass) {
            for (int s = 0; s < zemorph::kSections; ++s)
                current_[s] = target_[s];
        }

        // A clear on this block is honoured for the whole block. The next
        // block can hold again if the level is still climbing.
        if (!cleared && !bypass && guard_.observe(blockIn, blockWet, sampleTripped)) {
            bypassed_ = true;
            resetSections();
            wetMix_.setTarget(0.0f);
            if (getInt(kGuard) != 1)
                set(kGuard, 1.0f);
        }
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "ZEMORPH";
        info.vendor = "Tubedigga";
        info.uniqueId = MPC_FOURCC('T', 'T', '1', 'e');
        info.version = 1100;
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 0;
        return info;
    }

    void resetSections()
    {
        for (int s = 0; s < zemorph::kSections; ++s)
            section_[s].reset();
    }

    // One coefficient design per block, from the current knobs.
    // The sample loop then walks current_ toward target_ in equal steps.
    void designBlock(float /*frames*/)
    {
        const float morph = get(kMorph) * 0.01f;
        const float offset = get(kOffset);
        const float depth = get(kDepth) * 0.01f;
        zemorph::designCascade(getInt(kFrameA), getInt(kFrameB), morph, offset, depth,
                               sampleRate_, target_);
    }

    mpc::dsp::StereoBiquad section_[zemorph::kSections];
    mpc::dsp::BiquadCoeffs current_[zemorph::kSections];
    mpc::dsp::BiquadCoeffs target_[zemorph::kSections];
    mpc::dsp::Smoother outGain_;
    mpc::dsp::Smoother wetMix_;
    zemorph::GuardDetector guard_;
    std::atomic<int> clearReq_{0};
    double sampleRate_ = 44100.0;
    float duck_ = 1.0f;
    float lim_ = 1.0f;
    float duckRelease_ = 1.0f;
    float limRelease_ = 1.0f;
    bool bypassed_ = false;
};

MPC_PLUGIN_ENTRY(Zemorph)
