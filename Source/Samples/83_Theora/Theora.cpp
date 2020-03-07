#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Audio/SoundSource.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/IO/Log.h>
#include <stdio.h>

#include "Theora.h"
#include "TheoraAudio.h"

#include <Urho3D/DebugNew.h>
//=============================================================================
//=============================================================================
static const int SyncBufferSize = 4096;
static const int RGBAComponentSize = 4;

#define MAX_AUDIO_TIME_OFFSET   10LL
#define NEG_AUDIO_TIME_OFFSET   -5LL
#define AUDIO_PACKET_UNINIT     -1000LL

//=============================================================================
//=============================================================================
Theora::Theora(Context* context)
    : Component(context)
    , isFileOpened_(false)
    , isStopped_(false)
    , frameWidth_(0)
    , frameHeight_(0)
    , file_(0)
    , outputMaterial_(0)

    , thDecCtx_(NULL)
    , thSetupInfo_(NULL)

    , thPacket_(0)
    , vbPacket_(0)
    , stateFlag_(0)

    , videobufReady_(0)
    , videobufGranulePos_(-1)
    , videobufTime_(0)

    , audiobufFill_(0)
    , audiobufReady_(0)
    , audiobufGranulePos_(0)

    , audioFdTotalSize_(-1)
    , audioFdFragSize_(0)

    , audioFdTimerCalibrate_(-1)
    , audioTime_(0)

    , postProcessLevelMax_(0)
    , postProcessLevel_(0)
    , postProcessIncrement_(0)

    , frames_(0)
    , dropped_(0)

    , updateTime_(0)
{
    SubscribeToEvent(E_SCENEPOSTUPDATE, URHO3D_HANDLER(Theora, HandleUpdate));
}

Theora::~Theora()
{
    //audio_close();

    if (vbPacket_)
    {
      ogg_stream_clear(&oggVbStreamState_);
      vorbis_block_clear(&vbBlock_);
      vorbis_dsp_clear(&vbDspState_);
      vorbis_comment_clear(&vbComment_);
      vorbis_info_clear(&vbInfo_);
    }

    if (thPacket_)
    {
      ogg_stream_clear(&oggThStreamState_);
      th_decode_free(thDecCtx_);
      th_comment_clear(&thComment_);
      th_info_clear(&thInfo_);
    }
    ogg_sync_clear(&oggSyncState_);
}

void Theora::RegisterObject(Context* context)
{
    context->RegisterFactory<Theora>();
    //context->RegisterFactory<TheoraAudio>();
}

bool Theora::OpenFileName(const String& name)
{
    isFileOpened_ = false;

    if (OpenFile(name))
    {
        if (InitTheora() == INIT_OK)
        {
            isFileOpened_ = true;
        }
    }

    return isFileOpened_;
}

