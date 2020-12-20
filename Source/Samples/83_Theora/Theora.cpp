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

#include <Urho3D/DebugNew.h>
//=============================================================================
//=============================================================================
static const int SyncBufferSize = 4096;
static const float VideoAdvanceFrames = 10.0f;
static const float AudioAdvanceFrames = VideoAdvanceFrames + 1.0f;
const int RGBAComponentSize = 4;

//=============================================================================
//=============================================================================
Theora::Theora()
    : elapsedTime_(0)
    , fnExited_(false)
    , threadEnabled_(true)
    , file_(0)
    , videoAdvanceTime_(0)
    , audioAdvanceTime_(0)
    , stopAV_(false)

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
    , audioFdFragSize_(0)
    , audioTime_(0)

    , postProcessLevelMax_(0)
    , postProcessLevel_(0)
    , postProcessIncrement_(0)

    , frames_(0)
    , dropped_(0)
    , audioFills_(0)
{
}

Theora::~Theora()
{
    // force exit thread fn
    WaitExit();

    if (thSetupInfo_)
    {
        th_setup_free(thSetupInfo_);
        thSetupInfo_ = NULL;
    }

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

int Theora::Initialize(Context *context, const String& filename)
{
    int result = 0;
    context_ = context;

    if (OpenFile(filename))
    {
      result = InitTheora();
    }
    else
    {
      result = FILE_ERROR;
    }

    return result;
}

bool Theora::StartProcess()
{
    bool result = false;

    if (theoraAVInfo_.initialized_)
    {
        result = Thread::Run();
    }

    return result;
}

bool Theora::Run()
{
	return false;
}

void Theora::SetElapsedTime(float elapsedTime)
{
    MutexLock lock(mutexTimer_);
    elapsedTime_ = static_cast<int64_t>(elapsedTime * 1000.0f);
}

int64_t Theora::GetElapsedTime()
{
    MutexLock lock(mutexTimer_);
    return elapsedTime_;
}

void Theora::UpdateTimer()
{
  int64_t elapsedTime = GetElapsedTime();
  videoAdvanceTime_ = elapsedTime + static_cast<int64_t>(1000.0f * VideoAdvanceFrames/theoraAVInfo_.videoFrameRate_);
  audioAdvanceTime_ = elapsedTime + static_cast<int64_t>(1000.0f * AudioAdvanceFrames/theoraAVInfo_.videoFrameRate_);
}

const TheoraAVInfo& Theora::GetTheoraAVInfo() const
{
    return theoraAVInfo_;
}

void Theora::StoreVideoQueueData(SharedPtr<VideoData> theoraData)
{
    MutexLock lock(mutexVideoBuff_);
    videoBufferContainer_.Push(theoraData);
}

SharedPtr<VideoData> Theora::GetVideoQueueData()
{
    MutexLock lock(mutexVideoBuff_);
    SharedPtr<VideoData> ptr(0);
    if (videoBufferContainer_.Size() > 0)
    {
        ptr = videoBufferContainer_[0];
        videoBufferContainer_.Erase(0);
    }
    return ptr;
}

void Theora::StoreAudioQueueData(SharedPtr<AudioData> theoraData)
{
    MutexLock lock(mutexAudioBuff_);
    audioBufferContainer_.Push(theoraData);
}

SharedPtr<AudioData> Theora::GetAudioQueueData()
{
    MutexLock lock(mutexAudioBuff_);
    SharedPtr<AudioData> ptr(0);
    if (audioBufferContainer_.Size() > 0)
    {
        ptr = audioBufferContainer_[0];
        audioBufferContainer_.Erase(0);
    }
    return ptr;
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

      // error if the denom is near zero
      if (thInfo_.fps_denominator < M_EPSILON)
      {
        return CODEC_FRAMERATE_ERROR;
      }

      // error if not the right video encoding
      if (thInfo_.pixel_fmt != TH_PF_420)
      {
          return CODEC_YUV_ERROR;
      }

      // populate av info
      theoraAVInfo_.videoFrameWidth_ = thInfo_.frame_width;
      theoraAVInfo_.videoFrameHeight_ = thInfo_.frame_height;
      theoraAVInfo_.videoFrameRate_ = (float)thInfo_.fps_numerator/(float)thInfo_.fps_denominator;
    }
    else
    {
      /* tear down the partial theora setup */
      th_info_clear(&thInfo_);
      th_comment_clear(&thComment_);
    }

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

    if (vbPacket_)
    {
        audioFdFragSize_ = vbDspState_.pcm_storage * 2;
        audiobuf_ = new int16_t[audioFdFragSize_];
        audiobufFill_ = 0;
        int audioFillGranuleSize = audioFdFragSize_/2/vbInfo_.channels;
        audioFillGranuleTime_ = static_cast<int64_t>(1000.0 * audioFillGranuleSize / vbInfo_.rate);

        theoraAVInfo_.audioFrequencey_ = vbInfo_.rate;
        theoraAVInfo_.audioSixteenBits_ = true;
        theoraAVInfo_.audioStereo_ = vbInfo_.channels > 1 ? true : false;
    }

    // set as initialized
    theoraAVInfo_.initialized_ = true;
    elapsedTime_ = 0;

    stateFlag_ = 0; /* playback has not begun */

    videoAdvanceTime_ = static_cast<int64_t>(1000.0f * VideoAdvanceFrames/theoraAVInfo_.videoFrameRate_);
    audioAdvanceTime_ = static_cast<int64_t>(1000.0f * AudioAdvanceFrames/theoraAVInfo_.videoFrameRate_);
    UpdateFrames();

    return INIT_OK;
}

