// Minty — a creative compressor for the Polyend Endless.
// Endless-facing glue only: knobs, footswitch, LED. All DSP lives in
// source/minty/ and is host-independent.
//
//   Left knob   SENSITIVITY  threshold (and drive in Dusty)
//   Mid knob    DYNAMICS     compress → limit (noon) → sag
//   Right knob  EQ           dark ◄ flat (noon) ► thin, opens up when you dig in
//   Press       Swell on/off
//   Hold        Character: Stable → Wobble → Twitchy → Dusty

#include "Patch.h"
#include "minty/MintyEngine.h"

class PatchImpl : public Patch
{
  public:
    void init() override
    {
        engine.reset();
        samplesSincePress = kHoldUndoWindow;
        pressToggledSwell = false;
    }

    void setWorkingBuffer(std::span<float, kWorkingBufferSize> /* buffer */) override
    {
        // Minty keeps all its state in members; the external buffer isn't needed.
    }

    void processAudio(std::span<float> audioBufferLeft, std::span<float> audioBufferRight) override
    {
        const size_t frames = audioBufferLeft.size() < audioBufferRight.size()
                                ? audioBufferLeft.size()
                                : audioBufferRight.size();
        engine.process(audioBufferLeft.data(), audioBufferRight.data(), static_cast<int>(frames));

        if (samplesSincePress < kHoldUndoWindow)
        {
            samplesSincePress += static_cast<uint32_t>(frames);
        }
    }

    ParameterMetadata getParameterMetadata(int paramIdx) override
    {
        if (paramIdx < 0 || paramIdx >= minty::MintyEngine::kKnobCount)
        {
            return ParameterMetadata{ 0.0f, 1.0f, 0.5f };
        }
        return ParameterMetadata{ 0.0f, 1.0f, minty::MintyEngine::kKnobDefaults[paramIdx] };
    }

    void setParamValue(int idx, float value) override { engine.setKnob(idx, value); }

    void handleAction(int actionIdx) override
    {
        switch (static_cast<endless::ActionId>(actionIdx))
        {
            case endless::ActionId::kLeftFootSwitchPress:
                engine.setSwell(!engine.getSwell());
                samplesSincePress = 0;
                pressToggledSwell = true;
                break;

            case endless::ActionId::kLeftFootSwitchHold:
                // The SDK doesn't say whether a hold is preceded by a press
                // event. If a press landed just before this hold, assume it was
                // the start of the same gesture and undo its swell toggle.
                if (pressToggledSwell && samplesSincePress < kHoldUndoWindow)
                {
                    engine.setSwell(!engine.getSwell());
                }
                pressToggledSwell = false;
                engine.cycleCharacter();
                break;

            default:
                break;
        }
    }

    Color getStateLedColor() override
    {
        // Colour = mode, brightness = compressing (> 3 dB of gain reduction).
        const bool working = engine.meterDb() > 3.0f;

        if (engine.getSwell())
        {
            return working ? Color::kBlue : Color::kDarkCobalt;
        }
        switch (engine.getCharacter())
        {
            case minty::Character::kWobble:
                return working ? Color::kLightBlueColor : Color::kDimCyan;
            case minty::Character::kTwitchy:
                return working ? Color::kLightYellow : Color::kDimYellow;
            case minty::Character::kDusty:
                return working ? Color::kBeige : Color::kDimWhite;
            case minty::Character::kStable:
            case minty::Character::kCount:
            default:
                return working ? Color::kPastelGreen : Color::kDimGreen; // mint
        }
    }

  private:
    // 1.2 s at 48 kHz: generous enough for any plausible hold-detect time.
    static constexpr uint32_t kHoldUndoWindow = static_cast<uint32_t>(1.2f * kSampleRate);

    minty::MintyEngine engine;
    uint32_t samplesSincePress = kHoldUndoWindow;
    bool pressToggledSwell = false;
};

static PatchImpl patch;

Patch* Patch::getInstance()
{
    return &patch;
}
