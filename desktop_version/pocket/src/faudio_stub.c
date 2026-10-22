/* Placeholder FAudio backend: FAudioCreate fails, so musicclass::init()
 * logs "Unable to initialize FAudio" and the game runs silent.  The audio
 * phase (zenibako/VVVVVV#5) replaces FAudio with a converted ADPCM player
 * on of_mixer. */
#include <FAudio.h>

uint32_t FAudioCreate( FAudio **ppFAudio, uint32_t Flags, FAudioProcessor XAudio2Processor )
{
    (void) ppFAudio;
    (void) Flags;
    (void) XAudio2Processor;
    return 1;
}

uint32_t FAudioSourceVoice_FlushSourceBuffers( FAudioSourceVoice *voice )
{
    (void) voice;
    return 1;
}

void FAudioSourceVoice_GetState( FAudioSourceVoice *voice, FAudioVoiceState *pVoiceState, uint32_t Flags )
{
    (void) voice;
    (void) pVoiceState;
    (void) Flags;
}

uint32_t FAudioSourceVoice_Start( FAudioSourceVoice *voice, uint32_t Flags, uint32_t OperationSet )
{
    (void) voice;
    (void) Flags;
    (void) OperationSet;
    return 1;
}

uint32_t FAudioSourceVoice_Stop( FAudioSourceVoice *voice, uint32_t Flags, uint32_t OperationSet )
{
    (void) voice;
    (void) Flags;
    (void) OperationSet;
    return 1;
}

uint32_t FAudioSourceVoice_SubmitSourceBuffer( FAudioSourceVoice *voice, const FAudioBuffer *pBuffer, const FAudioBufferWMA *pBufferWMA )
{
    (void) voice;
    (void) pBuffer;
    (void) pBufferWMA;
    return 1;
}

void FAudioVoice_DestroyVoice(FAudioVoice *voice)
{
    (void) voice;
}

uint32_t FAudioVoice_SetVolume( FAudioVoice *voice, float Volume, uint32_t OperationSet )
{
    (void) voice;
    (void) Volume;
    (void) OperationSet;
    return 1;
}

uint32_t FAudio_CreateMasteringVoice( FAudio *audio, FAudioMasteringVoice **ppMasteringVoice, uint32_t InputChannels, uint32_t InputSampleRate, uint32_t Flags, uint32_t DeviceIndex, const FAudioEffectChain *pEffectChain )
{
    (void) audio;
    (void) ppMasteringVoice;
    (void) InputChannels;
    (void) InputSampleRate;
    (void) Flags;
    (void) DeviceIndex;
    (void) pEffectChain;
    return 1;
}

uint32_t FAudio_CreateSourceVoice( FAudio *audio, FAudioSourceVoice **ppSourceVoice, const FAudioWaveFormatEx *pSourceFormat, uint32_t Flags, float MaxFrequencyRatio, FAudioVoiceCallback *pCallback, const FAudioVoiceSends *pSendList, const FAudioEffectChain *pEffectChain )
{
    (void) audio;
    (void) ppSourceVoice;
    (void) pSourceFormat;
    (void) Flags;
    (void) MaxFrequencyRatio;
    (void) pCallback;
    (void) pSendList;
    (void) pEffectChain;
    return 1;
}

uint32_t FAudio_Release(FAudio *audio)
{
    (void) audio;
    return 1;
}
