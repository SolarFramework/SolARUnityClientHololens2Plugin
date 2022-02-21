/**
 * @copyright Copyright (c) 2021-2022 B-com http://www.b-com.com/
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pch.h"
#include "VideoFrameProcessor.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>

using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage;

const int VideoFrameProcessor::kImageWidth = 760;
const wchar_t VideoFrameProcessor::kSensorName[3] = L"PV";

winrt::Windows::Foundation::IAsyncAction VideoFrameProcessor::InitializeAsync() // 
{
    auto mediaFrameSourceGroups{ co_await MediaFrameSourceGroup::FindAllAsync() };

    
    MediaFrameSourceGroup selectedSourceGroup = nullptr;
    MediaCaptureVideoProfile profile = nullptr;
    MediaCaptureVideoProfileMediaDescription desc = nullptr;
    std::vector<MediaFrameSourceInfo> selectedSourceInfos;

    // Find MediaFrameSourceGroup
    for (const MediaFrameSourceGroup& mediaFrameSourceGroup : mediaFrameSourceGroups)
    {
        auto knownProfiles = MediaCapture::FindKnownVideoProfiles(mediaFrameSourceGroup.Id(), KnownVideoProfile::VideoConferencing);
        for (const auto& knownProfile : knownProfiles)
        {
            for (auto knownDesc : knownProfile.SupportedRecordMediaDescription())
            {
                if ((knownDesc.Width() == kImageWidth)) // && (std::round(knownDesc.FrameRate()) == 15))
                {
                    profile = knownProfile;
                    desc = knownDesc;
                    selectedSourceGroup = mediaFrameSourceGroup;
                    break;
                }
            }
        }
    }

    winrt::check_bool(selectedSourceGroup != nullptr);

    for (auto sourceInfo : selectedSourceGroup.SourceInfos())
    {
        // Workaround since multiple Color sources can be found,
        // and not all of them are necessarily compatible with the selected video profile
        if (sourceInfo.SourceKind() == MediaFrameSourceKind::Color)
        {
            selectedSourceInfos.push_back(sourceInfo);
        }
    }
    winrt::check_bool(!selectedSourceInfos.empty());
    
    // Initialize a MediaCapture object
    MediaCaptureInitializationSettings settings;
    settings.VideoProfile(profile);
    settings.RecordMediaDescription(desc);
    settings.VideoDeviceId(selectedSourceGroup.Id());
    settings.StreamingCaptureMode(StreamingCaptureMode::Video);
    settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);
    settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
    settings.SourceGroup(selectedSourceGroup);

    MediaCapture mediaCapture = MediaCapture();
    co_await mediaCapture.InitializeAsync(settings);

    MediaFrameSource selectedSource  = nullptr;
    MediaFrameFormat preferredFormat = nullptr;

    for (MediaFrameSourceInfo sourceInfo : selectedSourceInfos)
    {
        auto tmpSource = mediaCapture.FrameSources().Lookup(sourceInfo.Id());
        for (MediaFrameFormat format : tmpSource.SupportedFormats())
        {
            if (format.VideoFormat().Width() == kImageWidth)
            {
                selectedSource = tmpSource;
                preferredFormat = format;
                break;
            }
        }
    }

    winrt::check_bool(preferredFormat != nullptr);

    co_await selectedSource.SetFormatAsync(preferredFormat);
    m_mediaFrameReader = co_await mediaCapture.CreateFrameReaderAsync(selectedSource);
    auto status = co_await m_mediaFrameReader.StartAsync();

    winrt::check_bool(status == MediaFrameReaderStartStatus::Success);

    // reserve for 10 seconds at 30fps
    m_PVFrameLog.reserve(10 * 30);
    // m_pGrabThread = new std::thread(CameraGrabThread, this);

    m_OnFrameArrivedRegistration = m_mediaFrameReader.FrameArrived({ this, &VideoFrameProcessor::OnFrameArrived });
    
}

void VideoFrameProcessor::OnFrameArrived(const MediaFrameReader& sender, const MediaFrameArrivedEventArgs& args)
{
    if (MediaFrameReference frame = sender.TryAcquireLatestFrame())
    {   //
        std::lock_guard<std::shared_mutex> lock(m_frameMutex);
        m_latestFrame = frame;
        m_NbFrameArrived++; 
    }
}

uint32_t VideoFrameProcessor::GetNbFrameArrived(){ return m_NbFrameArrived;}
uint32_t VideoFrameProcessor::GetNbFrameConverted() { return m_NbFrameConverted; }
uint32_t VideoFrameProcessor::GetNbFrameCopyInContext() { return m_NbFrameCopyInContext; }
uint32_t VideoFrameProcessor::GetNbFrameCopyToClient() { return m_NbFrameCopyToClient; }

uint32_t VideoFrameProcessor::GetRGBByteArraySize()
{
    std::lock_guard<std::shared_mutex> lock( m_frameMutex );
    return m_RGBFrame.pixelBufferSize;
}

uint32_t VideoFrameProcessor::GetRGBByteArrayWidth()
{
    std::lock_guard<std::shared_mutex> lock( m_frameMutex );
    return m_RGBFrame.width;
}

uint32_t VideoFrameProcessor::GetRGBByteArrayHeight()
{
    std::lock_guard<std::shared_mutex> lock( m_frameMutex );
    return m_RGBFrame.height;
}

void VideoFrameProcessor::CopyLastFrame(PVFrame& to_RGBFrame) // 
{
    std::lock_guard<std::shared_mutex> lock( m_frameMutex );

    // if to_RGBFrame buffer has been allocated and shares the same size as m_RGBFrame
    // recopy m_RGBFrame datas
    if (to_RGBFrame.pixelBufferData != nullptr  && to_RGBFrame.pixelBufferSize == m_RGBFrame.pixelBufferSize)
    {
        // buffer copy
        std::memcpy(&(to_RGBFrame.pixelBufferData[0]), m_RGBFrame.pixelBufferData, m_RGBFrame.pixelBufferSize);
        // metadatas 
        to_RGBFrame.width              = m_RGBFrame.width;
        to_RGBFrame.height             = m_RGBFrame.height;
        to_RGBFrame.fx                 = m_RGBFrame.fx;
        to_RGBFrame.fy                 = m_RGBFrame.fy;
        to_RGBFrame.timestamp          = (uint64_t) m_RGBFrame.timestamp;
        to_RGBFrame.PVtoWorldtransform = m_RGBFrame.PVtoWorldtransform;

        m_NbFrameCopyToClient = m_NbFrameCopyToClient + 1;
        m_RGBByteArrayNewAvailable = false;
    }
}

bool VideoFrameProcessor::GetRGBByteArrayNewAvailable()
{
    return m_RGBByteArrayNewAvailable;
}

//void VideoFrameProcessor::AddLogFrame()
//{
//    // Lock on m_frameMutex from caller
//    PVFrame frame;
//
//    frame.timestamp = m_latestTimestamp;
//    frame.fx = m_latestFrame.VideoMediaFrame().CameraIntrinsics().FocalLength().x;
//    frame.fy = m_latestFrame.VideoMediaFrame().CameraIntrinsics().FocalLength().y;
//
//    auto PVtoWorld = m_latestFrame.CoordinateSystem().TryGetTransformTo(m_worldCoordSystem);
//    if (PVtoWorld)
//    {
//        frame.PVtoWorldtransform = PVtoWorld.Value();
//    }
//    m_PVFrameLog.push_back(std::move(frame));
//}

// This thread converts last grabbed frame and updates m_RGBFrame
void VideoFrameProcessor::CameraGrabThread(VideoFrameProcessor* pProcessor)
{
    while (!pProcessor->m_fExit)
    {
        std::lock_guard<std::shared_mutex> lock(pProcessor->m_frameMutex);
        if (pProcessor->m_latestFrame != nullptr)
        {
            auto frame = pProcessor->m_latestFrame;
            long long timestamp = pProcessor->m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds(frame.SystemRelativeTime().Value().count())).count();
            if (timestamp != pProcessor->m_latestTimestamp)
            {
                SoftwareBitmap softwareBitmap = SoftwareBitmap::Convert(frame.VideoMediaFrame().SoftwareBitmap(), BitmapPixelFormat::Bgra8);
                    
                if (softwareBitmap != nullptr)
                {
                    // Fill RGB Frame infos
                    pProcessor->m_latestTimestamp = timestamp;

                    pProcessor->m_RGBFrame.timestamp = timestamp;
                    pProcessor->m_RGBFrame.fx = frame.VideoMediaFrame().CameraIntrinsics().FocalLength().x;
                    pProcessor->m_RGBFrame.fy = frame.VideoMediaFrame().CameraIntrinsics().FocalLength().y;
                    pProcessor->m_RGBFrame.width = softwareBitmap.PixelWidth();
                    pProcessor->m_RGBFrame.height = softwareBitmap.PixelHeight();

                    //// use first pose as world reference coordinate system
                    //if ( !pProcessor->m_isWorldCoordSystemSet )
                    //{
                    //    if ( frame.CoordinateSystem() )
                    //    {
                    //    pProcessor->m_worldCoordSystem = frame.CoordinateSystem();
                    //    pProcessor->m_isWorldCoordSystemSet = true;
                    //    }
                    //}

                    assert( pProcessor->m_worldCoordSystem );

                    auto PVtoWorld = pProcessor->m_latestFrame.CoordinateSystem().TryGetTransformTo(pProcessor->m_worldCoordSystem);
                    if (PVtoWorld)
                    {
                        pProcessor->m_RGBFrame.PVtoWorldtransform = PVtoWorld.Value();
                    }
                    pProcessor->m_NbFrameConverted = pProcessor->m_NbFrameConverted + 1;

                    // Get image buffer data
                    {
                        // Get bitmap buffer object of the frame
                        BitmapBuffer bitmapBuffer = softwareBitmap.LockBuffer(BitmapBufferAccessMode::Read);

                        // Get raw pointer to the buffer object
                        uint32_t pixelBufferDataLength = 0;
                        uint8_t* pixelBufferData;

                        auto spMemoryBufferByteAccess{ bitmapBuffer.CreateReference().as<::Windows::Foundation::IMemoryBufferByteAccess>() };
                        winrt::check_hresult(spMemoryBufferByteAccess->GetBuffer(&pixelBufferData, &pixelBufferDataLength));

                        // check if buffer must be reallocated
                        if (pProcessor->m_RGBFrame.pixelBufferSize != pixelBufferDataLength)
                        {
                            if (pProcessor->m_RGBFrame.pixelBufferData != nullptr)
                            {
                                delete[] pProcessor->m_RGBFrame.pixelBufferData;
                                pProcessor->m_RGBFrame.pixelBufferData = nullptr;
                                pProcessor->m_RGBFrame.pixelBufferSize = 0;
                            }
                            pProcessor->m_RGBFrame.pixelBufferData = new uint8_t[pixelBufferDataLength];
                            pProcessor->m_RGBFrame.pixelBufferSize = pixelBufferDataLength;
                        }

                        // 
                        std::memcpy(pProcessor->m_RGBFrame.pixelBufferData, &pixelBufferData[0], pixelBufferDataLength);

                        pProcessor->m_NbFrameCopyInContext = pProcessor->m_NbFrameCopyInContext + 1;
                        pProcessor->m_RGBByteArrayNewAvailable = true;
                    }

                    {
                        std::lock_guard<std::mutex> guard( pProcessor->m_storageMutex );
                        // Recording ?
                        if ( pProcessor->m_storageFolder != nullptr )
                        {
                        // TODO(jmhenaff): Warning this version of PVFrame added image buffer
                        // (compared to the sample version). So only original info must be used
                        // (timestamp, fx/fy, PVToWorld matrix) Add another structure only
                        // containing these info and add it to PVFrame to replace them ?
                        pProcessor->m_PVFrameLog.push_back( pProcessor->m_RGBFrame );

                        // Convert and write the bitmap
                        pProcessor->DumpFrame( softwareBitmap, pProcessor->m_latestTimestamp );
                        }
                    }
                }
            }
        }
        
    }
}

void VideoFrameProcessor::Clear()
{
    std::lock_guard<std::shared_mutex> lock(m_frameMutex);
    m_PVFrameLog.clear();
}

void VideoFrameProcessor::DumpFrame(const SoftwareBitmap& softwareBitmap, long long timestamp)
{
    // Compose the output file name
    wchar_t bitmapPath[MAX_PATH];
    swprintf_s(bitmapPath, L"%lld.%s", timestamp, L"bytes");

    // Get bitmap buffer object of the frame
    BitmapBuffer bitmapBuffer = softwareBitmap.LockBuffer(BitmapBufferAccessMode::Read);

    // Get raw pointer to the buffer object
    uint32_t pixelBufferDataLength = 0;
    uint8_t* pixelBufferData;

    auto spMemoryBufferByteAccess{ bitmapBuffer.CreateReference().as<::Windows::Foundation::IMemoryBufferByteAccess>() };
    winrt::check_hresult(spMemoryBufferByteAccess->GetBuffer(&pixelBufferData, &pixelBufferDataLength));

    m_tarball->AddFile(bitmapPath, &pixelBufferData[0], pixelBufferDataLength);
}

bool VideoFrameProcessor::DumpDataToDisk( const StorageFolder& folder,
                                          const std::wstring& datetime_path )
{
  auto path = folder.Path().data();
  std::wstring fullName( path );
  fullName += L"\\" + datetime_path + L"_pv.txt";
  std::ofstream file( fullName );
  if ( !file )
  {
    return false;
  }

  std::lock_guard<std::shared_mutex> lock( m_frameMutex );
  // assuming this is called at the end of the capture session, and m_latestFrame is not nullptr
  assert( m_latestFrame != nullptr );
  file << m_latestFrame.VideoMediaFrame().CameraIntrinsics().PrincipalPoint().x << ","
       << m_latestFrame.VideoMediaFrame().CameraIntrinsics().PrincipalPoint().y << ","
       << m_latestFrame.VideoMediaFrame().CameraIntrinsics().ImageWidth() << ","
       << m_latestFrame.VideoMediaFrame().CameraIntrinsics().ImageHeight() << "\n";

  for ( const PVFrame& frame : m_PVFrameLog )
  {
    file << frame.timestamp << ",";
    file << frame.fx << "," << frame.fy << ",";
    file << frame.PVtoWorldtransform.m11 << "," << frame.PVtoWorldtransform.m21 << ","
         << frame.PVtoWorldtransform.m31 << "," << frame.PVtoWorldtransform.m41 << ","
         << frame.PVtoWorldtransform.m12 << "," << frame.PVtoWorldtransform.m22 << ","
         << frame.PVtoWorldtransform.m32 << "," << frame.PVtoWorldtransform.m42 << ","
         << frame.PVtoWorldtransform.m13 << "," << frame.PVtoWorldtransform.m23 << ","
         << frame.PVtoWorldtransform.m33 << "," << frame.PVtoWorldtransform.m43 << ","
         << frame.PVtoWorldtransform.m14 << "," << frame.PVtoWorldtransform.m24 << ","
         << frame.PVtoWorldtransform.m34 << "," << frame.PVtoWorldtransform.m44;
    file << "\n";
  }
  file.close();
  return true;
}

void VideoFrameProcessor::StartRecording(const StorageFolder& storageFolder, const SpatialCoordinateSystem& worldCoordSystem)
{
    m_worldCoordSystem = worldCoordSystem;
    m_isWorldCoordSystemSet = true;

    // Recording
    if (storageFolder)
    {
        std::lock_guard<std::mutex> guard(m_storageMutex);
        m_storageFolder = storageFolder;

        // Create the tarball for the image files
        wchar_t fileName[MAX_PATH] = {};
        swprintf_s(fileName, L"%s\\%s.tar", m_storageFolder.Path().data(), kSensorName);
        m_tarball.reset(new Io::Tarball(fileName));
    }

    // Grab thread control
    if (m_pGrabThread)
    {
        // Already running
        return;
    }
    //auto status = co_await m_mediaFrameReader.StartAsync();
    //winrt::check_bool(status == MediaFrameReaderStartStatus::Success);
    m_fExit = false;
    m_pGrabThread = new std::thread(CameraGrabThread, this);

}

void VideoFrameProcessor::StopRecording()
{
    m_fExit = true;

    if (m_pGrabThread && m_pGrabThread->joinable())
    {
        m_pGrabThread->join();
    }
    m_pGrabThread = nullptr;

    if (m_storageFolder)
    {
        std::lock_guard<std::mutex> guard(m_storageMutex);
        m_tarball.reset();
        m_storageFolder = nullptr;
    }
}

//void VideoFrameProcessor::StartRGBSensorCapture()
//{
//    // Already running ?
//    if (m_pGrabThread)
//    {
//        return;
//    }
//    //auto status = co_await m_mediaFrameReader.StartAsync();
//    //winrt::check_bool(status == MediaFrameReaderStartStatus::Success);
//    m_fExit = false;
//    m_pGrabThread = new std::thread(CameraGrabThread, this);
//}
//
//void VideoFrameProcessor::StopRGBSensorCapture()
//{
//    m_fExit = true;
//    if (m_pGrabThread && m_pGrabThread->joinable())
//    {
//        m_pGrabThread->join();
//    }
//    m_pGrabThread = nullptr;
//    //co_await m_mediaFrameReader.StopAsync();
//    //m_mediaFrameReader.FrameArrived(nullptr);
//}