int Theora::InitTheora()
{
    /* start up Ogg stream synchronization layer */
    ogg_sync_init(&oggSyncState_);

    /* init supporting Vorbis structures needed in header parsing */
    vorbis_info_init(&vbInfo_);
    vorbis_comment_init(&vbComment_);

    /* init supporting Theora structures needed in header parsing */
    th_comment_init(&thComment_);
    th_info_init(&thInfo_);

    /* Ogg file open; parse the headers */
    /* Only interested in Vorbis/Theora streams */
    while (!stateFlag_)
    {
      int ret = BufferData();
      if (ret == 0)
      {
          break;
      }

      while (ogg_sync_pageout(&oggSyncState_, &oggPage_) > 0)
      {
        ogg_stream_state test;

        /* is this a mandated initial header? If not, stop parsing */
        if (!ogg_page_bos(&oggPage_))
        {
          /* don't leak the page; get it into the appropriate stream */
          QueuePage(&oggPage_);
          stateFlag_ = 1;
          break;
        }

        ogg_stream_init(&test, ogg_page_serialno(&oggPage_));
        ogg_stream_pagein(&test, &oggPage_);
        ogg_stream_packetout(&test, &oggPacket_);

        /* identify the codec: try theora */
        if (!thPacket_ && th_decode_headerin(&thInfo_, &thComment_, &thSetupInfo_, &oggPacket_) >= 0)
        {
          /* it is theora */
          memcpy(&oggThStreamState_, &test, sizeof(test));
          thPacket_ = 1;
        }
        else if (!vbPacket_ && vorbis_synthesis_headerin(&vbInfo_, &vbComment_, &oggPacket_) >= 0)
        {
          /* it is vorbis */
          memcpy(&oggVbStreamState_, &test, sizeof(test));
          vbPacket_ = 1;
        }
        else
        {
          /* whatever it is, we don't care about it */
          ogg_stream_clear(&test);
        }
      }
      /* fall through oggThStreamState_ non-bos page parsing */
    }

    /* we're expecting more header packets. */
    while ((thPacket_ && thPacket_ < 3) || (vbPacket_ && vbPacket_ < 3))
    {
      int ret;

      /* look for further theora headers */
      while (thPacket_ && (thPacket_ < 3) && (ret = ogg_stream_packetout(&oggThStreamState_, &oggPacket_)))
      {
        if (ret < 0)
        {
          URHO3D_LOGERROR("Error parsing Theora stream headers; corrupt stream?");
          return TH_PACKET_ERROR;
        }

        if (!th_decode_headerin(&thInfo_, &thComment_, &thSetupInfo_, &oggPacket_))
        {
          URHO3D_LOGERROR("Error parsing Theora stream headers; corrupt stream?");
          return TH_PACKET_ERROR;
        }
        ++thPacket_;
      }

      /* look for more vorbis header packets */
      while (vbPacket_ && (vbPacket_ < 3) && (ret = ogg_stream_packetout(&oggVbStreamState_, &oggPacket_)))
      {
        if (ret < 0)
        {
          URHO3D_LOGERROR("Error parsing Vorbis stream headers; corrupt stream?");
          return VB_PACKET_ERROR;
        }

        if (vorbis_synthesis_headerin(&vbInfo_, &vbComment_, &oggPacket_))
        {
          URHO3D_LOGERROR("Error parsing Vorbis stream headers; corrupt stream?");
          return VB_PACKET_ERROR;
        }
        ++vbPacket_;
      }

      /* The header pages/packets will arrive before anything else we
         care about, or the stream is not obeying spec */

      if (ogg_sync_pageout(&oggSyncState_, &oggPage_) > 0)
      {
        QueuePage(&oggPage_); /* demux into the appropriate stream */
      }
      else
      {
        int ret = BufferData(); /* someone needs more data */
        if (ret == 0)
        {
          URHO3D_LOGERROR("End of file while searching for codec headers.");
          return CODEC_HEADER_ERROR;
        }
      }
    }



    /* and now we have it all.  initialize decoders */
    if (thPacket_)
    {
      thDecCtx_ = th_decode_alloc(&thInfo_, thSetupInfo_);

      thPixelFmt_ = thInfo_.pixel_fmt;

      DumpInfo();

      th_decode_ctl(thDecCtx_, TH_DECCTL_GET_PPLEVEL_MAX, &postProcessLevelMax_, sizeof(postProcessLevelMax_));
      postProcessLevel_ = postProcessLevelMax_;
      th_decode_ctl(thDecCtx_, TH_DECCTL_SET_PPLEVEL, &postProcessLevel_, sizeof(postProcessLevel_));
      postProcessIncrement_ = 0;

      // init decoder
      frameWidth_ = thInfo_.frame_width;
      frameHeight_ = thInfo_.frame_height;
      videoTimer_ = 0;
      isStopped_ = false;
    }
    else
    {
      /* tear down the partial theora setup */
      th_info_clear(&thInfo_);
      th_comment_clear(&thComment_);
    }

    th_setup_free(thSetupInfo_);
    thSetupInfo_ = NULL;

    if (vbPacket_)
    {
      vorbis_synthesis_init(&vbDspState_, &vbInfo_);
      vorbis_block_init(&vbDspState_, &vbBlock_);

      URHO3D_LOGINFOF("Ogg logical stream 0x%x is Vorbis: %d channel, %d Hz audio.",
                      oggVbStreamState_.serialno, vbInfo_.channels, vbInfo_.rate);
    }
    else
    {
      /* tear down the partial vorbis setup */
      vorbis_info_clear(&vbInfo_);
      vorbis_comment_clear(&vbComment_);
    }
    elapsedTime_ = 0;

    /* open audio */
    if (vbPacket_)
    {
        //open_audio();
        audioFdFragSize_ = (vbDspState_.pcm_storage/2);
        // approximate total size
        audioFdTotalSize_ = audioFdFragSize_ * 2;
        audiobuf_ = new ogg_int16_t[audioFdTotalSize_];

        theoraAudio_ = new TheoraAudio(context_);
        SoundSource *soundSource = node_->CreateComponent<SoundSource>();
        const bool sixteenBits = true;
        const bool stereo = vbInfo_.channels > 1?true:false;
        theoraAudio_->Init(soundSource, vbInfo_.rate, sixteenBits, stereo);
    }

    /* open video */
    if (thPacket_)
    {
        //open_video();
    }

    stateFlag_ = 0; /* playback has not begun */

    return INIT_OK;
}

