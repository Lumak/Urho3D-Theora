#pragma once

#include <Urho3D/Scene/Component.h>

#include <ogg/ogg.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>

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

class TheoraAudio;

//=============================================================================
//=============================================================================
class URHO3D_API Theora : public Component
{
    URHO3D_OBJECT(Theora, Component);

public:
    Theora(Context* context);
    virtual ~Theora();
    static void RegisterObject(Context* context);

    bool OpenFileName(const String& name);
    bool SetOutputModel(StaticModel* sm);
    void Play();
    void Pause();
    void Loop(bool isLoop = true);
    void Stop();

private:
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    bool OpenFile(const String& fileName);
    void ScaleModelAccordingVideoRatio();
    int BufferData();
    bool FileEof() const;
    void DecodeVideoFrame();
    bool InitTexture();
    void Yuv420pToRgba8888(const th_ycbcr_buffer &yuv);

    int InitTheora();
    void DumpInfo();
    void UpdateTheora(float timeStep);
    int QueuePage(ogg_page *page);
    void AddElapsedTime(float timeStep);
    ogg_int64_t GetElapsedTime();

    ogg_int64_t GetTime();
    void AudioCalibrateTimer(int restart);
    void AudioWrite();
    void VideoWrite();

private:
    SharedPtr<StaticModel> outputModel_;
    SharedPtr<Material> outputMaterial_;
    SharedArrayPtr<unsigned char> framePlanarDataRGBA_;
    SharedPtr<Texture2D> rgbaTexture_;

    SharedPtr<File>     file_;
    bool                isFileOpened_;
    bool                isStopped_;
    unsigned            frameWidth_;
    unsigned            frameHeight_;
    float               videoTimer_;

    // audio and video
    ogg_sync_state   oggSyncState_;
    ogg_page         oggPage_;
    ogg_stream_state oggVbStreamState_;
    ogg_stream_state oggThStreamState_;
    ogg_packet       oggPacket_;

    th_info          thInfo_;
    th_comment       thComment_;
    th_dec_ctx       *thDecCtx_;
    th_setup_info    *thSetupInfo_;
    th_pixel_fmt     thPixelFmt_;

    vorbis_info      vbInfo_;
    vorbis_comment   vbComment_;
    vorbis_dsp_state vbDspState_;
    vorbis_block     vbBlock_;

    int              thPacket_;
    int              vbPacket_;
    int              stateFlag_;

    // single frame video buffering
    int              videobufReady_;
    ogg_int64_t      videobufGranulePos_;
    ogg_int64_t      videobufTime_;

    // single audio fragment audio buffering
    int              audiobufFill_;
    int              audiobufReady_;
    SharedArrayPtr<ogg_int16_t>      audiobuf_;

    long             audioFdTotalSize_;
    int              audioFdFragSize_;      /* read and write only complete fragments
                                           so that SNDCTL_DSP_GETOSPACE is
                                           accurate immediately after a bank
                                           switch */

    ogg_int64_t      audiobufGranulePos_; /* time position of last sample */
    ogg_int64_t      audioFdTimerCalibrate_;
    ogg_int64_t      elapsedTime_;
    ogg_int64_t      updateTime_;
    ogg_int64_t      audioTime_;


    int              postProcessLevelMax_;
    int              postProcessLevel_;
    int              postProcessIncrement_;

    int              frames_;
    int              dropped_;

    SharedPtr<TheoraAudio> theoraAudio_;

    enum InitErrorType
    {
        CODEC_HEADER_ERROR = -3,
        VB_PACKET_ERROR = -2,
        TH_PACKET_ERROR = -1,
        INIT_OK = 0,
    };

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