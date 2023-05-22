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
#include "SolARHololens2ResearchMode.h"
#include "SolARHololens2ResearchMode.g.cpp"
#include "Utils.h"

#include <winrt/Windows.Foundation.h>
#include <ctime>

#include <winrt/Windows.ApplicationModel.Core.h>

extern "C"
HMODULE LoadLibraryA(
    LPCSTR lpLibFileName
);

using namespace bcom::hololensdemo;

using namespace DirectX;
using namespace std;
using namespace winrt::Windows::Perception;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Perception::Spatial::Preview;
using namespace winrt::Windows::Storage;

typedef std::chrono::duration<int64_t, std::ratio<1, 10'000'000>> HundredsOfNanoseconds;


// TODO(jmhenaff): manage state machine for start/stop (enabled/disabled) ?

namespace winrt::SolARHololens2UnityPlugin::implementation
{

    void SolARHololens2ResearchMode::SetSpatialCoordinateSystem( Windows::Perception::Spatial::SpatialCoordinateSystem unitySpatialCoodinateSystem)
    {
        m_UnitySpatialCoordinateSystem = unitySpatialCoodinateSystem;
    }

    void SolARHololens2ResearchMode::Init()
    {
        m_recording = false;
        m_qrCodeValue = "";

        // MixedReality has affinity with UI Thread
        winrt::Windows::ApplicationModel::Core::CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(
            Windows::UI::Core::CoreDispatcherPriority::Normal,
                [&]()
            {
                DrawCall::Initialize();
                m_mixedReality.EnableMixedReality();
                m_mixedReality.EnableSurfaceMapping();
            }).get();

        if ( !m_mixedReality.IsEnabled() )
        {
            // TODO(jmhenaff): add bool return type to Init() so that error can be handled in Unity C# scripts
            std::cout << "SolARHololens2UnityPlugin::Init(): Error while initializing MixedReality util" << std::endl;
            throw std::runtime_error("SolARHololens2UnityPlugin::Init(): Error while initializing MixedReality util");
            return;
        }

        if (kEnabledRMStreamTypes.size() > 0)
        {
            // Enable SensorScenario for RM
            m_sensorScenario = std::make_unique<SensorScenario>(kEnabledRMStreamTypes);
            m_sensorScenario->InitializeSensors();
            m_sensorScenario->InitializeCameraReaders();
        }

        for (int i = 0; i < kEnabledStreamTypes.size(); ++i)
        {
            if (kEnabledStreamTypes[i] == StreamTypes::PV)
            {
                m_videoFrameProcessorOperation = InitializeVideoFrameProcessorAsync();
            }
            else if (kEnabledStreamTypes[i] == StreamTypes::EYE)
            {
                // m_mixedReality.EnableEyeTracking();
            }
        }        
    }

    void SolARHololens2ResearchMode::EnablePv()
    {
      if ( std::find( kEnabledStreamTypes.begin(), kEnabledStreamTypes.end(), StreamTypes::PV ) ==
           kEnabledStreamTypes.end() )
      {
        kEnabledStreamTypes.push_back( StreamTypes::PV );
      }
    }

    void SolARHololens2ResearchMode::EnableVlc( RMSensorType sensorType )
    {
      auto hLType = toHololensRMSensorType( sensorType );
      if ( std::find( kEnabledRMStreamTypes.begin(), kEnabledRMStreamTypes.end(), hLType ) ==
           kEnabledRMStreamTypes.end() )
      {
        kEnabledRMStreamTypes.push_back( hLType );
      }
    }

    void SolARHololens2ResearchMode::EnableDepth( bool isLongThrow )
    {
      auto depthType = isLongThrow ? ResearchModeSensorType::DEPTH_LONG_THROW
                                   : ResearchModeSensorType::DEPTH_AHAT;
      if ( std::find( kEnabledRMStreamTypes.begin(), kEnabledRMStreamTypes.end(), depthType ) ==
           kEnabledRMStreamTypes.end() )
      {
        kEnabledRMStreamTypes.push_back( depthType );
      }
    }

