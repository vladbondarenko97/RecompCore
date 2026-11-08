// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "AudioCommon/SoundStream.h"

#ifdef IPHONEOS
#include <AudioToolbox/AudioToolbox.h>
#endif

// iOS output through the RemoteIO audio unit (cubeb's backend is macOS-only).
class AudioUnitStream final : public SoundStream
{
#ifdef IPHONEOS
public:
  ~AudioUnitStream() override;
  bool Init() override;
  bool SetRunning(bool running) override;
  void SetVolume(int volume) override { m_volume = volume / 100.0f; }
  static bool IsValid() { return true; }

private:
  static OSStatus Render(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                         UInt32 frames, AudioBufferList* data);
  AudioUnit m_unit = nullptr;
  float m_volume = 1.0f;
#endif
};