void Theora::UpdateTheora(float timeStep)
{
    /* on to the main decode loop.  We assume in this example that audio
       and video start roughly together, and don't begin playback until
       we have a start frame for both.  This is not necessarily a valid
       assumption in Ogg A/V streams! It will always be true of the
       example_encoder (and most streams) though. */

    AddElapsedTime(timeStep);
    int numLoops = 0;

    // time correction
    ogg_int64_t audioOffset = audioTime_ - elapsedTime_;

    if (audioOffset > MAX_AUDIO_TIME_OFFSET)
    {
        return;
    }

    // process stream
    while (!FileEof())
    {
      /* we want a video and audio frame ready to go at all times.  If
         we have to buffer incoming, buffer the compressed data (ie, let
         ogg do the buffering) */
      while (vbPacket_ && !audiobufReady_)
      {
        int ret;
        float **pcm;

        /* if there's pending, decoded audio, grab it */
        if ((ret = vorbis_synthesis_pcmout(&vbDspState_, &pcm)) > 0)
        {
          int countIdx = audiobufFill_/2;
          int maxsamples = (audioFdFragSize_ - audiobufFill_)/2/vbInfo_.channels;
          int i;
          for (i = 0; i < ret && i < maxsamples; ++i)
          {
              for (int j = 0; j < vbInfo_.channels; ++j)
              {
                  audiobuf_[countIdx++] = Clamp(FloorToInt(pcm[j][i] * 32767.0f), -32768, 32767);
              }
          }

          vorbis_synthesis_read(&vbDspState_, i);
          audiobufFill_ += i * vbInfo_.channels * 2;

          if (audiobufFill_ == audioFdFragSize_)
          {
              audiobufReady_ = 1;
          }

          if (vbDspState_.granulepos >= 0)
          {
            audiobufGranulePos_ = (long)vbDspState_.granulepos - ret + i;
          }
          else
          {
            audiobufGranulePos_ += i;
          }
        }
        else
        {
          /* no pending audio; is there a pending packet to decode? */
          if (ogg_stream_packetout(&oggVbStreamState_, &oggPacket_) > 0)
          {
            if (vorbis_synthesis(&vbBlock_, &oggPacket_) == 0) /* test for success! */
            {
              vorbis_synthesis_blockin(&vbDspState_, &vbBlock_);
              audioTime_ = (ogg_int64_t)(1000.0 * vorbis_granule_time(&vbDspState_, vbDspState_.granulepos));
            }
          }
          else   /* we need more data; break out to suck in another page */
          {
            break;
          }
        }
      }

      while (thPacket_ && !videobufReady_)
      {
        /* theora is one in, one out... */
        if (ogg_stream_packetout(&oggThStreamState_, &oggPacket_) > 0)
        {
          if (postProcessIncrement_)
          {
            postProcessLevel_ += postProcessIncrement_;
            th_decode_ctl(thDecCtx_, TH_DECCTL_SET_PPLEVEL, &postProcessLevel_, sizeof(postProcessLevel_));
            postProcessIncrement_ = 0;
          }
          /*HACK: This should be set after a seek or a gap, but we might not have
             a granulepos for the first packet (we only have them for the last
             packet on a page), so we just set it as often as we get it.
            To do this right, we should back-track from the last packet on the
             page and compute the correct granulepos for the first packet after
             a seek or a gap.*/
          if (oggPacket_.granulepos >= 0)
          {
            th_decode_ctl(thDecCtx_, TH_DECCTL_SET_GRANPOS, &oggPacket_.granulepos, sizeof(oggPacket_.granulepos));
          }

          if (th_decode_packetin(thDecCtx_, &oggPacket_, &videobufGranulePos_) == 0)
          {
            videobufTime_ = (ogg_int64_t)(1000.0 * th_granule_time(thDecCtx_, videobufGranulePos_));
            frames_++;

            /* is it already too old to be useful?  This is only actually
               useful cosmetically after a SIGSTOP.  Note that we have to
               decode the frame even if we don't show it (for now) due to
               keyframing.  Soon enough libtheora will be able to deal
               with non-keyframe seeks.  */
            if (videobufTime_ >= GetTime())
            {
              videobufReady_ = 1;
            }
            else
            {
              /*If we are too slow, reduce the pp level.*/
              postProcessIncrement_ = postProcessLevel_ > 0 ? -1 : 0;
              dropped_++;
            }
          }
        }
        else
        {
          break;
        }
      }

      if (!videobufReady_ && !audiobufReady_ && FileEof())
      {
          break;
      }

      if (!videobufReady_ || !audiobufReady_)
      {
        /* no data yet for somebody.  Grab another page */
        BufferData();

        while (ogg_sync_pageout(&oggSyncState_, &oggPage_) > 0)
        {
          QueuePage(&oggPage_);
        }
      }

      /* if our buffers either don't exist or are ready to go,
         we can begin playback */
      if ((!thPacket_ || videobufReady_) && (!vbPacket_ || audiobufReady_))
      {
          stateFlag_ = 1;
      }

      /* If playback has begun, top audio buffer off immediately. */
      if (stateFlag_)
      {
          AudioWrite();
          if (!theoraAudio_->IsPlaying())
          {
              theoraAudio_->Play();
          }
      }
      #ifdef DBG_TIMERS
      char buff[256];
      #endif

      /* are we at or past time for this video frame? */
      if (stateFlag_ && videobufReady_ && videobufTime_ <= GetTime())
      {
          #ifdef DBG_TIMERS
          sprintf(buff, "aud=%I64d, vid=%I64d, e=%I64d", audioTime_, videobufTime_, GetElapsedTime());
          URHO3D_LOGINFOF("%s", buff);
          #endif

          VideoWrite();
          videobufReady_ = 0;
      }

      /* same if we've run out of input */
      if (FileEof())
      {
          break;
      }

      // correction loops
      // note: audioTime_ will remain negative (-1000) until vorbis actually acquires a vorbis packet
      audioOffset = audioTime_ - elapsedTime_;

      if (audioTime_ != AUDIO_PACKET_UNINIT && audioOffset < NEG_AUDIO_TIME_OFFSET)
      {
          #ifdef DBG_TIMERS
          sprintf(buff, "audio offset=%I64d, loops=%d", audioOffset, numLoops);
          URHO3D_LOGINFOF(buff);
          ++numLoops;
          #endif
      }
      else
      {
          break;
      }
    }
}

