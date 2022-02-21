#pragma once

#include "pch.h"
#include <MemoryBuffer.h>
#include <winrt/Windows.Media.Devices.Core.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include "TimeConverter.h"
#include "Tar.h"
#include <mutex>
#include <shared_mutex>
#include <thread>

// Structur used to store per-frame PV information:timestamp, PV2world transform, focal length, pixel buffer
struct PVFrame
{
    long long timestamp = 0;
    winrt::Windows::Foundation::Numerics::float4x4 PVtoWorldtransform{ 0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f };
    float fx = 0.f;
    float fy = 0.f;
    uint8_t* pixelBufferData = nullptr; 
    uint32_t pixelBufferSize = 0; 
    uint32_t width = 0; 
    uint32_t height = 0;

};


class VideoFrameProcessor
{
public:
    VideoFrameProcessor()
    {
    }

    virtual ~VideoFrameProcessor()
    {
        m_fExit = true;
        
        // desallocate m_RGBFrame
        if (m_RGBFrame.pixelBufferData != nullptr)
        {
            delete[] m_RGBFrame.pixelBufferData;
            m_RGBFrame.pixelBufferData = NULL;
            m_RGBFrame.pixelBufferData = 0;
            m_RGBByteArrayNewAvailable = false;
        }
        m_pGrabThread->join();
    }

    void     CopyLastFrame(PVFrame& to_RGBFrame);
    uint32_t GetRGBByteArraySize();
    bool     GetRGBByteArrayNewAvailable();
    uint32_t GetNbFrameArrived();
    uint32_t GetNbFrameConverted();
    uint32_t GetNbFrameCopyInContext();
    uint32_t GetNbFrameCopyToClient();
    uint32_t GetRGBByteArrayWidth();
    uint32_t GetRGBByteArrayHeight();
    //void StartRGBSensorCapture();
    //void StopRGBSensorCapture();

    void VideoFrameProcessor::Clear();
    winrt::Windows::Foundation::IAsyncAction InitializeAsync();
    bool DumpDataToDisk(const winrt::Windows::Storage::StorageFolder& folder, const std::wstring& datetime_path);
    void StartRecording(const winrt::Windows::Storage::StorageFolder& storageFolder, const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& worldCoordSystem);
    void StopRecording();

protected:
    void OnFrameArrived(const winrt::Windows::Media::Capture::Frames::MediaFrameReader& sender,        
                        const winrt::Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs& args);

private:
    void DumpFrame(const winrt::Windows::Graphics::Imaging::SoftwareBitmap& softwareBitmap, long long timestamp);

    winrt::Windows::Media::Capture::Frames::MediaFrameReader m_mediaFrameReader = nullptr;
    
    winrt::event_token m_OnFrameArrivedRegistration;
    std::shared_mutex m_frameMutex;
    winrt::Windows::Media::Capture::Frames::MediaFrameReference m_latestFrame = nullptr;
    
    long long m_latestTimestamp = 0;
    
    TimeConverter m_converter;
    winrt::Windows::Perception::Spatial::SpatialCoordinateSystem m_worldCoordSystem = nullptr;
    bool m_isWorldCoordSystemSet = false; 

    // grabbing thread
    static void CameraGrabThread(VideoFrameProcessor* pProcessor);
    // TODO(jmhenaff): replace by unique_ptr
    std::thread* m_pGrabThread = nullptr;
    bool m_fExit = false;
    PVFrame   m_RGBFrame;


    void AddLogFrame();
    std::vector<PVFrame> m_PVFrameLog;

    // Storage
    std::mutex m_storageMutex;
    winrt::Windows::Storage::StorageFolder m_storageFolder = nullptr;
    std::unique_ptr<Io::Tarball> m_tarball;


    bool      m_RGBByteArrayNewAvailable = false;
    
    // frame counters 
    uint32_t  m_NbFrameArrived = 0;
    uint32_t  m_NbFrameConverted = 0;
    uint32_t  m_NbFrameCopyInContext= 0;
    uint32_t  m_NbFrameCopyToClient = 0;
    
    static const int kImageWidth;
    static const wchar_t kSensorName[3];
};
