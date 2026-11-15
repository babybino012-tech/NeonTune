#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <vector>

class NeonTuneProcessor : public juce::AudioProcessor
{
public:
    NeonTuneProcessor();
    ~NeonTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NeonTune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor for the pitch meter (-1 = no pitch detected)
    std::atomic<float> detectedMidi { -1.0f };
    std::atomic<float> correctedMidi { -1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float detectPitch();                 // YIN, returns Hz or -1
    void  updateTarget (float hz);       // quantise to scale, set targetShift

    static constexpr int kRing  = 8192;  // detector ring buffer (power of 2)
    static constexpr int kDelay = 16384; // pitch-shifter delay line (power of 2)

    double sr = 44100.0;
    int    shiftWindow = 1400;           // crossfading-tap window in samples

    // detector
    std::vector<float> ring, frame, cmnd;
    int ringWrite = 0, hopCounter = 0, unvoicedHops = 0;

    // pitch shifter (two crossfading delay taps, shared phase)
    std::vector<float> delayBuf[2];
    int   delayWrite = 0;
    float phase = 0.0f;

    float currentShift = 0.0f;           // semitones, smoothed
    float targetShift  = 0.0f;           // semitones, from quantiser

    // parameter snapshot for this block
    int   keyV = 0, scaleV = 1;
    float amountV = 1.0f;

    float readTap (int ch, float delaySamples) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeonTuneProcessor)
};