bool Theora::SetOutputModel(StaticModel* sm)
{
    bool ret = false;

    if (sm)
    {
        // Set model surface
        outputModel_ = sm;
        outputMaterial_ = sm->GetMaterial(0);

        // Create textures & images
        ScaleModelAccordingVideoRatio();
        InitTexture();
        ret = true;
    }

    return ret;
}

void Theora::Play()
{
    isStopped_ = false;
    if (theoraAudio_)
    {
        theoraAudio_->Play();
    }
}

void Theora::Pause() 
{
    isStopped_ = true;
    if (theoraAudio_)
    {
        theoraAudio_->Stop();
    }
}

void Theora::Loop(bool isLoop)
{

}

void Theora::Stop()
{
    isStopped_ = true;
    file_->Seek(0);

    videoTimer_ = 0;
    videobufTime_ = 0;
    elapsedTime_ = 0;
    stateFlag_ = 0;
    updateTime_ = 0;

    audioTime_ = 0;
    audioFdTimerCalibrate_ = -1;
    audiobufGranulePos_ = 0;
    audiobufFill_ = 0;
    videobufReady_ = 0;
    audiobufReady_ = 0;

    if (theoraAudio_)
    {
        theoraAudio_->Stop();
        theoraAudio_->Clear();
    }

    ogg_stream_reset(&oggVbStreamState_);
    ogg_stream_reset(&oggThStreamState_);
}

void Theora::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    if (!isStopped_)
    {
        UpdateTheora(timeStep);
    }
}

