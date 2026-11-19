#pragma once
#include <juce_dsp/juce_dsp.h>

namespace vocalis
{

/** Valori degli effetti (letti dai parametri automatizzabili a ogni blocco). */
struct EffectsParams
{
    bool ringOn = false;    float ringFreq = 220.0f, ringMix = 0.5f;
    bool distOn = false;    float distDrive = 12.0f, distMix = 0.6f;
    bool crushOn = false;   float crushBits = 8.0f, crushDown = 4.0f, crushMix = 0.7f;
    bool eqOn = true;       float eqLowCut = 80.0f, eqHighCut = 16000.0f, eqPresence = 0.0f;
    bool compOn = true;     float compThresh = -18.0f, compRatio = 3.0f;
    bool chorusOn = false;  float chorusRate = 0.8f, chorusDepth = 0.3f, chorusMix = 0.4f;
    bool delayOn = false;   int delayDiv = 1; float delayFeedback = 0.35f, delayMix = 0.25f;
    bool reverbOn = false;  float reverbSize = 0.5f, reverbDamp = 0.5f, reverbMix = 0.25f;
    bool deessOn = true;    float deessAmount = 0.4f;            // 0..1
    bool doublerOn = false; float doublerMix = 0.35f, doublerWidth = 0.8f;
    float outGainDb = 0.0f;
    bool limiterOn = true;  float limiterCeilingDb = -0.3f;

    static juce::StringArray delayDivisionNames();
    static double delayDivisionBeats (int i);
};

/** Catena effetti stereo: De-esser -> Ring mod -> Distorsione -> Bitcrusher -> EQ -> Compressore -> Doubler -> Chorus -> Delay -> Riverbero.
    Usata sia in tempo reale (processBlock) sia offline (export WAV). */
class EffectsChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const EffectsParams& p, double bpm);

    /** Secondi di coda da aggiungere in export (delay + riverbero). */
    static double tailSeconds (const EffectsParams& p, double bpm);

private:
    double sr = 44100.0;
    int maxBlock = 512;

    double ringPhase = 0.0;
    float crushHold[2] = {}; int crushCounter = 0;

    using Filter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Filter lowCut, highCut, presence;
    float lastLowCut = -1, lastHighCut = -1, lastPresence = -100;

    juce::dsp::Compressor<float> compressor;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Reverb reverb;

    // De-esser: banda alta separata con crossover Linkwitz-Riley e ridotta quando le sibilanti superano la soglia
    juce::dsp::LinkwitzRileyFilter<float> deessSplit;
    float deessEnv = 0.0f, deessGain = 1.0f;
    juce::AudioBuffer<float> deessHigh;

    // Doubler: due copie ritardate (12/19 ms) con modulazione lenta, aperte a sinistra e destra
    juce::AudioBuffer<float> dblBuf;
    int dblWrite = 0;
    double dblPhase1 = 0.0, dblPhase2 = 0.25;

    float limGain = 1.0f;

    juce::AudioBuffer<float> delayBuf, scratch;
    int delayWrite = 0;
    juce::SmoothedValue<float> delaySamples;
};

/** Effetto "robotizza" (fase azzerata su STFT a hop fisso): voce monotona e metallica. Offline, mono. */
void robotize (juce::AudioBuffer<float>& mono, double sampleRate, float robotHz, float amount);

} // namespace vocalis
