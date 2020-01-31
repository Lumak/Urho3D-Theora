#pragma once

#include <Urho3D/Scene/Component.h>

#include <ogg/ogg.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
//=============================================================================
//=============================================================================
namespace Urho3D
{
class StaticModel;
class Material;
class Texture2D;
class File;
}

using namespace Urho3D;

//=============================================================================
//=============================================================================
class URHO3D_API TVComponent : public Component
{
    URHO3D_OBJECT(TVComponent, Component);

public:
    SharedPtr<StaticModel> outputModel_;
    SharedPtr<Material> outputMaterial_;

    unsigned prevTime_;
    unsigned prevFrame_;
    SharedArrayPtr<unsigned char> framePlanarDataRGBA_;
    SharedPtr<Texture2D> rgbaTexture_;

    TVComponent(Context* context);
    virtual ~TVComponent();
    static void RegisterObject(Context* context);

    bool OpenFileName(const String& name);
    bool SetOutputModel(StaticModel* sm);
    void Play();
    void Pause();
    void Loop(bool isLoop = true);
    void Stop();
    unsigned Advance(float timeStep);

    void ScaleModelAccordingVideoRatio();

    int GetFrameWidth(void) const { return frameWidth_; };
    int GetFrameHeight(void) const { return frameHeight_; };
    float GetFramesPerSecond(void) const { return framesPerSecond_; };
    void UpdatePlaneTextures();

private:
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    bool OpenFile(const String& fileName);
    int BufferData(void);
    void DecodeVideoFrame(void);
    bool InitTexture();
    void Yuv420pToRgba8888();

private:
    SharedPtr<File> file_;
    float framesPerSecond_;
    bool isFileOpened_;
    bool isStopped_;
    unsigned frameWidth_;
    unsigned frameHeight_;
    float videoTimer_;
    unsigned lastVideoFrame_;

    /* Ogg stuff */
    ogg_sync_state      m_OggSyncState;
    ogg_page            m_OggPage;
    ogg_packet          m_OggPacket;
    ogg_stream_state    m_VideoStream;

    /* Theora stuff */
    theora_info         m_TheoraInfo;
    theora_comment      m_TheoraComment;
    theora_state        m_TheoraState;
    yuv_buffer          m_YUVFrame;

    //=============================================================================
    // Function ref - below functions are from 
    // Android Open Source Project (AOSP), APACHE LICENSE 2.0
    //=============================================================================
    typedef uint8_t uint8;
    typedef uint32_t uint32;
    typedef int32_t int32;

    union Rgba {
      uint32 color;
      uint8 channel[4];
    };

    // Channel index constants
    static const uint8 kRed = 0;
    static const uint8 kGreen = 1;
    static const uint8 kBlue = 2;
    static const uint8 kAlpha = 3;

    // Clamp to range 0-255
    static inline uint8 clamp(int32 x)
    {
      return (uint8)(x > 255 ? 255 : (x < 0 ? 0 : x));
    }

    static inline Rgba convertYuvToRgba(int32 y, int32 u, int32 v)
    {
      Rgba color;
      color.channel[kRed] = clamp(y + static_cast<int>(1.402f * v));
      color.channel[kGreen] = clamp(y - static_cast<int>(0.344f * u + 0.714f * v));
      color.channel[kBlue] = clamp(y + static_cast<int>(1.772f * u));
      color.channel[kAlpha] = 0xff;
      return color;
    }

};