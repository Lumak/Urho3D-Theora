#pragma once


//=============================================================================
//=============================================================================
using namespace Urho3D;

extern const int RGBAComponentSize;

enum TheoraErrorType
{
    CODEC_FRAMERATE_ERROR = -6,
    CODEC_YUV_ERROR = -5,
    CODEC_HEADER_ERROR = -4,
    VB_PACKET_ERROR = -3,
    TH_PACKET_ERROR = -2,
    FILE_ERROR = -1,
    INIT_OK = 0,
};

struct TheoraAVInfo
{
    TheoraAVInfo()
    {
        videoFrameWidth_ = 0;
        videoFrameHeight_ = 0;
        videoFrameRate_ = 0.0f;

        audioFrequencey_ = 0;
        audioSixteenBits_ = true;
        audioStereo_ = true;

        initialized_ = false;
    }

    unsigned videoFrameWidth_;
    unsigned videoFrameHeight_;
    float videoFrameRate_;

    long audioFrequencey_;
    bool audioSixteenBits_;
    bool audioStereo_;
    bool initialized_;
};

template <class T>
class TheoraData: public RefCounted
{
public:
    TheoraData() : buf_(0), size_(0), time_(0)
    {
    }

    virtual ~TheoraData()
    {
        buf_ = NULL;
    }

    SharedArrayPtr<T>  buf_;
    int                size_;
    int64_t            time_;
};

typedef TheoraData<signed short> AudioData;
typedef TheoraData<unsigned char> VideoData;

//=============================================================================
//=============================================================================