    bool SolARHololens2ResearchMode::EnableRecording()
    {
      // Code is mostly present but not tested and likely not working
      throw std::runtime_error( "Not yet implemented" );
      if ( m_is_running )
      {
        return false;
      }
      m_enable_recording = true;
    }

    // TODO(jmhenaff): think about only keeping enable recording, since set to false by init()
    bool SolARHololens2ResearchMode::DisableRecording()
    {
      if ( m_is_running )
      {
        return false;
      }
      m_enable_recording = false;
    }

    bool SolARHololens2ResearchMode::IsRecordingEnabled()
    {
        return m_enable_recording;
    }

    void SolARHololens2ResearchMode::Update()
    {
      if ( m_mixedReality.IsEnabled() )
      {
        m_mixedReality.Update();
      }
            

        //// Look for a QR code in the scene and get its transformation, if we didn't find one yet;
        //// but do not change the world coordinate system if we're recording already.
        //// Note that, with the current implementation, after finding a QRCode we'll not look
        //// for new codes anymore. This behavior should be changed if it's not the expected one
        //if (!m_recording && !IsQRCodeDetected())
        //{
        //    std::vector<QRCode> qrcodes = m_mixedReality.GetTrackedQRCodeList();
        //    if (!qrcodes.empty())
        //    {
        //        m_qrCodeValue = qrcodes[0].value;
        //        m_qrCodeTransform = qrcodes[0].worldTransform;
        //    }
        //}
    }

    void SolARHololens2ResearchMode::Start()
    {
        m_is_running = true;
        SetDateTimePath();
        StartRecordingAsync();
    }

    void SolARHololens2ResearchMode::Stop()
    {
        if (m_videoFrameProcessor)
        {
            m_videoFrameProcessor->StopRecording();
            if ( m_archiveFolder )
            {
                m_videoFrameProcessor->DumpDataToDisk( m_archiveFolder, m_datetime );
            }
           
        }
        if (m_sensorScenario)
        {
            m_sensorScenario->StopRecording();
        }

        m_recording = false;

        if ( m_archiveFolder )
        {
          winrt::hstring archiveName{ m_datetime.c_str() };
          m_archiveFolder.RenameAsync( archiveName );
        }

        m_archiveFolder = nullptr;

        m_is_running = false;
    }

    boolean SolARHololens2ResearchMode::IsRunning()
    {
        return m_is_running;
    }

    bool SolARHololens2ResearchMode::SetDateTimePath()
    {
        wchar_t m_datetime_c[200];
        const std::time_t now = std::time(nullptr);
        std::tm tm;
        if (localtime_s(&tm, &now))
        {
            return false;
        }
        if (!std::wcsftime(m_datetime_c, sizeof(m_datetime_c), L"%F-%H%M%S", &tm))
        {
            return false;
        }
        m_datetime.assign(m_datetime_c);
        return true;
    }

