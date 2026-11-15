#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr int kWindow = 1024;   // YIN integration window
    constexpr int kHop    = 512;    // pitch is re-estimated every kHop samples

    // C-relative scale masks: Chromatic, Major, Minor, Minor Pent, Major Pent
    const bool kScales[5][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 },
        { 1,0,1,0,1,1,0,1,0,1,0,1 },
        { 1,0,1,1,0,1,0,1,1,0,1,0 },
        { 1,0,0,1,0,1,0,1,0,0,1,0 },
        { 1,0,1,0,1,0,0,1,0,1,0,0 }
    };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout NeonTuneProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
        StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
        StringArray { "Chromatic","Major","Minor","Minor Pent.","Major Pent." }, 1));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "retune", 1 }, "Retune Speed",
        NormalisableRange<float> (0.0f, 500.0f, 0.1f, 0.4f), 20.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    return { p.begin(), p.end() };
}

NeonTuneProcessor::NeonTuneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool NeonTuneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

//==============================================================================
void NeonTuneProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    shiftWindow = juce::jmax (256, (int) (0.032 * sr));   // ~32 ms

    ring.assign ((size_t) kRing, 0.0f);
    frame.assign ((size_t) (kWindow + 1100), 0.0f);
    cmnd.assign (1100, 0.0f);
    delayBuf[0].assign ((size_t) kDelay, 0.0f);
    delayBuf[1].assign ((size_t) kDelay, 0.0f);

    ringWrite = hopCounter = unvoicedHops = delayWrite = 0;
    phase = currentShift = targetShift = 0.0f;

    setLatencySamples (shiftWindow / 2 + 2);
}

//==============================================================================
// YIN pitch detector (de Cheveigne & Kawahara, 2002)
float NeonTuneProcessor::detectPitch()
{
    const int tauMin = juce::jmax (2, (int) (sr / 1000.0));        // 1000 Hz ceiling
    const int tauMax = juce::jmin ((int) (sr / 70.0), 1024);       // ~70 Hz floor
    const int total  = kWindow + tauMax;

    for (int i = 0; i < total; ++i)
        frame[(size_t) i] = ring[(size_t) ((ringWrite - total + i + kRing) & (kRing - 1))];

    // silence gate
    float energy = 0.0f;
    for (int i = tauMax; i < total; ++i)
        energy += frame[(size_t) i] * frame[(size_t) i];
    if (std::sqrt (energy / (float) kWindow) < 0.008f)
        return -1.0f;

    // difference function + cumulative mean normalisation
    cmnd[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float d = 0.0f;
        for (int j = 0; j < kWindow; ++j)
        {
            const float diff = frame[(size_t) j] - frame[(size_t) (j + tau)];
            d += diff * diff;
        }
        running += d;
        cmnd[(size_t) tau] = d * (float) tau / juce::jmax (running, 1.0e-9f);
    }

    // absolute threshold
    int est = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                ++tau;
            est = tau;
            break;
        }
    }
    if (est < 0)
        return -1.0f;

    // parabolic interpolation
    float better = (float) est;
    const float s0 = cmnd[(size_t) (est - 1)], s1 = cmnd[(size_t) est], s2 = cmnd[(size_t) (est + 1)];
    const float denom = s0 + s2 - 2.0f * s1;
    if (std::abs (denom) > 1.0e-9f)
        better += 0.5f * (s0 - s2) / denom;

    return (float) sr / better;
}

//==============================================================================
void NeonTuneProcessor::updateTarget (float hz)
{
    if (hz < 0.0f)
    {
        if (++unvoicedHops > 3)   // hold the correction briefly through short gaps
        {
            targetShift = 0.0f;
            detectedMidi.store (-1.0f);
            correctedMidi.store (-1.0f);
        }
        return;
    }
    unvoicedHops = 0;

    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int   r    = (int) std::round (midi);

    int   bestNote = r;
    float bestDist = 1.0e9f;
    for (int n = r - 7; n <= r + 7; ++n)
    {
        const int pc = (((n - keyV) % 12) + 12) % 12;
        if (! kScales[scaleV][pc])
            continue;
        const float dist = std::abs ((float) n - midi);
        if (dist < bestDist) { bestDist = dist; bestNote = n; }
    }

    targetShift = ((float) bestNote - midi) * amountV;
    detectedMidi.store (midi);
    correctedMidi.store ((float) bestNote);
}

//==============================================================================
float NeonTuneProcessor::readTap (int ch, float delaySamples) const
{
    float rp = (float) delayWrite - delaySamples;
    while (rp < 0.0f) rp += (float) kDelay;
    const int   i0 = (int) rp;
    const float f  = rp - (float) i0;
    const int   i1 = (i0 + 1) & (kDelay - 1);
    const auto& b  = delayBuf[ch];
    return b[(size_t) (i0 & (kDelay - 1))] * (1.0f - f) + b[(size_t) i1] * f;
}

void NeonTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    keyV    = (int) apvts.getRawParameterValue ("key")->load();
    scaleV  = juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("scale")->load());
    amountV = apvts.getRawParameterValue ("amount")->load();
    const float retuneMs = apvts.getRawParameterValue ("retune")->load();
    const float mix      = apvts.getRawParameterValue ("mix")->load();

    const float smoothCoef = retuneMs <= 1.0f ? 0.0f
                           : std::exp (-1.0f / (0.001f * retuneMs * (float) sr));
    const float W = (float) shiftWindow;
    constexpr float pi = juce::MathConstants<float>::pi;

    for (int i = 0; i < numSamples; ++i)
    {
        // detector input: mono sum
        float mono = 0.0f;
        for (int c = 0; c < numCh; ++c)
            mono += buffer.getReadPointer (c)[i];
        mono /= (float) juce::jmax (1, numCh);

        ring[(size_t) ringWrite] = mono;
        ringWrite = (ringWrite + 1) & (kRing - 1);

        if (++hopCounter >= kHop)
        {
            hopCounter = 0;
            updateTarget (detectPitch());
        }

        currentShift = smoothCoef * currentShift + (1.0f - smoothCoef) * targetShift;
        const float ratio = std::exp2 (currentShift / 12.0f);

        // crossfade gains for the two taps (sin^2 + cos^2 = 1)
        float ph2 = phase + 0.5f;
        if (ph2 >= 1.0f) ph2 -= 1.0f;
        const float s1 = std::sin (pi * phase), s2 = std::sin (pi * ph2);
        const float g1 = s1 * s1, g2 = s2 * s2;
        const float d1 = phase * W + 2.0f, d2 = ph2 * W + 2.0f;

        // write inputs first, then read
        for (int c = 0; c < numCh; ++c)
            delayBuf[c][(size_t) delayWrite] = buffer.getReadPointer (c)[i];
        delayWrite = (delayWrite + 1) & (kDelay - 1);

        for (int c = 0; c < numCh; ++c)
        {
            const float wet = readTap (c, d1) * g1 + readTap (c, d2) * g2;
            const float dry = readTap (c, W * 0.5f + 2.0f);   // latency-matched dry
            buffer.getWritePointer (c)[i] = dry * (1.0f - mix) + wet * mix;
        }

        phase += (1.0f - ratio) / W;
        phase -= std::floor (phase);
    }
}

//==============================================================================
juce::AudioProcessorEditor* NeonTuneProcessor::createEditor()
{
    return new NeonTuneEditor (*this);
}

void NeonTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NeonTuneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeonTuneProcessor();
}
