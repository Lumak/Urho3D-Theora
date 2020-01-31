#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Resource/ResourceCache.h>

#include "TVComponent.h"

#include <Urho3D/DebugNew.h>
//=============================================================================
//=============================================================================
static const int k_SyncBufferSize = 8192;
static const int RGBAComponentSize = 4;

//=============================================================================
//=============================================================================
TVComponent::TVComponent(Context* context)
    : Component(context)
    , isFileOpened_(false)
    , isStopped_(false)
    , frameWidth_(0)
    , frameHeight_(0)
    , file_(0)
    , outputMaterial_(0)
    , prevTime_(0)
    , prevFrame_(0)
{
    SubscribeToEvent(E_SCENEPOSTUPDATE, URHO3D_HANDLER(TVComponent, HandleUpdate));
}

TVComponent::~TVComponent()
{
    if (isFileOpened_)
    {
        ogg_stream_clear(&m_VideoStream);
        theora_clear(&m_TheoraState);
        theora_comment_clear(&m_TheoraComment);
        theora_info_clear(&m_TheoraInfo);
        ogg_sync_clear(&m_OggSyncState);
    }
}

void TVComponent::RegisterObject(Context* context)
{
    context->RegisterFactory<TVComponent>();
}

bool TVComponent::OpenFileName(const String& name)
{
    // init theora
    ogg_sync_init(&m_OggSyncState);
    theora_comment_init(&m_TheoraComment);
    theora_info_init(&m_TheoraInfo);

    isFileOpened_ = false;

    if (OpenFile(name))
    {
        bool dataFound = false;
        int theoraPacketsFound = 0;

        while (!dataFound)
        {
            // grab some data from file and put it into the ogg stream
            BufferData();

            // grab the ogg page from the stream
            while (ogg_sync_pageout(&m_OggSyncState, &m_OggPage) > 0)
            {
                ogg_stream_state test;

                // check: if this is not headers page, then we finished
                if (!ogg_page_bos(&m_OggPage))
                {
                    // all headers pages are finished, now there are only data packets
                    dataFound = true;

                    // don't leak the page, get it into the video stream
                    ogg_stream_pagein(&m_VideoStream, &m_OggPage);
                    break;
                }

                // we need to identify the stream

                // 1) Init the test stream with the s/n from our page
                ogg_stream_init(&test, ogg_page_serialno(&m_OggPage));
                // 2) Add this page to this test stream
                ogg_stream_pagein(&test, &m_OggPage);
                // 3) Decode the page into the packet
                ogg_stream_packetout(&test, &m_OggPacket);

                // try to interpret the packet as Theora's data
                if (!theoraPacketsFound && theora_decode_header(&m_TheoraInfo, &m_TheoraComment, &m_OggPacket) >= 0)
                {
                    // theora found ! Let's copy the stream
                    memcpy(&m_VideoStream, &test, sizeof(test));
                    theoraPacketsFound++;
                }
                else
                {
                    // non-theora (vorbis maybe)
                    ogg_stream_clear(&test);
                }
            }
        }

        // no theora found, maybe this is music file ?
        if (theoraPacketsFound)
        {
            int err;
            // by specification we need 3 header packets for any logical stream (theora, vorbis, etc.)
            while (theoraPacketsFound < 3)
            {
                err = ogg_stream_packetout(&m_VideoStream, &m_OggPacket);
                if (err < 0)
                {
                    // some stream errors (maybe stream corrupted?)
                    break;
                }
                if (err > 0)
                {
                    if (theora_decode_header(&m_TheoraInfo, &m_TheoraComment, &m_OggPacket) >= 0)
                        theoraPacketsFound++;
                    else
                    {
                        // another stream corruption ?
                        break;
                    }
                }

                // if read nothing from packet - just grab more data into packet
                if (!err)
                {
                    if (ogg_sync_pageout(&m_OggSyncState, &m_OggPage) > 0)
                    {
                        // if data arrivet into packet - put it into our logical stream
                        ogg_stream_pagein(&m_VideoStream, &m_OggPage);
                    }
                    else
                    {
                        // nothing goint from the ogg stream, need to read some data from file
                        if (!BufferData())
                        {
                            // f***k ! End of file :(
                            break;
                        }
                    }
                }
            }
        }

        // if we have theora ok
        if (theoraPacketsFound)
        {
            // init decoder
            if (0 == theora_decode_init(&m_TheoraState, &m_TheoraInfo))
            {
                // decoder intialization succeed
                isFileOpened_ = true;

                frameWidth_ = m_TheoraInfo.frame_width;
                frameHeight_ = m_TheoraInfo.frame_height;
                framesPerSecond_ = static_cast<float>(m_TheoraInfo.fps_numerator) / static_cast<float>(m_TheoraInfo.fps_denominator);
                videoTimer_ = 0;
                isStopped_ = false;
            }
        }
    }

    return isFileOpened_;
}

bool TVComponent::SetOutputModel(StaticModel* sm)
{
    bool ret = false;

    if (sm)
    {
        // Set model surface
        outputModel_ = sm;
        outputMaterial_ = sm->GetMaterial(0);

        // Create textures & images
        InitTexture();
        ScaleModelAccordingVideoRatio();
        ret = true;
    }

    return ret;
}

