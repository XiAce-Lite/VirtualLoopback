#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VirtualLoopbackAudioProcessor::VirtualLoopbackAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
 #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
 #endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
                     )
#endif
{
    addParameter (volumeParam = new juce::AudioParameterFloat (
        { "volume", 1 }, "Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f));

    addParameter (muteParam = new juce::AudioParameterBool (
        { "mute", 1 }, "Mute", false));

    addParameter (captureEnabledParam = new juce::AudioParameterBool (
        { "capture", 1 }, "Capture", true));

    refreshDeviceList();

    for (const auto& d : devices)
    {
        if (d.isDefault)
        {
            selectedDeviceId = d.id;
            break;
        }
    }

    if (selectedDeviceId.isEmpty() && ! devices.isEmpty())
        selectedDeviceId = devices.getReference (0).id;
}

VirtualLoopbackAudioProcessor::~VirtualLoopbackAudioProcessor()
{
#if JUCE_WINDOWS
    capture.stop();
#endif
}

//==============================================================================
const juce::String VirtualLoopbackAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool VirtualLoopbackAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool VirtualLoopbackAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool VirtualLoopbackAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double VirtualLoopbackAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int VirtualLoopbackAudioProcessor::getNumPrograms() { return 1; }
int VirtualLoopbackAudioProcessor::getCurrentProgram() { return 0; }
void VirtualLoopbackAudioProcessor::setCurrentProgram (int) {}
const juce::String VirtualLoopbackAudioProcessor::getProgramName (int) { return {}; }
void VirtualLoopbackAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void VirtualLoopbackAudioProcessor::resetResamplerState()
{
    pendingFrames = 0;
    readPos = 0.0;
    outputPrimed = false;
    adaptiveRatioScale = 1.0;
    lastSample[0] = lastSample[1] = 0.0f;
    if (pendingCapture.getNumSamples() > 0)
        pendingCapture.clear();
}

void VirtualLoopbackAudioProcessor::pullIntoPending()
{
#if JUCE_WINDOWS
    // Leave headroom in pending buffer
    const int room = pendingCapture.getNumSamples() - pendingFrames;
    if (room <= 16)
        return;

    const int toPull = juce::jmin (room, juce::jmin (pullScratch.getNumSamples(), capture.getNumReady()));
    if (toPull <= 0)
        return;

    pullScratch.clear();
    const int got = capture.read (pullScratch, toPull);
    if (got <= 0)
        return;

    for (int ch = 0; ch < pendingCapture.getNumChannels(); ++ch)
    {
        const int srcCh = juce::jmin (ch, pullScratch.getNumChannels() - 1);
        pendingCapture.copyFrom (ch, pendingFrames, pullScratch, srcCh, 0, got);
    }
    pendingFrames += got;
#endif
}

void VirtualLoopbackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    hostBlockSize = samplesPerBlock;

    // Large pending buffer so adaptive SRC has room
    pendingCapture.setSize (2, samplesPerBlock * 32 + 8192, false, false, true);
    pullScratch.setSize (2, samplesPerBlock * 8 + 2048, false, false, true);
    resetResamplerState();

    if (captureEnabledParam != nullptr && captureEnabledParam->get())
        restartCapture();
}

void VirtualLoopbackAudioProcessor::releaseResources()
{
#if JUCE_WINDOWS
    capture.stop();
#endif
    resetResamplerState();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VirtualLoopbackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void VirtualLoopbackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const bool muted = muteParam != nullptr && muteParam->get();
    const bool enabled = captureEnabledParam != nullptr && captureEnabledParam->get();
    const float volume = volumeParam != nullptr ? volumeParam->get() : 0.8f;

#if ! JUCE_WINDOWS
    juce::ignoreUnused (muted, enabled, volume);
    uiPeak = 0.0f;
    return;
#else

    if (! enabled || muted || ! capture.isRunning())
    {
        uiPeak = uiPeak.load() * 0.9f;
        return;
    }

    const double captureRate = capture.getCaptureSampleRate();
    if (captureRate <= 0.0 || hostSampleRate <= 0.0)
        return;

    pullIntoPending();

    const int numOutCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Total available capture-domain frames (FIFO + pending)
    const double pendingAvail = juce::jmax (0.0, (double) pendingFrames - readPos);
    const double totalAvail = pendingAvail + (double) capture.getNumReady();

    // Target ~80ms of buffered audio to absorb WASAPI↔ASIO clock drift
    const double targetFrames = captureRate * 0.080;
    const double primeFrames = captureRate * 0.050;

    if (! outputPrimed)
    {
        if (totalAvail < primeFrames)
        {
            uiPeak = uiPeak.load() * 0.9f;
            return;
        }
        outputPrimed = true;
    }

    // Soft PLL: speed up or slow down consume rate based on buffer fill
    const double fillError = (totalAvail - targetFrames) / targetFrames;
    const double corr = juce::jlimit (-0.025, 0.025, fillError * 0.04);
    adaptiveRatioScale = juce::jlimit (0.97, 1.03, 0.995 * adaptiveRatioScale + 0.005 * (1.0 + corr));

    const double ratio = (captureRate / hostSampleRate) * adaptiveRatioScale;

    // Pull more if we're going to need it this block
    const double framesNeeded = readPos + (double) numSamples * ratio + 4.0;
    while ((double) pendingFrames < framesNeeded)
    {
        const int before = pendingFrames;
        pullIntoPending();
        if (pendingFrames == before)
            break;
    }

    for (int outCh = 0; outCh < numOutCh; ++outCh)
    {
        const int srcCh = juce::jmin (outCh, pendingCapture.getNumChannels() - 1);
        float* dest = buffer.getWritePointer (outCh);
        const float* src = pendingCapture.getReadPointer (srcCh);
        float hold = lastSample[juce::jmin (outCh, 1)];

        double pos = readPos;
        for (int i = 0; i < numSamples; ++i)
        {
            const int i0 = (int) pos;
            if (i0 + 1 < pendingFrames)
            {
                const float frac = (float) (pos - (double) i0);
                const float s0 = src[i0];
                const float s1 = src[i0 + 1];
                hold = s0 + frac * (s1 - s0);
                dest[i] = hold * volume;
                pos += ratio;
            }
            else if (i0 < pendingFrames)
            {
                // Last available sample — hold rather than hard zero
                hold = src[i0];
                dest[i] = hold * volume;
                pos += ratio;
            }
            else
            {
                // True underrun: hold last sample (avoids clicky silence gaps)
                dest[i] = hold * volume;
                // Do not advance pos into empty territory excessively
            }
        }

        lastSample[juce::jmin (outCh, 1)] = hold;

        if (outCh == numOutCh - 1)
            readPos = pos;
    }

    // If we badly underran, re-prime on next fill
    if ((double) pendingFrames < readPos + 1.0 && capture.getNumReady() < (int) (captureRate * 0.01))
        outputPrimed = false;

    // Drop consumed whole frames from pendingCapture
    const int drop = (int) readPos;
    if (drop > 0)
    {
        const int safeDrop = juce::jmin (drop, pendingFrames);
        const int remain = pendingFrames - safeDrop;
        if (remain > 0)
        {
            for (int ch = 0; ch < pendingCapture.getNumChannels(); ++ch)
            {
                auto* data = pendingCapture.getWritePointer (ch);
                std::memmove (data, data + safeDrop, (size_t) remain * sizeof (float));
            }
        }
        pendingFrames = remain;
        readPos = juce::jmax (0.0, readPos - (double) safeDrop);
    }

    float peak = 0.0f;
    for (int ch = 0; ch < numOutCh; ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
    uiPeak = peak;

#endif
}

//==============================================================================
bool VirtualLoopbackAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* VirtualLoopbackAudioProcessor::createEditor()
{
    return new VirtualLoopbackAudioProcessorEditor (*this);
}

//==============================================================================
void VirtualLoopbackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("VirtualLoopback");
    state.setProperty ("deviceId", selectedDeviceId, nullptr);
    state.setProperty ("volume", volumeParam != nullptr ? volumeParam->get() : 0.8f, nullptr);
    state.setProperty ("mute", muteParam != nullptr ? muteParam->get() : false, nullptr);
    state.setProperty ("capture", captureEnabledParam != nullptr ? captureEnabledParam->get() : true, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void VirtualLoopbackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid())
        return;

    selectedDeviceId = state.getProperty ("deviceId", selectedDeviceId).toString();

    if (volumeParam != nullptr)
        *volumeParam = (float) state.getProperty ("volume", 0.8f);
    if (muteParam != nullptr)
        *muteParam = (bool) state.getProperty ("mute", false);
    if (captureEnabledParam != nullptr)
        *captureEnabledParam = (bool) state.getProperty ("capture", true);

    refreshDeviceList();
    if (captureEnabledParam != nullptr && captureEnabledParam->get())
        restartCapture();
}

//==============================================================================
juce::StringArray VirtualLoopbackAudioProcessor::getRenderDeviceNames() const
{
    juce::StringArray names;
    for (const auto& d : devices)
        names.add (d.isDefault ? (d.name + juce::String (L" （既定）")) : d.name);
    return names;
}

juce::String VirtualLoopbackAudioProcessor::getSelectedDeviceName() const
{
    for (const auto& d : devices)
        if (d.id == selectedDeviceId)
            return d.name;
    return selectedDeviceId.isEmpty() ? juce::String (L"（既定）") : selectedDeviceId;
}

int VirtualLoopbackAudioProcessor::getSelectedDeviceIndex() const
{
    for (int i = 0; i < devices.size(); ++i)
        if (devices.getReference (i).id == selectedDeviceId)
            return i;
    return devices.isEmpty() ? -1 : 0;
}

void VirtualLoopbackAudioProcessor::setSelectedDeviceByIndex (int index)
{
    if (! juce::isPositiveAndBelow (index, devices.size()))
        return;
    setSelectedDeviceId (devices.getReference (index).id);
}

void VirtualLoopbackAudioProcessor::setSelectedDeviceId (const juce::String& deviceId)
{
    selectedDeviceId = deviceId;
    if (captureEnabledParam != nullptr && captureEnabledParam->get())
        restartCapture();
}

void VirtualLoopbackAudioProcessor::refreshDeviceList()
{
#if JUCE_WINDOWS
    devices = WasapiLoopbackCapture::getRenderDevices();
#else
    devices.clear();
#endif
}

bool VirtualLoopbackAudioProcessor::restartCapture()
{
#if JUCE_WINDOWS
    resetResamplerState();

    if (captureEnabledParam == nullptr || ! captureEnabledParam->get())
    {
        capture.stop();
        return false;
    }

    return capture.start (selectedDeviceId);
#else
    return false;
#endif
}

bool VirtualLoopbackAudioProcessor::isCaptureRunning() const
{
#if JUCE_WINDOWS
    return capture.isRunning();
#else
    return false;
#endif
}

juce::String VirtualLoopbackAudioProcessor::getCaptureStatusText() const
{
#if JUCE_WINDOWS
    if (captureEnabledParam != nullptr && ! captureEnabledParam->get())
        return juce::String (L"キャプチャ停止中");

    if (capture.isRunning())
    {
        return juce::String (L"キャプチャ中  ")
             + juce::String (capture.getCaptureSampleRate(), 0) + " Hz / "
             + juce::String (capture.getCaptureNumChannels()) + " ch";
    }

    const auto err = capture.getLastError();
    if (err.isNotEmpty())
        return juce::String (L"エラー: ") + err;

    return juce::String (L"キャプチャしていません");
#else
    return juce::String (L"Windows 専用です");
#endif
}

float VirtualLoopbackAudioProcessor::getInputPeak() const
{
    return uiPeak.load();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VirtualLoopbackAudioProcessor();
}
