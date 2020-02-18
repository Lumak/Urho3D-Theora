#pragma once

#include <Urho3D/Audio/BufferedSoundStream.h>

//=============================================================================
//=============================================================================
namespace Urho3D
{
class SoundSource;
}

using namespace Urho3D;

//=============================================================================
//=============================================================================
class TheoraAudio : public BufferedSoundStream
{
public:
    TheoraAudio(Context* context);
    virtual ~TheoraAudio();

    void Init(SoundSource *soundSource, long frequency, bool sixteenBit, bool stereo);
    void Play();
    void Stop();
    bool IsPlaying() const;
    void Clear();

    unsigned WriteData(void* data, unsigned numBytes);

protected:
    WeakPtr<SoundSource> soundSource_;
};

