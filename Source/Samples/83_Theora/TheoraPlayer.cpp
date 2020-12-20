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

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Audio/SoundSource.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>

#include "TheoraPlayer.h"
#include "Theora.h"
#include "TheoraAudio.h"
#include <cstdio>

#include <Urho3D/DebugNew.h>
//=============================================================================
//=============================================================================
URHO3D_DEFINE_APPLICATION_MAIN(TheoraPlayer)

//=============================================================================
//=============================================================================
static const unsigned InputDelay = 250;

//=============================================================================
//=============================================================================
TheoraPlayer::TheoraPlayer(Context* context)
    : Sample(context)
    , elapsedTime_(0.0f)
    , stopped_(false)
    , paused_(false)
    , rescaleNode_(true)
{
}

void TheoraPlayer::Setup()
{
    // Called before engine initialization. engineParameters_ member variable can be modified here
    engineParameters_[EP_WINDOW_WIDTH]  = 1280;
    engineParameters_[EP_WINDOW_HEIGHT] = 720;
    engineParameters_[EP_FULL_SCREEN]   = false;
    engineParameters_[EP_SOUND]         = true;
    engineParameters_[EP_SOUND_BUFFER]  = 20;
    engineParameters_[EP_RESOURCE_PATHS]= "Data;CoreData;Data/Theora";
    engineParameters_[EP_LOG_NAME]      = GetSubsystem<FileSystem>()->GetProgramDir() + "theora.log";
    engineParameters_[EP_VSYNC] = true;
    engineParameters_[EP_REFRESH_RATE] = 60;
}

void TheoraPlayer::Start()
{
    // Execute base class startup
    Sample::Start();

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateInstructions();

    // Hook up to the frame update events
    SubscribeToEvents();
}

void TheoraPlayer::CreateScene()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);

    XMLFile *xmlLevel = cache->GetResource<XMLFile>("Theora/Scenes/Scene.xml");
    scene_->LoadXML(xmlLevel->GetRoot());

    // Create a scene node for the camera, which we will move around
    // The camera will use default settings (1000 far clip distance, 45 degrees FOV, set aspect ratio automatically)
    cameraNode_ = scene_->GetChild("Camera", true);
    yaw_ = cameraNode_->GetWorldRotation().YawAngle();

    // Set up a viewport to the Renderer subsystem so that the 3D scene can be seen. We need to define the scene and the camera
    // at minimum. Additionally we could configure the viewport screen size and the rendering path (eg. forward / deferred) to
    // use, but now we just use full screen and default render path configured in the engine command line options
    Renderer* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);

    // create TVComponent
    tvNode_ = scene_->GetChild("TV", true);

    // init theora
    InitializeTheora();
}

void TheoraPlayer::InitializeTheora()
{
    if (tvNode_)
    {
        // select file to play
        String filename = GetSubsystem<FileSystem>()->GetProgramDir()+ "Data/Theora/Video/sira-numb.ogv";
        //String filename = GetSubsystem<FileSystem>()->GetProgramDir()+ "Data/Theora/Video/bbb_theora_486kbit.ogv";

        theora_ = new Theora();
        int result = theora_->Initialize(context_, filename);

        if (result == INIT_OK)
        {
            // get video/audio info
            theoraAVInfo_ = theora_->GetTheoraAVInfo();

            // init
            InitAudio();
            SetOutputModel(tvNode_->GetComponent<StaticModel>());

            // start theora process
            theora_->StartProcess();
        }
        else
        {
            URHO3D_LOGERRORF("Theora init error: %d", result);
        }
    }
}

void TheoraPlayer::Play()
{
    if (stopped_)
    {
        if (!theora_)
        {
            InitializeTheora();
        }

        stopped_ = false;
    }
    paused_ = false;
}

void TheoraPlayer::Pause()
{
    if (stopped_)
    {
        return;
    }

    paused_ = !paused_;

    if (paused_)
    {
        if (theoraAudio_)
        {
            theoraAudio_->Stop();
        }
    }
}

