/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MBCompAudioProcessor::MBCompAudioProcessor()
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
    using namespace Params;
    const auto& params = GetParams();
    
    auto floatHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    
    floatHelper(lowBandComp.attack, Names::Attack_Low_Band);
    floatHelper(lowBandComp.release, Names::Release_Low_Band);
    floatHelper(lowBandComp.threshold, Names::Threshold_Low_Band);
    
    floatHelper(midBandComp.attack, Names::Attack_Mid_Band);
    floatHelper(midBandComp.release, Names::Release_Mid_Band);
    floatHelper(midBandComp.threshold, Names::Threshold_Mid_Band);
    
    floatHelper(highBandComp.attack, Names::Attack_High_Band);
    floatHelper(highBandComp.release, Names::Release_High_Band);
    floatHelper(highBandComp.threshold, Names::Threshold_High_Band);
    
    auto choiceHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    
    choiceHelper(lowBandComp.ratio, Names::Ratio_Low_Band);
    choiceHelper(midBandComp.ratio, Names::Ratio_Mid_Band);
    choiceHelper(highBandComp.ratio, Names::Ratio_High_Band);
    
    auto boolHelper = [&apvts = this->apvts, &params](auto& param, const auto& paramName)
    {
        param = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(params.at(paramName)));
        jassert(param != nullptr);
    };
    
    boolHelper(lowBandComp.bypassed, Names::Bypassed_Low_Band);
    boolHelper(midBandComp.bypassed, Names::Bypassed_Mid_Band);
    boolHelper(highBandComp.bypassed, Names::Bypassed_High_Band);
    
    floatHelper(lowMidCrossover, Names::Low_Mid_Crossover_Freq);
    floatHelper(midHighCrossover, Names::Mid_High_Crossover_Freq);
    
    LP1.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    HP1.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    AP2.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
    LP2.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    HP2.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
}

MBCompAudioProcessor::~MBCompAudioProcessor()
{
}

//==============================================================================
const juce::String MBCompAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MBCompAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool MBCompAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool MBCompAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double MBCompAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MBCompAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int MBCompAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MBCompAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String MBCompAudioProcessor::getProgramName (int index)
{
    return {};
}

void MBCompAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void MBCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    spec.sampleRate = sampleRate;
    
    for (auto& comp : compressors)
        comp.prepare(spec);
    
    LP1.prepare(spec);
    HP1.prepare(spec);
    
    AP2.prepare(spec);
    
    LP2.prepare(spec);
    HP2.prepare(spec);
    
    for (auto& buffer : filterBuffers)
    {
        buffer.setSize(spec.numChannels, samplesPerBlock);
    }
}

void MBCompAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MBCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void MBCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    for (auto& compressor : compressors)
        compressor.updateCompressorSettings();
    
    for (auto& fb : filterBuffers)
    {
        fb = buffer;
    }
    
    auto lowMidCutoff = lowMidCrossover->get();
    LP1.setCutoffFrequency(lowMidCutoff);
    HP1.setCutoffFrequency(lowMidCutoff);
    
    auto midHighCutoff = midHighCrossover->get();
    AP2.setCutoffFrequency(midHighCutoff);
    LP2.setCutoffFrequency(midHighCutoff);
    HP2.setCutoffFrequency(midHighCutoff);
    
    auto fb0Block = juce::dsp::AudioBlock<float>(filterBuffers[0]);
    auto fb1Block = juce::dsp::AudioBlock<float>(filterBuffers[1]);
    auto fb2Block = juce::dsp::AudioBlock<float>(filterBuffers[2]);

    auto fb0Ctx = juce::dsp::ProcessContextReplacing<float>(fb0Block);
    auto fb1Ctx = juce::dsp::ProcessContextReplacing<float>(fb1Block);
    auto fb2Ctx = juce::dsp::ProcessContextReplacing<float>(fb2Block);
    
    LP1.process(fb0Ctx);
    AP2.process(fb0Ctx);
    
    HP1.process(fb1Ctx);
    filterBuffers[2] = filterBuffers[1];
    LP2.process(fb1Ctx);
    
    HP2.process(fb2Ctx);

    for (size_t i = 0; i < filterBuffers.size(); ++i)
    {
        compressors[i].process(filterBuffers[i]);
    }
    
    auto numSamples = buffer.getNumSamples();
    auto numChannels = buffer.getNumChannels();
    
    buffer.clear();
    
    auto addFilterBand = [nc = numChannels, ns = numSamples](auto& inputBuffer, const auto& source)
    {
        for (auto i = 0; i < nc; ++i)
        {
            inputBuffer.addFrom(i, 0, source, i, 0, ns);
        }
    };
    
    addFilterBand(buffer, filterBuffers[0]);
    addFilterBand(buffer, filterBuffers[1]);
    addFilterBand(buffer, filterBuffers[2]);
}