bool Theora::OpenFile(const String& fileName)
{
    file_ = new File(context_, fileName, FILE_READ);
    return (file_ != 0 && file_->IsOpen());
}

int Theora::BufferData()
{
    if (!file_ || !file_->IsOpen())
    {
      return 0;
    }

    // ask some buffer for putting data into stream
    char* buffer = ogg_sync_buffer(&oggSyncState_, SyncBufferSize);
    // read data from file
    int bytes = file_->Read(buffer, SyncBufferSize);
    // put readed data into Ogg stream
    if (ogg_sync_wrote(&oggSyncState_, bytes) < 0)
    {
        bytes = 0;
    }

    return bytes;
}

bool Theora::FileEof() const
{
    if (!file_ || !file_->IsOpen())
    {
      return true;
    }

    return file_->IsEof();
}

bool Theora::InitTexture()
{
    bool success = false;

    // RGBA texture
    if (outputMaterial_)
    {
      framePlanarDataRGBA_ = new unsigned char[frameWidth_ * frameHeight_ * RGBAComponentSize];
      rgbaTexture_ = SharedPtr<Texture2D>(new Texture2D(context_));
      rgbaTexture_->SetSize(frameWidth_, frameHeight_, Graphics::GetRGBAFormat(), TEXTURE_DYNAMIC);
      rgbaTexture_->SetFilterMode(FILTER_BILINEAR);
      rgbaTexture_->SetNumLevels(1);

      outputMaterial_->SetTexture(TextureUnit::TU_DIFFUSE, rgbaTexture_);
      success = true;
    }

    return success;
}

//=============================================================================
// Function ref - below function is a modified version of nativeYuv420pToRgba8888
// function found in Android Open Source Project (AOSP), APACHE LICENSE 2.0
//=============================================================================
void Theora::Yuv420pToRgba8888(const th_ycbcr_buffer &yuv) 
{
    uint8* pInY = yuv[0].data;
    uint8* pInU = yuv[1].data;
    uint8* pInV = yuv[2].data;
    Rgba* pOutColor = (Rgba*)framePlanarDataRGBA_.Get();
    int width = yuv[1].width;
    int height = yuv[1].height;

    if (!pInY || !pInU || !pInV || !pOutColor)
    {
        return;
    }

    for (int y = 0; y < height; ++y)
    {
      for (int x = 0; x < width; ++x)
      {
        int u, v, y1, y2, y3, y4;

        y1 = pInY[x*2];
        y2 = pInY[x*2 + 1];
        y3 = pInY[x*2 + yuv[0].stride];
        y4 = pInY[x*2 + yuv[0].stride + 1];

        u = pInU[x] - 128;
        v = pInV[x] - 128;

        pOutColor[x*2] = convertYuvToRgba(y1, u, v);
        pOutColor[x*2 + 1] = convertYuvToRgba(y2, u, v);
        pOutColor[x*2 + yuv[0].width] = convertYuvToRgba(y3, u, v);
        pOutColor[x*2 + yuv[0].width + 1] = convertYuvToRgba(y4, u, v);
      }

      pInU += yuv[1].stride;
      pInV += yuv[2].stride;
      pInY += yuv[0].stride * 2;
      pOutColor += yuv[0].width * 2;
    }
}

void Theora::ScaleModelAccordingVideoRatio()
{
    if (outputModel_)
    {
        Node* node = outputModel_->GetNode();
        float ratioW = (float)frameWidth_ / (float)frameHeight_;
        float ratioH = (float)frameHeight_ / (float)frameWidth_;

        Vector3 originalScale = node->GetScale();
        node->SetScale(Vector3(originalScale.x_, originalScale.x_ * ratioH, originalScale.z_ * ratioH));
    }
}

int Theora::QueuePage(ogg_page *page)
{
  if (thPacket_)
  {
      ogg_stream_pagein(&oggThStreamState_, page);
  }
  if (vbPacket_)
  {
      ogg_stream_pagein(&oggVbStreamState_, page);
  }
  return 0;
}

void Theora::AddElapsedTime(float timeStep)
{
    videoTimer_ += timeStep;
    elapsedTime_ = (ogg_int64_t)(videoTimer_ * 1000.0f);
}

ogg_int64_t Theora::GetElapsedTime()
{
    return elapsedTime_;
}