void TheoraPlayer::Stop()
{
    if (!stopped_)
    {
        if (theora_)
        {
            theora_.Reset();
        }
        if (theoraAudio_)
        {
            theoraAudio_->Stop();
            theoraAudio_->Clear();
        }

        elapsedTime_ = 0.0f;
        videoBufferContainer_.Clear();
        audioBufferContainer_.Clear();
        stopped_ = true;
    }
}

void TheoraPlayer::AddElapsedTime(float timeStep)
{
    if (!stopped_ && !paused_)
    {
        elapsedTime_ += timeStep;
        elapsedTime64_ = static_cast<int64_t>(1000.0f * elapsedTime_);

        if (theora_)
        {
            theora_->SetElapsedTime(elapsedTime_);

            ProcessAudioVideo();
        }
    }
}

void TheoraPlayer::ProcessAudioVideo()
{
    if (!theora_)
    {
        return;
    }

    // get audio/video buffers
    SharedPtr<VideoData> vptr = theora_->GetVideoQueueData();
    while (vptr != NULL)
    {
        videoBufferContainer_.Push(vptr);
        vptr = theora_->GetVideoQueueData();
    }
    SharedPtr<AudioData> aptr = theora_->GetAudioQueueData();
    while (aptr != NULL)
    {
        audioBufferContainer_.Push(aptr);
        aptr = theora_->GetAudioQueueData();
    }

    // write video
    while (videoBufferContainer_.Size() > 0)
    {
        if (videoBufferContainer_[0]->time_ <= elapsedTime64_)
        {
            rgbaTexture_->SetSize(theoraAVInfo_.videoFrameWidth_, theoraAVInfo_.videoFrameHeight_, 
                                  Graphics::GetRGBAFormat(), TEXTURE_DYNAMIC);
            rgbaTexture_->SetData(0, 0, 0, theoraAVInfo_.videoFrameWidth_, theoraAVInfo_.videoFrameHeight_, 
                                  (const void*)videoBufferContainer_[0]->buf_);
            videoBufferContainer_.Erase(0);
        }
        else
        {
            break;
        }
    }

    // write audio
    bool gotAudioBuff = false;
    while (audioBufferContainer_.Size() > 0)
    {
        if (audioBufferContainer_[0]->time_ <= elapsedTime64_)
        {
            theoraAudio_->AddData(audioBufferContainer_[0]->buf_, audioBufferContainer_[0]->size_);
            audioBufferContainer_.Erase(0);
            gotAudioBuff = true;
        }
        else
        {
            break;
        }
    }

    if (gotAudioBuff)
    {
        if (!theoraAudio_->IsPlaying())
        {
            theoraAudio_->Play();
        }
    }
}

bool TheoraPlayer::SetOutputModel(StaticModel* model)
{
    bool ret = false;

    if (model)
    {
        // Set model surface
        outputModel_ = model;
        outputMaterial_ = model->GetMaterial(0);

        // Create textures & images
        ScaleModelAccordingVideoRatio();
        InitTexture();
        ret = true;
    }

    return ret;
}

void TheoraPlayer::ScaleModelAccordingVideoRatio()
{
    if (outputModel_ && rescaleNode_)
    {
        Node* node = outputModel_->GetNode();
        float ratioW = (float)theoraAVInfo_.videoFrameWidth_ / (float)theoraAVInfo_.videoFrameHeight_;
        float ratioH = (float)theoraAVInfo_.videoFrameHeight_ / (float)theoraAVInfo_.videoFrameWidth_;

        Vector3 originalScale = node->GetScale();
        node->SetScale(Vector3(originalScale.x_, originalScale.x_ * ratioH, originalScale.z_ * ratioH));
        rescaleNode_ = false;
    }
}

void TheoraPlayer::InitAudio()
{
    if (!theoraAudio_)
    {
        theoraAudio_ = new TheoraAudio(context_);
        SoundSource *soundSource = tvNode_->CreateComponent<SoundSource>();
        theoraAudio_->Init(soundSource, theoraAVInfo_.audioFrequencey_, theoraAVInfo_.audioSixteenBits_, theoraAVInfo_.audioStereo_);
    }
}