//==============================================================================
bool MBCompAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* MBCompAudioProcessor::createEditor()
{
    //return new MBCompAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void MBCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void MBCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState(tree);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MBCompAudioProcessor::createParameterLayout()
{
    APVTS::ParameterLayout layout;
    using namespace Params;
    const auto& params = GetParams();
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Threshold_Low_Band), 1),
                                                           params.at(Names::Threshold_Low_Band),
                                                           juce::NormalisableRange<float>(-60, 12, 1, 1),
                                                           0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Threshold_Mid_Band), 1),
                                                           params.at(Names::Threshold_Mid_Band),
                                                           juce::NormalisableRange<float>(-60, 12, 1, 1),
                                                           0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Threshold_High_Band), 1),
                                                           params.at(Names::Threshold_High_Band),
                                                           juce::NormalisableRange<float>(-60, 12, 1, 1),
                                                           0));
    
    auto attackReleaseRange = juce::NormalisableRange<float>(5, 500, 1, 1);
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Attack_Low_Band), 1),
                                                           params.at(Names::Attack_Low_Band),
                                                           attackReleaseRange,
                                                           5));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Attack_Mid_Band), 1),
                                                           params.at(Names::Attack_Mid_Band),
                                                           attackReleaseRange,
                                                           5));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Attack_High_Band), 1),
                                                           params.at(Names::Attack_High_Band),
                                                           attackReleaseRange,
                                                           5));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Release_Low_Band), 1),
                                                           params.at(Names::Release_Low_Band),
                                                           attackReleaseRange,
                                                           250));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Release_Mid_Band), 1),
                                                           params.at(Names::Release_Mid_Band),
                                                           attackReleaseRange,
                                                           250));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Release_High_Band), 1),
                                                           params.at(Names::Release_High_Band),
                                                           attackReleaseRange,
                                                           250));
    
    auto choices = std::vector<double> { 1, 1.5, 2, 3, 4, 5, 6, 7, 8, 10, 15, 20, 50, 100 };
    juce::StringArray stringArray;
    for (auto choice : choices)
    {
        stringArray.add(juce::String(choice, 1));
    }
    
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(params.at(Names::Ratio_Low_Band), 1),
                                                            params.at(Names::Ratio_Low_Band),
                                                            stringArray,
                                                            2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(params.at(Names::Ratio_Mid_Band), 1),
                                                            params.at(Names::Ratio_Mid_Band),
                                                            stringArray,
                                                            2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(params.at(Names::Ratio_High_Band), 1),
                                                            params.at(Names::Ratio_High_Band),
                                                            stringArray,
                                                            2));
    
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(params.at(Names::Bypassed_Low_Band), 1),
                                                          params.at(Names::Bypassed_Low_Band),
                                                          false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(params.at(Names::Bypassed_Mid_Band), 1),
                                                          params.at(Names::Bypassed_Mid_Band),
                                                          false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(params.at(Names::Bypassed_High_Band), 1),
                                                          params.at(Names::Bypassed_High_Band),
                                                          false));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Low_Mid_Crossover_Freq), 1),
                                                           params.at(Names::Low_Mid_Crossover_Freq),
                                                           juce::NormalisableRange<float>(20, 999, 1, 1),
                                                           400));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(params.at(Names::Mid_High_Crossover_Freq), 1),
                                                           params.at(Names::Mid_High_Crossover_Freq),
                                                           juce::NormalisableRange<float>(1000, 20000, 1, 1),
                                                           2000));
    
    return layout;
}
//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MBCompAudioProcessor();
}