void Theora::ThreadFunction()
{
    unsigned sleepMS = static_cast<unsigned>(1000.0f / (theoraAVInfo_.videoFrameRate_ * 2.0f));

    while (true)
    {
        UpdateTimer();

        if (videobufTime_ < videoAdvanceTime_ || audioTime_ < audioAdvanceTime_)
        {
            UpdateFrames();
        }

        if (!GetThreadEnabled())
        {
            break;
        }

        Time::Sleep(sleepMS);
    }

    SetFnExit(true);
}

void Theora::UpdateFrames()
{
    int processOggPackets = 0;

    // see the note about this at the bottom of this loop
	while (processOggPackets < 2)
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
                ++audioFills_;

				if (vbDspState_.granulepos >= 0)
				{
					audiobufGranulePos_ = (long)vbDspState_.granulepos - ret + i;
				}
				else
				{
					audiobufGranulePos_ += i;
				}

                audioTime_ = static_cast<int64_t>(1000.0 * vorbis_granule_time(&vbDspState_, audiobufGranulePos_));

                if (audiobufFill_ == audioFdFragSize_)
                {
                    audiobufReady_ = 1;

                    // buffer audio
                    SharedPtr<AudioData> audioData(new AudioData());
                    audioData->time_ = audioTime_ - audioFillGranuleTime_;
                    audioData->size_ = audiobufFill_;
                    audioData->buf_ = new int16_t[audiobufFill_];
                    memcpy(audioData->buf_.Get(), audiobuf_, audiobufFill_ * sizeof(int16_t));

                    StoreAudioQueueData(audioData);

                    // clear buffer idx position
                    audiobufFill_ = 0;
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

            if (oggPacket_.granulepos >= 0)
            {
              th_decode_ctl(thDecCtx_, TH_DECCTL_SET_GRANPOS, &oggPacket_.granulepos, sizeof(oggPacket_.granulepos));
            }

            if (th_decode_packetin(thDecCtx_, &oggPacket_, &videobufGranulePos_) == 0)
            {
              videobufTime_ = static_cast<int64_t>(1000.0 * th_granule_time(thDecCtx_, videobufGranulePos_));
              frames_++;

              // write buffer
			  videobufReady_ = 1;
              VideoWrite();
            }
          }
          else
          {
            break;
          }
        }

        if (videobufTime_ > videoAdvanceTime_ && audioTime_ > audioAdvanceTime_)
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

		// post clear (to prevent continuous buffer fetching above)
        if (audiobufReady_)
        {
            audiobufReady_ = 0;
        }
        if (videobufReady_)
        {
            videobufReady_ = 0;
        }

        // even when at EoF, there are still data already buffered in the 
        // ogg stream states, and we want to allow that to get processed at
        // least one cycle per call into this routine.
        if (FileEof())
        {
            ++processOggPackets;
        }
	}
}

void Theora::WaitExit()
{
    SetThreadEnable(false);

    do 
    { 
        Time::Sleep(1); 
    }
    while (!GetFnExited());
}

void Theora::SetThreadEnable(bool enable)
{
    MutexLock lock(mutexThreadEnable_);
    threadEnabled_ = enable;
}

bool Theora::GetThreadEnabled()
{
    MutexLock lock(mutexThreadEnable_);
    return threadEnabled_;
}

void Theora::SetFnExit(bool bset)
{
    MutexLock lock(mutexExit_);
    fnExited_ = bset;
}

bool Theora::GetFnExited()
{
    MutexLock lock(mutexExit_);
    return fnExited_;
}

bool Theora::OpenFile(const String& fileName)
{
    file_ = new File(context_, fileName, FILE_READ);
    return (file_ != 0 && file_->IsOpen());
}

bool Theora::FileEof() const
{
    if (!file_ || !file_->IsOpen())
    {
      return true;
    }

    return file_->IsEof();
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

void Theora::VideoWrite()
{
    th_ycbcr_buffer yuv;
    th_decode_ycbcr_out(thDecCtx_, yuv);

    SharedPtr<VideoData> ptr(new VideoData());
    ptr->size_ = theoraAVInfo_.videoFrameWidth_ * theoraAVInfo_.videoFrameHeight_ * RGBAComponentSize;
    ptr->buf_ = new unsigned char[ptr->size_];
    ptr->time_ = videobufTime_;

    // convert
    Yuv420pToRgba8888(yuv, ptr);

    // queue buffer
    StoreVideoQueueData(ptr);
}

//=============================================================================
// Function ref - below function is a modified version of nativeYuv420pToRgba8888
// function found in Android Open Source Project (AOSP), APACHE LICENSE 2.0
//=============================================================================
void Theora::Yuv420pToRgba8888(const th_ycbcr_buffer &yuv, SharedPtr<VideoData> ptr) 
{
    uint8* pInY = yuv[0].data;
    uint8* pInU = yuv[1].data;
    uint8* pInV = yuv[2].data;
    Rgba* pOutColor = (Rgba*)ptr->buf_.Get();
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

void Theora::DumpInfo()
{
    URHO3D_LOGINFOF("Ogg logical stream 0x%x is Theora %d x %d, fps=%f",
           oggThStreamState_.serialno, thInfo_.pic_width, thInfo_.pic_height, (float)thInfo_.fps_numerator/thInfo_.fps_denominator);

    switch (thInfo_.pixel_fmt)
    {
    case TH_PF_420: URHO3D_LOGINFO(" 4:2:0 video"); break;
    case TH_PF_422: URHO3D_LOGINFO(" 4:2:2 video - not supported"); break;
    case TH_PF_444: URHO3D_LOGINFO(" 4:4:4 video - not supported"); break;
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