void TVComponent::Play()
{
    isStopped_ = false;
}

void TVComponent::Pause() 
{
    isStopped_ = true;
}

void TVComponent::Loop(bool isLoop)
{

}

void TVComponent::Stop()
{
    isStopped_ = true;
    videoTimer_ = 0;
    prevFrame_ = 0;
    file_->Seek(0);
}

void TVComponent::HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace Update;
    float timeStep = eventData[P_TIMESTEP].GetFloat();

    unsigned frame = Advance(timeStep);

    if (!isStopped_ && prevFrame_ != frame)
    {
        UpdatePlaneTextures();		
    }

    prevFrame_ = frame;
}

bool TVComponent::OpenFile(const String& fileName)
{
    file_ = new File(context_, fileName, FILE_READ);
    return (file_ != 0 && file_->IsOpen());
}

int TVComponent::BufferData()
{
    if (!file_ || !file_->IsOpen())
    {
      return 0;
    }

    unsigned s = file_->GetSize();

    // ask some buffer for putting data into stream
    char* buffer = ogg_sync_buffer(&m_OggSyncState, k_SyncBufferSize);
    // read data from file
    int bytes = file_->Read(buffer, k_SyncBufferSize);
    // put readed data into Ogg stream
    ogg_sync_wrote(&m_OggSyncState, bytes);

    return bytes;
}

unsigned TVComponent::Advance(float timeStep)
{
    if (isStopped_)
    {
        return lastVideoFrame_;
    }

    // advance video timer by delta time in milliseconds
    videoTimer_ += timeStep;

    // calculate current frame
    const unsigned curFrame = static_cast<unsigned>(floor(videoTimer_ * framesPerSecond_));


    if (lastVideoFrame_ != curFrame)
    {
        lastVideoFrame_ = curFrame;
        DecodeVideoFrame();
    }

    return curFrame;
}

void TVComponent::DecodeVideoFrame()
{
    // first of all - grab some data into ogg packet
    while (ogg_stream_packetout(&m_VideoStream, &m_OggPacket) <= 0)
    {
        // if no data in video stream, grab some data
        if (!BufferData())
        {
            isStopped_ = true;
            return;
        }

        // grab all decoded ogg pages into our video stream
        while (ogg_sync_pageout(&m_OggSyncState, &m_OggPage) > 0)
        {
            ogg_stream_pagein(&m_VideoStream, &m_OggPage);
        }
    }

    // load packet into theora decoder
    if (0 == theora_decode_packetin(&m_TheoraState, &m_OggPacket))
    {
        // if decoded ok - get YUV frame
        theora_decode_YUVout(&m_TheoraState, &m_YUVFrame);
    }
    else
    {
        isStopped_ = true;
    }
}

bool TVComponent::InitTexture()
{
    bool success = false;

    // do this for fill m_YUVFrame with properly info about frame
    Advance(0);

    // RGBA texture
    if (outputMaterial_)
    {
      framePlanarDataRGBA_ = new unsigned char[m_YUVFrame.y_width * m_YUVFrame.y_height * RGBAComponentSize];
      rgbaTexture_ = SharedPtr<Texture2D>(new Texture2D(context_));
      rgbaTexture_->SetSize(m_YUVFrame.y_width, m_YUVFrame.y_height, Graphics::GetRGBAFormat(), TEXTURE_DYNAMIC);
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
void TVComponent::Yuv420pToRgba8888() 
{
  uint8* pInY = m_YUVFrame.y;
  uint8* pInU = m_YUVFrame.u;
  uint8* pInV = m_YUVFrame.v;
  Rgba* pOutColor = (Rgba*)framePlanarDataRGBA_.Get();
  int width = m_YUVFrame.uv_width;
  int height = m_YUVFrame.uv_height;

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
      y3 = pInY[x*2 + m_YUVFrame.y_stride];
      y4 = pInY[x*2 + m_YUVFrame.y_stride + 1];

      u = pInU[x] - 128;
      v = pInV[x] - 128;

      pOutColor[x*2] = convertYuvToRgba(y1, u, v);
      pOutColor[x*2 + 1] = convertYuvToRgba(y2, u, v);
      pOutColor[x*2 + m_YUVFrame.y_width] = convertYuvToRgba(y3, u, v);
      pOutColor[x*2 + m_YUVFrame.y_width + 1] = convertYuvToRgba(y4, u, v);
    }

    pInU += m_YUVFrame.uv_stride;
    pInV += m_YUVFrame.uv_stride;
    pInY += m_YUVFrame.y_stride * 2;
    pOutColor += m_YUVFrame.y_width * 2;
  }
}

void TVComponent::UpdatePlaneTextures() 
{
    if (framePlanarDataRGBA_)
    {
        Yuv420pToRgba8888();

        rgbaTexture_->SetSize(m_YUVFrame.y_width, m_YUVFrame.y_height, Graphics::GetRGBAFormat(), TEXTURE_DYNAMIC);
        rgbaTexture_->SetData(0, 0, 0, m_YUVFrame.y_width, m_YUVFrame.y_height, (const void*)framePlanarDataRGBA_);
    }
}

void TVComponent::ScaleModelAccordingVideoRatio()
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
