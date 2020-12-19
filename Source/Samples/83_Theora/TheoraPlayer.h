//
// Copyright (c) 2008-2020 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "Sample.h"
#include "TheoraData.h"

//=============================================================================
//=============================================================================
namespace Urho3D
{
class Node;
class Scene;
class StaticModel;
class Material;
class Texture2D;
}

class Theora;
class TheoraAudio;

//=============================================================================
//=============================================================================
class TheoraPlayer : public Sample
{
    URHO3D_OBJECT(TheoraPlayer, Sample);

public:
    /// Construct.
    TheoraPlayer(Context* context);

    virtual void Setup();
    virtual void Start();

private:
    void CreateScene();
    void InitializeTheora();
    void AddElapsedTime(float timeStep);
    void ProcessAudioVideo();
    void Play();
    void Pause();
    void Stop();

    bool SetOutputModel(StaticModel *model);
    void ScaleModelAccordingVideoRatio();
    void InitAudio();
    bool InitTexture();
    /// Construct an instruction text to the UI.
    void CreateInstructions();
    /// Subscribe to application-wide logic update events.
    void SubscribeToEvents();
    /// Handle the logic update event.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Read input and moves the camera.
    void MoveCamera(float timeStep);

private:
    SharedPtr<Theora> theora_;
    Vector<SharedPtr<VideoData>> videoBufferContainer_;
    Vector<SharedPtr<AudioData>> audioBufferContainer_;
    SharedPtr<StaticModel> outputModel_;
    SharedPtr<Material> outputMaterial_;
    SharedArrayPtr<unsigned char> framePlanarDataRGBA_;
    SharedPtr<Texture2D> rgbaTexture_;

    SharedPtr<TheoraAudio> theoraAudio_;
    TheoraAVInfo theoraAVInfo_;
    WeakPtr<Node> tvNode_;
    bool rescaleNode_;

    float elapsedTime_;
    int64_t elapsedTime64_;
    bool stopped_;
    bool paused_;
    Timer inputTimer_;
};
