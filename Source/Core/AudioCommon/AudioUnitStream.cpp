// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AudioCommon/AudioUnitStream.h"

#ifdef IPHONEOS
#include "AudioCommon/Mixer.h"

AudioUnitStream::~AudioUnitStream()
{
  if (!m_unit)
    return;
  AudioOutputUnitStop(m_unit);
  AudioUnitUninitialize(m_unit);
  AudioComponentInstanceDispose(m_unit);
}

bool AudioUnitStream::Init()
{
  AudioComponentDescription desc{};
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_RemoteIO;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent component = AudioComponentFindNext(nullptr, &desc);
  if (!component || AudioComponentInstanceNew(component, &m_unit) != noErr)
    return false;

  // What the mixer gives: interleaved stereo s16 at its rate.
  AudioStreamBasicDescription format{};
  format.mSampleRate = m_mixer->GetSampleRate();
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = 16;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = 4;
  format.mBytesPerPacket = 4;
  AURenderCallbackStruct callback{&AudioUnitStream::Render, this};
  return AudioUnitSetProperty(m_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                              &format, sizeof(format)) == noErr &&
         AudioUnitSetProperty(m_unit, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 0, &callback, sizeof(callback)) == noErr &&
         AudioUnitInitialize(m_unit) == noErr;
}

bool AudioUnitStream::SetRunning(bool running)
{
  return (running ? AudioOutputUnitStart(m_unit) : AudioOutputUnitStop(m_unit)) == noErr;
}

OSStatus AudioUnitStream::Render(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                                 UInt32, UInt32 frames, AudioBufferList* data)
{
  auto* self = static_cast<AudioUnitStream*>(ref);
  s16* out = static_cast<s16*>(data->mBuffers[0].mData);
  self->m_mixer->Mix(out, frames);
  if (self->m_volume < 1.0f)
    for (UInt32 i = 0; i < frames * 2; ++i)
      out[i] = static_cast<s16>(out[i] * self->m_volume);
  return noErr;
}
#endif