/* write a fragment to the OSS kernel audio API, but only if we can
   stuff in a whole fragment without blocking */
void Theora::AudioWrite()
{
  if (audiobufReady_)
  {
    long bytes = audiobufFill_;

    if (bytes >= audioFdFragSize_)
    {
      if (bytes == audioFdFragSize_)
      {
          AudioCalibrateTimer(1);
      }

      theoraAudio_->WriteData(audiobuf_.Get(), audiobufFill_);

      audiobufFill_ = 0;
      audiobufReady_ = 0;
    }
  }
}

ogg_int64_t Theora::GetTime()
{
  ogg_int64_t now = GetElapsedTime();

  if (audioFdTimerCalibrate_ == -1)
  {
      audioFdTimerCalibrate_ = now;
  }

  return (now - audioFdTimerCalibrate_);
}

/* call this only immediately after unblocking from a full kernel
   having a newly empty fragment or at the point of DMA restart */
void Theora::AudioCalibrateTimer(int restart)
{
  ogg_int64_t current_sample;
  ogg_int64_t new_time = GetElapsedTime();

  if (restart)
  {
    current_sample = audiobufGranulePos_ - audiobufFill_/2/vbInfo_.channels;
  }
  else
  {
    current_sample = audiobufGranulePos_ - (audiobufFill_ + audioFdTotalSize_ - audioFdFragSize_)/2/vbInfo_.channels;
  }

  new_time -= 1000 * current_sample / vbInfo_.rate;

  if (new_time < 0)
  {
      new_time = 0;
  }

  audioFdTimerCalibrate_ = new_time;
}

void Theora::VideoWrite()
{
    th_ycbcr_buffer yuv;

    th_decode_ycbcr_out(thDecCtx_, yuv);
    Yuv420pToRgba8888(yuv);

    // update texture
    rgbaTexture_->SetSize(yuv[0].width, yuv[0].height, Graphics::GetRGBAFormat(), TEXTURE_DYNAMIC);
    rgbaTexture_->SetData(0, 0, 0, yuv[0].width, yuv[0].height, (const void*)framePlanarDataRGBA_);
}

void Theora::DumpInfo()
{
    URHO3D_LOGINFOF("Ogg logical stream 0x%x is Theora %d x %d, fps=%f",
           oggThStreamState_.serialno, thInfo_.pic_width, thInfo_.pic_height, (float)thInfo_.fps_numerator/thInfo_.fps_denominator);

    switch (thInfo_.pixel_fmt)
    {
    case TH_PF_420: URHO3D_LOGINFO(" 4:2:0 video"); break;
    case TH_PF_422: URHO3D_LOGINFO(" 4:2:2 video"); break;
    case TH_PF_444: URHO3D_LOGINFO(" 4:4:4 video"); break;
    case TH_PF_RSVD:
    default:
        URHO3D_LOGINFO(" video -- (UNKNOWN Chroma sampling!)");
        break;
    }

    if (thInfo_.pic_width != thInfo_.frame_width || thInfo_.pic_height != thInfo_.frame_height)
    {
        URHO3D_LOGINFOF("  Frame content is %d x %d with offset (%d, %d).",
                        thInfo_.frame_width, thInfo_.frame_height, thInfo_.pic_x, thInfo_.pic_y);
    }

    switch(thInfo_.colorspace)
    {
    case TH_CS_UNSPECIFIED:
    /* nothing to report */
        break;
    case TH_CS_ITU_REC_470M:
        URHO3D_LOGINFO("  encoder specified ITU Rec 470M (NTSC) color.");
        break;
    case TH_CS_ITU_REC_470BG:
        URHO3D_LOGINFO("  encoder specified ITU Rec 470BG (PAL) color.");
        break;
    default:
        URHO3D_LOGINFOF("warning: encoder specified unknown colorspace (%d).",
                        thInfo_.colorspace);
        break;
    }

    URHO3D_LOGINFOF("Encoded by %s", thComment_.vendor);

    if (thComment_.comments)
    {
        URHO3D_LOGINFO("theora comment header:");
        for (int i = 0; i < thComment_.comments; ++i)
        {
            if (thComment_.user_comments[i])
            {
                String comment(thComment_.user_comments[i], thComment_.comment_lengths[i]);
                URHO3D_LOGINFOF("\t%s", comment.CString());
            }
        }
    }
}