    // Set streams to capture
    winrt::Windows::Foundation::IAsyncAction SolARHololens2ResearchMode::InitializeVideoFrameProcessorAsync()
    {
        // PV/RGB sensor capture
        if (m_videoFrameProcessorOperation &&
            m_videoFrameProcessorOperation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
        {
            return;
        }
        m_videoFrameProcessor = std::make_unique<VideoFrameProcessor>();
        if (!m_videoFrameProcessor.get())
        {
            throw winrt::hresult(E_POINTER);
        }
        co_await m_videoFrameProcessor->InitializeAsync();
    }

    winrt::Windows::Foundation::IAsyncAction SolARHololens2ResearchMode::StartRecordingAsync()
    {
        m_archiveFolder = nullptr;

        if (m_enable_recording)
        {
            StorageFolder localFolder = ApplicationData::Current().LocalFolder();
            auto archiveSourceFolder = co_await localFolder.CreateFolderAsync(
                L"archiveSource",
                CreationCollisionOption::ReplaceExisting);
            if (archiveSourceFolder)
            {
                m_archiveFolder = archiveSourceFolder;
            }
            m_recording = true;
        }

        auto worldCoordinate = m_UnitySpatialCoordinateSystem; // m_mixedReality.GetWorldCoordinateSystem();

        if (m_sensorScenario)
        {
            // TODO(jmhenaff): remove reference to mixedReality
            m_sensorScenario->StartRecording(m_archiveFolder, worldCoordinate );
        }
        if (m_videoFrameProcessor)
        {
            m_videoFrameProcessor->Clear();
           // TODO(jmhenaff): remove reference to mixedReality
            m_videoFrameProcessor->StartRecording(m_archiveFolder, worldCoordinate );
        }
    }


    //void SolARHololens2ResearchMode::InitializeRGBSensor()
    //{
    //    m_videoFrameProcessorOperation = InitializeVideoFrameProcessorAsync();
    //}

    //void SolARHololens2ResearchMode::StartRGBSensorCapture()
    //{
    //    m_videoFrameProcessor->StartRGBSensorCapture();
    //}
    //
    //void SolARHololens2ResearchMode::StopRGBSensorCapture()
    //{
    //    m_videoFrameProcessor->StopRGBSensorCapture();
    //}

    uint32_t SolARHololens2ResearchMode::GetPvWidth()
    {
        uint32_t width  =  m_videoFrameProcessor->GetRGBByteArrayWidth();
        return width; 
    }

    uint32_t SolARHololens2ResearchMode::GetPvHeight()
    {
        uint32_t height = m_videoFrameProcessor->GetRGBByteArrayHeight();
        return height;
    }

    // Get RGB synchronized texture buffer and metadatas
    com_array<uint8_t> SolARHololens2ResearchMode::GetPvData(uint64_t& timestamp,
                                                                    com_array<double>& PVtoWorldtransform, 
                                                                    float& fx,
                                                                    float& fy,
                                                                    uint32_t& pixelBufferSize,
                                                                    uint32_t& width,
                                                                    uint32_t& height,
                                                                    bool flip)
    {
      m_RGBTextureUpdated = m_videoFrameProcessor->GetRGBByteArrayNewAvailable();

      // return null
      if ( !m_RGBTextureUpdated )
      {
        return com_array<UINT8>();
      }

      // re allocation if data size changes
      auto rgbByteArraySize = m_videoFrameProcessor->GetRGBByteArraySize();
      if ( m_RGBFrame.pixelBufferSize != rgbByteArraySize )
      {
        if ( m_RGBFrame.pixelBufferData != nullptr )
        {
          delete[] m_RGBFrame.pixelBufferData;
          m_RGBFrame.pixelBufferData = nullptr;
          m_RGBFrame.pixelBufferSize = 0;
        }
        m_RGBFrame.pixelBufferSize = rgbByteArraySize;
        m_RGBFrame.pixelBufferData = new UINT8[m_RGBFrame.pixelBufferSize];
      }

      if ( m_RGBFrame.pixelBufferData != nullptr && m_RGBFrame.pixelBufferSize > 0 )
      {
        // all datas are copied simultaneously - metadatas (as timestamp) and RGB pixels buffer
        m_videoFrameProcessor->CopyLastFrame( m_RGBFrame );
      }

      com_array<UINT8> tempBuffer;
      if ( !flip )
      {
        tempBuffer = com_array<UINT8>(
            std::move_iterator( m_RGBFrame.pixelBufferData ),
            std::move_iterator( m_RGBFrame.pixelBufferData + m_RGBFrame.pixelBufferSize ) );
      }
      else
      {
        tempBuffer = com_array<UINT8>( rgbByteArraySize );
        for ( int x = 0; x < m_RGBFrame.width; x++ )
        {
          for ( int y = 0; y < m_RGBFrame.height; y++ )
          {
            // BGRA 32
            int destIndex = ( x * 4 ) + y * ( m_RGBFrame.width * 4 );
            int srcIndex = ( x * 4 ) + ( m_RGBFrame.height - 1 - y ) * ( m_RGBFrame.width * 4 );

            tempBuffer[destIndex] = m_RGBFrame.pixelBufferData[srcIndex];
            tempBuffer[destIndex + 1] = m_RGBFrame.pixelBufferData[srcIndex + 1];
            tempBuffer[destIndex + 2] = m_RGBFrame.pixelBufferData[srcIndex + 2];
            tempBuffer[destIndex + 3] = m_RGBFrame.pixelBufferData[srcIndex + 3];
          }
        }
      }

      //
      std::array<double, 16> PVtoWorldtransform_values;
      PVtoWorldtransform_values[0] = m_RGBFrame.PVtoWorldtransform.m11;
      PVtoWorldtransform_values[1] = m_RGBFrame.PVtoWorldtransform.m12;
      PVtoWorldtransform_values[2] = m_RGBFrame.PVtoWorldtransform.m13;
      PVtoWorldtransform_values[3] = m_RGBFrame.PVtoWorldtransform.m14;
      PVtoWorldtransform_values[4] = m_RGBFrame.PVtoWorldtransform.m21;
      PVtoWorldtransform_values[5] = m_RGBFrame.PVtoWorldtransform.m22;
      PVtoWorldtransform_values[6] = m_RGBFrame.PVtoWorldtransform.m23;
      PVtoWorldtransform_values[7] = m_RGBFrame.PVtoWorldtransform.m24;
      PVtoWorldtransform_values[8] = m_RGBFrame.PVtoWorldtransform.m31;
      PVtoWorldtransform_values[9] = m_RGBFrame.PVtoWorldtransform.m32;
      PVtoWorldtransform_values[10] = m_RGBFrame.PVtoWorldtransform.m33;
      PVtoWorldtransform_values[11] = m_RGBFrame.PVtoWorldtransform.m34;
      PVtoWorldtransform_values[12] = m_RGBFrame.PVtoWorldtransform.m41;
      PVtoWorldtransform_values[13] = m_RGBFrame.PVtoWorldtransform.m42;
      PVtoWorldtransform_values[14] = m_RGBFrame.PVtoWorldtransform.m43;
      PVtoWorldtransform_values[15] = m_RGBFrame.PVtoWorldtransform.m44;

      std::array<double, 16> PVtoWorldtransformSolar_values;
      PVtoWorldtransformSolar_values.fill( 0 );
      Utils::convertToSolARPose( PVtoWorldtransform_values, PVtoWorldtransformSolar_values );

      // PVtoWorldtransform = com_array<double>(std::move_iterator(PVtoWorldtransform_values),
      // std::move_iterator(PVtoWorldtransform_values + 16*sizeof(double)));
      PVtoWorldtransform = com_array<double>( PVtoWorldtransformSolar_values.begin(),
                                              PVtoWorldtransformSolar_values.end() );
      timestamp = m_RGBFrame.timestamp;
      fx = m_RGBFrame.fx;
      fy = m_RGBFrame.fy;
      pixelBufferSize = m_RGBFrame.pixelBufferSize;
      width = m_RGBFrame.width;
      height = m_RGBFrame.height;
      pixelBufferSize = m_RGBFrame.pixelBufferSize;

      return tempBuffer;
    }

    void SolARHololens2ResearchMode::InitializeRMSensors()
    {
        m_sensorScenario->InitializeSensors();
        m_sensorScenario->InitializeCameraReaders();
    }

    bool SolARHololens2ResearchMode::StartRMSensor(RMSensorType sensor)
    {
        return m_sensorScenario->m_cameraReaders[toHololensRMSensorType(sensor)]->start();
    }

    void SolARHololens2ResearchMode::StopRMSensor(RMSensorType sensor)
    {
        return m_sensorScenario->m_cameraReaders[toHololensRMSensorType(sensor)]->stop();
    }

    uint32_t SolARHololens2ResearchMode::GetVlcWidth( RMSensorType sensor )
    {
      // TODO(jmhenaff): make m_sensorScenario->m_cameraReaders private, and make this check
      // in a m_sensorScenario method (e.g. getVlcSensorData(sensor, params....)
      if ( m_sensorScenario->m_cameraReaders.find( toHololensRMSensorType( sensor ) ) ==
           m_sensorScenario->m_cameraReaders.end() )
      {
        return 0;
      }
      return m_sensorScenario->m_cameraReaders[toHololensRMSensorType( sensor )]->getWidth();
    }

    uint32_t SolARHololens2ResearchMode::GetVlcHeight( const RMSensorType sensor )
    {
      // TODO(jmhenaff): make m_sensorScenario->m_cameraReaders private, and make this check
      // in a m_sensorScenario method (e.g. getVlcSensorData(sensor, params....)
      if ( m_sensorScenario->m_cameraReaders.find( toHololensRMSensorType( sensor ) ) ==
           m_sensorScenario->m_cameraReaders.end() )
      {
        return 0;
      }
      return m_sensorScenario->m_cameraReaders[toHololensRMSensorType( sensor )]->getHeight();
    }

    bool SolARHololens2ResearchMode::ComputeIntrinsics(
        const RMSensorType sensor,
        float& fx,
        float& fy,
        float& cx,
        float& cy,
        float& avgReprojErr )
    {
        return m_sensorScenario->m_cameraReaders[toHololensRMSensorType( sensor )]->computeIntrinsics(fx, fy, cx, cy, avgReprojErr);
    }

    com_array<uint8_t> SolARHololens2ResearchMode::GetVlcData(
        const RMSensorType sensor,
        uint64_t& timestamp,
        com_array<double>& PVtoWorldtransform,
        float& fx,
        float& fy,
        uint32_t& pixelBufferSize,
        uint32_t& width,
        uint32_t& height,
        bool flip)
    {
        // TODO(jmhenaff): make m_sensorScenario->m_cameraReaders private, and make this check
        // in a m_sensorScenario method (e.g. getVlcSensorData(sensor, params....)
      if ( m_sensorScenario->m_cameraReaders.find( toHololensRMSensorType( sensor ) ) ==
           m_sensorScenario->m_cameraReaders.end() )
      {
        return com_array<uint8_t>();
      }

      return m_sensorScenario->m_cameraReaders[toHololensRMSensorType( sensor )]->getVlcSensorData(
          timestamp, PVtoWorldtransform, pixelBufferSize, width, height, flip );
    }

    uint32_t SolARHololens2ResearchMode::GetDepthWidth()
    {
      if ( !m_sensorScenario->m_depthCameraReader )
      {
        return 0;
      }
      return m_sensorScenario->m_depthCameraReader->getWidth();
    }

    uint32_t SolARHololens2ResearchMode::GetDepthHeight()
    {
      if ( !m_sensorScenario->m_depthCameraReader )
      {
        return 0;
      }
      return m_sensorScenario->m_depthCameraReader->getHeight();
    }

    com_array<uint16_t> SolARHololens2ResearchMode::GetDepthData(
        uint64_t& timestamp,
        com_array<double>& PVtoWorldtransform,
        float& fx,
        float& fy,
        uint32_t& pixelBufferSize,
        uint32_t& width,
        uint32_t& height )
    {
      if ( !m_sensorScenario->m_depthCameraReader )
      {
        return com_array<uint16_t>();
      }
      return m_sensorScenario->m_depthCameraReader->getDepthSensorData(
          timestamp, PVtoWorldtransform, pixelBufferSize, width, height );
    }

    ResearchModeSensorType SolARHololens2ResearchMode::toHololensRMSensorType( RMSensorType sType )
    {
        switch ( sType )
        {
        case RMSensorType::LEFT_FRONT:
            return ResearchModeSensorType::LEFT_FRONT;
        case RMSensorType::LEFT_LEFT:
            return ResearchModeSensorType::LEFT_LEFT;
        case RMSensorType::RIGHT_FRONT:
            return ResearchModeSensorType::RIGHT_FRONT;
        case RMSensorType::RIGHT_RIGHT:
            return ResearchModeSensorType::RIGHT_RIGHT;
        //case RMSensorType::DEPTH_AHAT:
        //    return ResearchModeSensorType::DEPTH_AHAT;
        //case RMSensorType::DEPTH_LONG_THROW:
            return ResearchModeSensorType::DEPTH_LONG_THROW;
        default:
            throw std::runtime_error( "Unknown SensorType" );
        }
    }
    }
