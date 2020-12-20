#pragma once

#include <Urho3D/Core/Thread.h>
#include <Urho3D/Container/RefCounted.h>

#include <ogg/ogg.h>
#include <theora/theora.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>

#include "TheoraData.h"

//=============================================================================
//=============================================================================
using namespace Urho3D;
namespace Urho3D
{
class Context;
}

//=============================================================================
//=============================================================================
class URHO3D_API Theora : public Thread, public RefCounted
{
public:
    Theora();
    virtual ~Theora();

    int Initialize(Context *context, const String& filename);
    const TheoraAVInfo& GetTheoraAVInfo() const;

    // control and buffer
    bool StartProcess();
    void SetElapsedTime(float timer);
    SharedPtr<VideoData> GetVideoQueueData();
    SharedPtr<AudioData> GetAudioQueueData();

private:
    int InitTheora();

    // threaded background fn
    virtual void ThreadFunction();
    // override the thread Run() - not virtual but having the same name blocks access
    bool Run();
    int64_t GetElapsedTime();
    void UpdateTimer();
    void UpdateFrames();

    // buffer container methods
    void StoreVideoQueueData(SharedPtr<VideoData> theoraData);
    void StoreAudioQueueData(SharedPtr<AudioData> theoraData);

    void WaitExit();
    void SetThreadEnable(bool enable);
    bool GetThreadEnabled();
    void SetFnExit(bool bset);
    bool GetFnExited();

    bool OpenFile(const String& fileName);
    bool FileEof() const;
    int BufferData();
    int QueuePage(ogg_page *page);
    void VideoWrite();
    void Yuv420pToRgba8888(const th_ycbcr_buffer &yuv, SharedPtr<VideoData> ptr);
    void DumpInfo();

private:
    TheoraAVInfo        theoraAVInfo_;
    WeakPtr<Context>    context_;
    SharedPtr<File>     file_;

    int64_t             elapsedTime_;
    int64_t             videoAdvanceTime_;
    int64_t             audioAdvanceTime_;

    // buffers
    Vector<SharedPtr<VideoData>>  videoBufferContainer_;
    Vector<SharedPtr<AudioData>>  audioBufferContainer_;

    // threading properties
    Mutex            mutexTimer_;
    Mutex            mutexExit_;
    Mutex            mutexAudioBuff_;
    Mutex            mutexVideoBuff_;
    Mutex            mutexThreadEnable_;
    bool             threadEnabled_;
    bool             fnExited_;
    bool             stopAV_;

    // theora audio and video
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

    int              videobufReady_;
    int64_t          videobufGranulePos_;
    int64_t          videobufTime_;

    // audio fragment audio buffering
    int              audioFdFragSize_;
    int              audiobufFill_;
    int              audiobufReady_;
    SharedArrayPtr<int16_t> audiobuf_;
	int64_t          audiobufGranulePos_; /* time position of last sample */
	int64_t          audioTime_;
    int64_t          audioFillGranuleTime_;

    int              postProcessLevelMax_;
    int              postProcessLevel_;
    int              postProcessIncrement_;

    int              audioFills_;
    int              frames_;
    int              dropped_;

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