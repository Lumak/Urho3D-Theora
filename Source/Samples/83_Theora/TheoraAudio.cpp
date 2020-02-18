#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Audio/SoundSource.h>
#include <Urho3D/Resource/ResourceCache.h>

#include "TheoraAudio.h"

#include <Urho3D/DebugNew.h>
//=============================================================================
//=============================================================================
TheoraAudio::TheoraAudio(Context* context)
{
    soundSource_ = NULL;
}

TheoraAudio::~TheoraAudio()
{
    Clear();
}

void TheoraAudio::Init(SoundSource *soundSource, long frequency, bool sixteenBit, bool stereo)
{
    soundSource_ = soundSource;
    SetFormat(frequency, sixteenBit, stereo);
}

void TheoraAudio::Play()
{
    soundSource_->Play(this);
}

void TheoraAudio::Stop()
{
    soundSource_->Stop();
}

bool TheoraAudio::IsPlaying() const
{
    return soundSource_->IsPlaying();
}

unsigned TheoraAudio::WriteData(void* data, unsigned numBytes)
{
    BufferedSoundStream::AddData(data, numBytes);
    return numBytes;
}

void TheoraAudio::Clear()
{
    BufferedSoundStream::Clear();
}