bool TheoraPlayer::InitTexture()
{
    bool success = false;

    // RGBA texture
    if (outputMaterial_)
    {
        if (!framePlanarDataRGBA_)
        {
            framePlanarDataRGBA_ = new unsigned char[theoraAVInfo_.videoFrameWidth_ * theoraAVInfo_.videoFrameHeight_ * RGBAComponentSize];
            rgbaTexture_ = SharedPtr<Texture2D>(new Texture2D(context_));
            rgbaTexture_->SetSize(theoraAVInfo_.videoFrameWidth_, theoraAVInfo_.videoFrameHeight_, Graphics::GetRGBAFormat(), TEXTURE_DYNAMIC);
            rgbaTexture_->SetFilterMode(FILTER_BILINEAR);
            rgbaTexture_->SetNumLevels(1);

            outputMaterial_->SetTexture(TextureUnit::TU_DIFFUSE, rgbaTexture_);
        }
        success = true;
    }

    return success;
}

void TheoraPlayer::CreateInstructions()
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    UI* ui = GetSubsystem<UI>();

    // Construct new Text object, set string to display and font to use
    Text* instructionText = ui->GetRoot()->CreateChild<Text>();
    instructionText->SetText("WASD - move\nVid: J - play, K - toggle pause, L - stop");
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 12);

    // Position the text relative to the screen center
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetPosition(0, 10);
}

void TheoraPlayer::SubscribeToEvents()
{
    // Subscribe HandleUpdate() function for processing update events
    SubscribeToEvent(E_UPDATE, URHO3D_HANDLER(TheoraPlayer, HandleUpdate));
}

void TheoraPlayer::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;

    // Take the frame time step, which is stored as a float
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    AddElapsedTime(timeStep);

    // Move the camera, scale movement with time step
    MoveCamera(timeStep);
}

void TheoraPlayer::MoveCamera(float timeStep)
{
    // Do not move if the UI has a focused element (the console)
    if (GetSubsystem<UI>()->GetFocusElement())
        return;

    Input* input = GetSubsystem<Input>();

    // Movement speed as world units per second
    const float MOVE_SPEED = 20.0f;
    // Mouse sensitivity as degrees per pixel
    const float MOUSE_SENSITIVITY = 0.1f;

    // Use this frame's mouse motion to adjust camera node yaw and pitch. Clamp the pitch between -90 and 90 degrees
    IntVector2 mouseMove = input->GetMouseMove();
    yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
    pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
    pitch_ = Clamp(pitch_, -90.0f, 90.0f);

    // Construct new orientation for the camera scene node from yaw and pitch. Roll is fixed to zero
    cameraNode_->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

    // Read WASD keys and move the camera scene node to the corresponding direction if they are pressed
    // Use the Translate() function (default local space) to move relative to the node's orientation.
    if (input->GetKeyDown(KEY_W))
        cameraNode_->Translate(Vector3::FORWARD * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_S))
        cameraNode_->Translate(Vector3::BACK * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_A))
        cameraNode_->Translate(Vector3::LEFT * MOVE_SPEED * timeStep);
    if (input->GetKeyDown(KEY_D))
        cameraNode_->Translate(Vector3::RIGHT * MOVE_SPEED * timeStep);

    // video control
    if (input->GetKeyDown(KEY_J))
    {
        if (inputTimer_.GetMSec(false) > InputDelay)
        {
            Play();
            inputTimer_.Reset();
        }
    }
    if (input->GetKeyDown(KEY_K))
    {
        if (inputTimer_.GetMSec(false) > InputDelay)
        {
            Pause();
            inputTimer_.Reset();
        }
    }
    if (input->GetKeyDown(KEY_L))
    {
        if (inputTimer_.GetMSec(false) > InputDelay)
        {
            Stop();
            inputTimer_.Reset();
        }
    }
}

