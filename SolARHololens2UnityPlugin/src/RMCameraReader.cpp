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

//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "RMCameraReader.h"
#include "Utils.h"

#include <sstream>
#include <iostream>

using namespace bcom::hololensdemo;

using namespace winrt::Windows::Perception;
using namespace winrt::Windows::Perception::Spatial;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Storage;

using winrt::com_array;

namespace Depth
{
    enum InvalidationMasks
    {
        Invalid = 0x80,
    };
    static constexpr UINT16 AHAT_INVALID_VALUE = 4090;
}

bool RMCameraReader::start()
{
    if (m_pCameraUpdateThread || m_pWriteThread)
    {
        // Already started, or not stopped yet
        return false;
    }

    if (!m_pCameraUpdateThread)
    {
        m_fExit = false;
		m_pCameraUpdateThread = std::make_unique<std::thread>(CameraUpdateThread,
                                                                this,
                                                                m_camConsentGiven,
                                                                m_camAccessConsent);
    }

    if (m_storageFolder && !m_pWriteThread)
    {
        m_pWriteThread = std::make_unique<std::thread>(CameraWriteThread, this);
    }

    return true;
}

void RMCameraReader::stop()
{
    if (m_pCameraUpdateThread)
    {
        m_fExit = true;
        m_pCameraUpdateThread->join();
        m_pCameraUpdateThread = nullptr;
    }

    if (m_pWriteThread)
    {
        m_pWriteThread->join();
        m_pWriteThread = nullptr;
    }
}

void RMCameraReader::CameraUpdateThread(RMCameraReader* pCameraReader, HANDLE camConsentGiven, ResearchModeSensorConsent* camAccessConsent)
{
	HRESULT hr = S_OK;

    DWORD waitResult = WaitForSingleObject(camConsentGiven, INFINITE);

    if (waitResult == WAIT_OBJECT_0)
    {
        switch (*camAccessConsent)
        {
        case ResearchModeSensorConsent::Allowed:
            OutputDebugString(L"Access is granted");
            break;
        case ResearchModeSensorConsent::DeniedBySystem:
            OutputDebugString(L"Access is denied by the system");
            hr = E_ACCESSDENIED;
            break;
        case ResearchModeSensorConsent::DeniedByUser:
            OutputDebugString(L"Access is denied by the user");
            hr = E_ACCESSDENIED;
            break;
        case ResearchModeSensorConsent::NotDeclaredByApp:
            OutputDebugString(L"Capability is not declared in the app manifest");
            hr = E_ACCESSDENIED;
            break;
        case ResearchModeSensorConsent::UserPromptRequired:
            OutputDebugString(L"Capability user prompt required");
            hr = E_ACCESSDENIED;
            break;
        default:
            OutputDebugString(L"Access is denied by the system");
            hr = E_ACCESSDENIED;
            break;
        }
    }
    else
    {
        hr = E_UNEXPECTED;
    }

    if (SUCCEEDED(hr))
    {
        hr = pCameraReader->m_pRMSensor->OpenStream();

        if (FAILED(hr))
        {
            pCameraReader->m_pRMSensor->Release();
            pCameraReader->m_pRMSensor = nullptr;
        }

        while (!pCameraReader->m_fExit && pCameraReader->m_pRMSensor)
        {
            HRESULT hr = S_OK;
            IResearchModeSensorFrame* pSensorFrame = nullptr;

            hr = pCameraReader->m_pRMSensor->GetNextBuffer(&pSensorFrame);

            if (SUCCEEDED(hr))
            {
                std::lock_guard<std::mutex> guard(pCameraReader->m_sensorFrameMutex);
                if (pCameraReader->m_pSensorFrame)
                {
                    pCameraReader->m_pSensorFrame->Release();
                }
                pCameraReader->m_pSensorFrame = pSensorFrame;
                pCameraReader->updateFrameLocation();
            }
        }

        pCameraReader->m_worldCoordSystem = nullptr;

        if (pCameraReader->m_pRMSensor)
        {
            pCameraReader->m_pRMSensor->CloseStream();
        }
    }
}

void RMCameraReader::CameraWriteThread(RMCameraReader* pReader)
{
    while (!pReader->m_fExit)
    {
        std::unique_lock<std::mutex> storage_lock(pReader->m_storageMutex);
        assert(pReader->m_storageFolder);
        //if (pReader->m_storageFolder == nullptr)
        //{
        //    pReader->m_storageCondVar.wait(storage_lock);
        //}
        
        std::lock_guard<std::mutex> reader_guard(pReader->m_sensorFrameMutex);
        if (pReader->m_pSensorFrame)
        {
            if (pReader->IsNewTimestamp(pReader->m_pSensorFrame))
            {
                pReader->SaveFrame(pReader->m_pSensorFrame);
            }
        }       
    }	
}

void RMCameraReader::DumpCalibration()
{   
    // Get frame resolution (could also be stored once at the beginning of the capture)
    ResearchModeSensorResolution resolution;
    {
        std::lock_guard<std::mutex> guard(m_sensorFrameMutex);
        // Assuming we are at the end of the capture
        assert(m_pSensorFrame != nullptr);
        winrt::check_hresult(m_pSensorFrame->GetResolution(&resolution)); 
    }

    // Get camera sensor object
    IResearchModeCameraSensor* pCameraSensor = nullptr;    
    HRESULT hr = m_pRMSensor->QueryInterface(IID_PPV_ARGS(&pCameraSensor));
    winrt::check_hresult(hr);

    // Get extrinsics (rotation and translation) with respect to the rigNode
    wchar_t outputExtrinsicsPath[MAX_PATH] = {};
    swprintf_s(outputExtrinsicsPath, L"%s\\%s_extrinsics.txt", m_storageFolder.Path().data(), m_pRMSensor->GetFriendlyName());

    std::ofstream fileExtrinsics(outputExtrinsicsPath);
    DirectX::XMFLOAT4X4 cameraViewMatrix;

    pCameraSensor->GetCameraExtrinsicsMatrix(&cameraViewMatrix);

    fileExtrinsics << cameraViewMatrix.m[0][0] << "," << cameraViewMatrix.m[1][0] << "," << cameraViewMatrix.m[2][0] << "," << cameraViewMatrix.m[3][0] << "," 
                   << cameraViewMatrix.m[0][1] << "," << cameraViewMatrix.m[1][1] << "," << cameraViewMatrix.m[2][1] << "," << cameraViewMatrix.m[3][1] << "," 
                   << cameraViewMatrix.m[0][2] << "," << cameraViewMatrix.m[1][2] << "," << cameraViewMatrix.m[2][2] << "," << cameraViewMatrix.m[3][2] << "," 
                   << cameraViewMatrix.m[0][3] << "," << cameraViewMatrix.m[1][3] << "," << cameraViewMatrix.m[2][3] << "," << cameraViewMatrix.m[3][3] << "\n";
    
    fileExtrinsics.close();

    wchar_t outputPath[MAX_PATH] = {};    
    swprintf_s(outputPath, L"%s\\%s_lut.bin", m_storageFolder.Path().data(), m_pRMSensor->GetFriendlyName());
    
    float uv[2];
    float xy[2];
    std::vector<float> lutTable(size_t(resolution.Width * resolution.Height) * 3);
    auto pLutTable = lutTable.data();

    for (size_t y = 0; y < resolution.Height; y++)
    {
        uv[1] = (y + 0.5f);
        for (size_t x = 0; x < resolution.Width; x++)
        {
            uv[0] = (x + 0.5f);
            hr = pCameraSensor->MapImagePointToCameraUnitPlane(uv, xy);
            if (FAILED(hr))
            {
				*pLutTable++ = xy[0];
				*pLutTable++ = xy[1];
				*pLutTable++ = 0.f;
                continue;
            }
            float z = 1.0f;
            const float norm = sqrtf(xy[0] * xy[0] + xy[1] * xy[1] + z * z);
            const float invNorm = 1.0f / norm;
            xy[0] *= invNorm;
            xy[1] *= invNorm;
            z *= invNorm;

            // Dump LUT row
            *pLutTable++ = xy[0];
            *pLutTable++ = xy[1];
            *pLutTable++ = z;
        }
    }    
    pCameraSensor->Release();

    // Save binary LUT to disk
    std::ofstream file(outputPath, std::ios::out | std::ios::binary);
	file.write(reinterpret_cast<char*> (lutTable.data()), lutTable.size() * sizeof(float));
    file.close();
}

void RMCameraReader::DumpFrameLocations()
{
    wchar_t outputPath[MAX_PATH] = {};
    swprintf_s(outputPath, L"%s\\%s_rig2world.txt", m_storageFolder.Path().data(), m_pRMSensor->GetFriendlyName());
    
    std::ofstream file(outputPath);
    for (const FrameLocation& location : m_frameLocations)
    {
        file << location.timestamp << "," <<
            location.rigToWorldtransform.m11 << "," << location.rigToWorldtransform.m21 << "," << location.rigToWorldtransform.m31 << "," << location.rigToWorldtransform.m41 << "," <<
            location.rigToWorldtransform.m12 << "," << location.rigToWorldtransform.m22 << "," << location.rigToWorldtransform.m32 << "," << location.rigToWorldtransform.m42 << "," <<
            location.rigToWorldtransform.m13 << "," << location.rigToWorldtransform.m23 << "," << location.rigToWorldtransform.m33 << "," << location.rigToWorldtransform.m43 << "," <<
            location.rigToWorldtransform.m14 << "," << location.rigToWorldtransform.m24 << "," << location.rigToWorldtransform.m34 << "," << location.rigToWorldtransform.m44 << std::endl;
    }
    file.close();

    m_frameLocations.clear();
}

winrt::com_array<uint8_t> RMCameraReader::getVlcSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height, bool flip,
    winrt::Windows::Perception::Spatial::SpatialCoordinateSystem unitySpatialCoordinateSytem)
{
    std::lock_guard<std::mutex> reader_guard(m_sensorFrameMutex);
    if ( m_pSensorFrame && IsNewTimestamp( m_pSensorFrame ) )
    {
        timestamp = static_cast<uint64_t>(m_prevTimestamp);

        // TODO(jmhenaff): convert frame
        // Copy last frame, release mutex and process afterwards ?

        IResearchModeSensorVLCFrame* pVLCFrame = nullptr;
        auto hr = m_pSensorFrame->QueryInterface(IID_PPV_ARGS(&pVLCFrame));
        if (FAILED(hr))
        {
            // TODO(jmhenaff): handle error, depth frame
            throw std::runtime_error("Cannot call 'getVlcSensorData()' on a camera reader assigned to a non-VLC sensor");
            // return winrt::com_array<uint8_t>();
        }

        //// TODO(jmhenaff): do something with this?
        //UINT32 gain = 0;
        //UINT64 exposure = 0;
        //pVLCFrame->GetGain(&gain);
        //pVLCFrame->GetExposure(&exposure);

        ResearchModeSensorResolution resolution;
        winrt::check_hresult(m_pSensorFrame->GetResolution(&resolution));

        // Get data buffer (8 bits grayscale)
        const BYTE* pImage = nullptr;
        size_t outBufferCount = 0;
        winrt::check_hresult(pVLCFrame->GetBuffer(&pImage, &outBufferCount));
        pVLCFrame->Release();

        assert(outBufferCount == resolution.Width * resolution.Height);

        // Solution 1
        //// Convert to grayscale RGB8 (32 bits)
        //com_array<UINT8> tempBuffer(resolution.Width * resolution.Height * 3);
        //for (UINT y = 0; y < resolution.Height; ++y)
        //{
        //    for (UINT x = 0; x < resolution.Width; ++x)
        //    {
        //        BYTE inputPixel = pImage[resolution.Width * y + x];
        //        tempBuffer[resolution.Width * y + x] = inputPixel;
        //        tempBuffer[resolution.Width * y + x + 1] = inputPixel;
        //        tempBuffer[resolution.Width * y + x + 2] = inputPixel;
        //    }
        //}

        width = resolution.Width;
        height = resolution.Height;
        pixelBufferSize = resolution.Width * resolution.Height;

        //// Solution 2 - let caller convert it to RGB texture
        //// Copy grayscale image as single channel 8 bit format
        com_array<UINT8> tempBuffer(outBufferCount);
        if ( !flip )
        {
          for ( int i = 0; i < outBufferCount; ++i )
          {
            tempBuffer[i] = pImage[i];
          }
        }
        else
        {
          for ( int x = 0; x < width; x++ )
          {
            for ( int y = 0; y < height; y++ )
            {
              tempBuffer[x + y * width] = pImage[x + ( height - y - 1 ) * width];
            }
          }
        }

        // DEBUG JMH
        {

          auto timestamp = PerceptionTimestampHelper::FromSystemRelativeTargetTime(
              HundredsOfNanoseconds( checkAndConvertUnsigned( m_prevTimestamp ) ) );
          auto location = m_locator.TryLocateAtTimestamp( timestamp, unitySpatialCoordinateSytem );

          if ( !location )
          {
            return winrt::com_array<uint8_t>();
          }

          const float4x4 dynamicNodeToCoordinateSystem =
              make_float4x4_from_quaternion( location.Orientation() ) *
              make_float4x4_translation( location.Position() );
          auto absoluteTimestamp = m_converter
                                       .RelativeTicksToAbsoluteTicks(
                                           HundredsOfNanoseconds( (long long)m_prevTimestamp ) )
                                       .count();

          m_frameLocation = FrameLocation{ absoluteTimestamp, dynamicNodeToCoordinateSystem };
        }

        std::array<double, 16> VLCtoWorldtransform_values;
        //VLCtoWorldtransform_values[0] = m_frameLocation.rigToWorldtransform.m11;
        //VLCtoWorldtransform_values[1] = m_frameLocation.rigToWorldtransform.m12;
        //VLCtoWorldtransform_values[2] = m_frameLocation.rigToWorldtransform.m13;
        //VLCtoWorldtransform_values[3] = m_frameLocation.rigToWorldtransform.m14;
        //VLCtoWorldtransform_values[4] = m_frameLocation.rigToWorldtransform.m21;
        //VLCtoWorldtransform_values[5] = m_frameLocation.rigToWorldtransform.m22;
        //VLCtoWorldtransform_values[6] = m_frameLocation.rigToWorldtransform.m23;
        //VLCtoWorldtransform_values[7] = m_frameLocation.rigToWorldtransform.m24;
        //VLCtoWorldtransform_values[8] = m_frameLocation.rigToWorldtransform.m31;
        //VLCtoWorldtransform_values[9] = m_frameLocation.rigToWorldtransform.m32;
        //VLCtoWorldtransform_values[10] = m_frameLocation.rigToWorldtransform.m33;
        //VLCtoWorldtransform_values[11] = m_frameLocation.rigToWorldtransform.m34;
        //VLCtoWorldtransform_values[12] = m_frameLocation.rigToWorldtransform.m41;
        //VLCtoWorldtransform_values[13] = m_frameLocation.rigToWorldtransform.m42;
        //VLCtoWorldtransform_values[14] = m_frameLocation.rigToWorldtransform.m43;
        //VLCtoWorldtransform_values[15] = m_frameLocation.rigToWorldtransform.m44;

        // Matrix needs to be transposed for SolAR
        VLCtoWorldtransform_values[0] = m_frameLocation.rigToWorldtransform.m11;
        VLCtoWorldtransform_values[1] = m_frameLocation.rigToWorldtransform.m21;
        VLCtoWorldtransform_values[2] = m_frameLocation.rigToWorldtransform.m31;
        VLCtoWorldtransform_values[3] = m_frameLocation.rigToWorldtransform.m41;
        VLCtoWorldtransform_values[4] = m_frameLocation.rigToWorldtransform.m12;
        VLCtoWorldtransform_values[5] = m_frameLocation.rigToWorldtransform.m22;
        VLCtoWorldtransform_values[6] = m_frameLocation.rigToWorldtransform.m32;
        VLCtoWorldtransform_values[7] = m_frameLocation.rigToWorldtransform.m42;
        VLCtoWorldtransform_values[8] = m_frameLocation.rigToWorldtransform.m13;
        VLCtoWorldtransform_values[9] = m_frameLocation.rigToWorldtransform.m23;
        VLCtoWorldtransform_values[10] = m_frameLocation.rigToWorldtransform.m33;
        VLCtoWorldtransform_values[11] = m_frameLocation.rigToWorldtransform.m43;
        VLCtoWorldtransform_values[12] = m_frameLocation.rigToWorldtransform.m14;
        VLCtoWorldtransform_values[13] = m_frameLocation.rigToWorldtransform.m24;
        VLCtoWorldtransform_values[14] = m_frameLocation.rigToWorldtransform.m34;
        VLCtoWorldtransform_values[15] = m_frameLocation.rigToWorldtransform.m44;


        std::array<double, 16> PVLCtoWorldtransformSolar_values;
        PVLCtoWorldtransformSolar_values.fill(0);
        
        // Utils::convertToSolARPose(VLCtoWorldtransform_values, PVLCtoWorldtransformSolar_values);
        PVLCtoWorldtransformSolar_values = VLCtoWorldtransform_values;

        // PVtoWorldtransform = com_array<double>(std::move_iterator(VLCtoWorldtransform_values), std::move_iterator(VLCtoWorldtransform_values + 16 * sizeof(double)));
        PVtoWorldtransform = com_array<double>(PVLCtoWorldtransformSolar_values.begin(), PVLCtoWorldtransformSolar_values.end());

        return tempBuffer;
        // com_array<UINT8> tempBuffer = com_array<UINT8>(std::move_iterator(pImage), std::move_iterator(pImage + outBufferCount));
        
    }

    return winrt::com_array<uint8_t>();
}

winrt::com_array<uint16_t> RMCameraReader::getDepthSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height)
{
  std::lock_guard<std::mutex> reader_guard(m_sensorFrameMutex);
  if ( m_pSensorFrame && IsNewTimestamp( m_pSensorFrame ) )
    {
        timestamp = static_cast<uint64_t>(m_prevTimestamp);

        IResearchModeSensorDepthFrame* pDepthFrame = nullptr;
        auto hr = m_pSensorFrame->QueryInterface(IID_PPV_ARGS(&pDepthFrame));
        if (FAILED(hr))
        {
            // TODO(jmhenaff): find better design if possible (introduce sensor reader types to rely on polymorphism, or anything else...
            throw std::runtime_error("Cannot call 'getDepthSensorData()' on a camera reader assigned to a non-depth sensor");
            //return winrt::com_array<uint8_t>();
        }

        ResearchModeSensorResolution resolution;
        m_pSensorFrame->GetResolution(&resolution);

        bool isLongThrow = (m_pRMSensor->GetSensorType() == DEPTH_LONG_THROW);

        const UINT16* pAbImage = nullptr;
        size_t outAbBufferCount = 0;
        wchar_t outputAbPath[MAX_PATH];

        const UINT16* pDepth = nullptr;
        size_t outDepthBufferCount = 0;
        wchar_t outputDepthPath[MAX_PATH];

        const BYTE* pSigma = nullptr;
        size_t outSigmaBufferCount = 0;

        timestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp)).count();

        if (isLongThrow)
        {
            winrt::check_hresult(pDepthFrame->GetSigmaBuffer(&pSigma, &outSigmaBufferCount));
        }

        winrt::check_hresult(pDepthFrame->GetAbDepthBuffer(&pAbImage, &outAbBufferCount));
        winrt::check_hresult(pDepthFrame->GetBuffer(&pDepth, &outDepthBufferCount));

        pDepthFrame->Release();

        assert(outAbBufferCount == outDepthBufferCount);
        if (isLongThrow)
        {
            assert(outAbBufferCount == outSigmaBufferCount);
        }
            
        com_array<UINT16> tempBuffer(outDepthBufferCount * 2);
        for (size_t i = 0; i < outAbBufferCount; ++i)
        {
            UINT16 abVal;
            UINT16 d;
            const bool invalid = isLongThrow ? ((pSigma[i] & Depth::InvalidationMasks::Invalid) > 0) :
                                                (pDepth[i] >= Depth::AHAT_INVALID_VALUE);
            if (invalid)
            {
                d = 0;
            }
            else
            {
                d = pDepth[i];
            }

            abVal = pAbImage[i];

            tempBuffer[i] = d;
            tempBuffer[i + outDepthBufferCount] = abVal;
        }

        width = resolution.Width;
        height = resolution.Height;
        pixelBufferSize = resolution.Width * resolution.Height;

        std::array<double, 16> VLCtoWorldtransform_values;
        VLCtoWorldtransform_values[0] = m_frameLocation.rigToWorldtransform.m11;
        VLCtoWorldtransform_values[1] = m_frameLocation.rigToWorldtransform.m12;
        VLCtoWorldtransform_values[2] = m_frameLocation.rigToWorldtransform.m13;
        VLCtoWorldtransform_values[3] = m_frameLocation.rigToWorldtransform.m14;
        VLCtoWorldtransform_values[4] = m_frameLocation.rigToWorldtransform.m21;
        VLCtoWorldtransform_values[5] = m_frameLocation.rigToWorldtransform.m22;
        VLCtoWorldtransform_values[6] = m_frameLocation.rigToWorldtransform.m23;
        VLCtoWorldtransform_values[7] = m_frameLocation.rigToWorldtransform.m24;
        VLCtoWorldtransform_values[8] = m_frameLocation.rigToWorldtransform.m31;
        VLCtoWorldtransform_values[9] = m_frameLocation.rigToWorldtransform.m32;
        VLCtoWorldtransform_values[10] = m_frameLocation.rigToWorldtransform.m33;
        VLCtoWorldtransform_values[11] = m_frameLocation.rigToWorldtransform.m34;
        VLCtoWorldtransform_values[12] = m_frameLocation.rigToWorldtransform.m41;
        VLCtoWorldtransform_values[13] = m_frameLocation.rigToWorldtransform.m42;
        VLCtoWorldtransform_values[14] = m_frameLocation.rigToWorldtransform.m43;
        VLCtoWorldtransform_values[15] = m_frameLocation.rigToWorldtransform.m44;

        std::array<double, 16> PVLCtoWorldtransformSolar_values;
        PVLCtoWorldtransformSolar_values.fill(0);
        // Utils::convertToSolARPose(VLCtoWorldtransform_values, PVLCtoWorldtransformSolar_values);
        PVLCtoWorldtransformSolar_values = VLCtoWorldtransform_values;

        // PVtoWorldtransform = com_array<double>(std::move_iterator(VLCtoWorldtransform_values), std::move_iterator(VLCtoWorldtransform_values + 16 * sizeof(double)));
        PVtoWorldtransform = com_array<double>(PVLCtoWorldtransformSolar_values.begin(), PVLCtoWorldtransformSolar_values.end());

        return tempBuffer;
        
    }

    return winrt::com_array<UINT16>();
}


//com_array<uint8_t> RMCameraReader::getSensorData(uint64_t& timestamp, com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height)
//{
//    // TODO(jmhenaff): compute sensor type once instead of doing this here (perf)
//
//    IResearchModeSensorVLCFrame* pVLCFrame = nullptr;
//    IResearchModeSensorDepthFrame* pDepthFrame = nullptr;
//
//    HRESULT hr = m_pSensorFrame->QueryInterface(IID_PPV_ARGS(&pVLCFrame));
//
//    if (FAILED(hr))
//    {
//        hr = m_pSensorFrame->QueryInterface(IID_PPV_ARGS(&pDepthFrame));
//    }
//    
//    com_array<uint8_t> result;
//
//    if (pVLCFrame)
//    {
//        com_array<uint8_t> result = getVlcSensorData(timestamp, PVtoWorldtransform, pixelBufferSize, width, height);
//        pVLCFrame->Release();
//    }
//
//    if (pDepthFrame)
//    {
//        com_array<uint8_t> result = getDepthSensorData(timestamp, PVtoWorldtransform, pixelBufferSize, width, height);
//        pDepthFrame->Release();
//    }
//
//    return result;
//
//
//
//
//
//    //std::lock_guard<std::mutex> reader_guard(m_sensorFrameMutex);
//    //if (m_pSensorFrame)
//    //{
//    //    if (IsNewTimestamp(m_pSensorFrame))
//    //    {
//    //        timestamp = static_cast<uint64_t>(m_prevTimestamp);
//
//    //        // TODO(jmhenaff): convert frame
//    //        // Copy last frame, release mutex and process afterwards ?
//    //        
//    //        IResearchModeSensorVLCFrame* pVLCFrame = nullptr;
//    //        auto hr = m_pSensorFrame->QueryInterface(IID_PPV_ARGS(&pVLCFrame));
//    //        if (FAILED(hr))
//    //        {
//    //            // TODO(jmhenaff): handle error, depth frame
//    //            return winrt::com_array<uint8_t>();
//    //        }
//
//    //        //// TODO(jmhenaff): do something with this?
//    //        //UINT32 gain = 0;
//    //        //UINT64 exposure = 0;
//    //        //pVLCFrame->GetGain(&gain);
//    //        //pVLCFrame->GetExposure(&exposure);
//
//    //        // Get resolution
//    //        ResearchModeSensorResolution resolution;
//    //        m_pSensorFrame->GetResolution(&resolution);
//
//    //        // Get data buffer (8 bits grayscale)
//    //        const BYTE* pImage = nullptr;
//    //        size_t outBufferCount = 0;
//    //        pVLCFrame->GetBuffer(&pImage, &outBufferCount);
//    //        pVLCFrame->Release();
//
//    //        // Convert to RGB8 (32 bits)
//    //        com_array<UINT8> tempBuffer(resolution.Width * resolution.Height);
//    //        for (UINT i = 0; i < resolution.Height; i++)
//    //        {
//    //            for (UINT j = 0; j < resolution.Width; j++)
//    //            {
//    //                BYTE inputPixel = pImage[resolution.Width * i + j];
//    //                tempBuffer[resolution.Width * i + j] = inputPixel;
//    //            }
//    //        }
//    //        width = resolution.Width;
//    //        height = resolution.Height;
//    //        pixelBufferSize = resolution.Width * resolution.Height;
//
//    //        std::array<double, 16> VLCtoWorldtransform_values;
//    //        VLCtoWorldtransform_values[0] = m_frameLocation.rigToWorldtransform.m11;
//    //        VLCtoWorldtransform_values[1] = m_frameLocation.rigToWorldtransform.m12;
//    //        VLCtoWorldtransform_values[2] = m_frameLocation.rigToWorldtransform.m13;
//    //        VLCtoWorldtransform_values[3] = m_frameLocation.rigToWorldtransform.m14;
//    //        VLCtoWorldtransform_values[4] = m_frameLocation.rigToWorldtransform.m21;
//    //        VLCtoWorldtransform_values[5] = m_frameLocation.rigToWorldtransform.m22;
//    //        VLCtoWorldtransform_values[6] = m_frameLocation.rigToWorldtransform.m23;
//    //        VLCtoWorldtransform_values[7] = m_frameLocation.rigToWorldtransform.m24;
//    //        VLCtoWorldtransform_values[8] = m_frameLocation.rigToWorldtransform.m31;
//    //        VLCtoWorldtransform_values[9] = m_frameLocation.rigToWorldtransform.m32;
//    //        VLCtoWorldtransform_values[10] = m_frameLocation.rigToWorldtransform.m33;
//    //        VLCtoWorldtransform_values[11] = m_frameLocation.rigToWorldtransform.m34;
//    //        VLCtoWorldtransform_values[12] = m_frameLocation.rigToWorldtransform.m41;
//    //        VLCtoWorldtransform_values[13] = m_frameLocation.rigToWorldtransform.m42;
//    //        VLCtoWorldtransform_values[14] = m_frameLocation.rigToWorldtransform.m43;
//    //        VLCtoWorldtransform_values[15] = m_frameLocation.rigToWorldtransform.m44;
//    //        
//    //        std::array<double, 16> PVLCtoWorldtransformSolar_values;
//    //        PVLCtoWorldtransformSolar_values.fill(0);
//    //        Utils::convertToSolARPose(VLCtoWorldtransform_values, PVLCtoWorldtransformSolar_values);
//    //     
//    //        // PVtoWorldtransform = com_array<double>(std::move_iterator(VLCtoWorldtransform_values), std::move_iterator(VLCtoWorldtransform_values + 16 * sizeof(double)));
//    //        PVtoWorldtransform = com_array<double>(PVLCtoWorldtransformSolar_values.begin(), PVLCtoWorldtransformSolar_values.end());
//
//    //        return tempBuffer;
//    //        // com_array<UINT8> tempBuffer = com_array<UINT8>(std::move_iterator(pImage), std::move_iterator(pImage + outBufferCount));
//    //    }
//    //}
//    //return winrt::com_array<uint8_t>();
//}

uint32_t RMCameraReader::getWidth()
{
    std::lock_guard<std::mutex> reader_guard(m_sensorFrameMutex);

    if (!m_pSensorFrame)
    {
        return 0;
    }

    ResearchModeSensorResolution resolution;
    auto res = m_pSensorFrame->GetResolution(&resolution);
    return resolution.Width;
}

uint32_t RMCameraReader::getHeight()
{
    std::lock_guard<std::mutex> reader_guard(m_sensorFrameMutex);
    
    if (!m_pSensorFrame)
    {
        return 0;
    }

    ResearchModeSensorResolution resolution; 
    auto res = m_pSensorFrame->GetResolution(&resolution);
    return resolution.Height;
}

void RMCameraReader::SetLocator(const GUID& guid)
{
    m_locator = Preview::SpatialGraphInteropPreview::CreateLocatorForNode(guid);
}

bool RMCameraReader::IsNewTimestamp(IResearchModeSensorFrame* pSensorFrame)
{
    ResearchModeSensorTimestamp timestamp;
    winrt::check_hresult(pSensorFrame->GetTimeStamp(&timestamp));

    if (m_prevTimestamp == timestamp.HostTicks)
    {
        return false;
    }

    m_prevTimestamp = timestamp.HostTicks;

    return true;
}

void RMCameraReader::SetStorageFolder(const StorageFolder& storageFolder)
{
    if (storageFolder)
    {
        std::lock_guard<std::mutex> storage_guard(m_storageMutex);
        m_storageFolder = storageFolder;
        wchar_t fileName[MAX_PATH] = {};
        swprintf_s(fileName, L"%s\\%s.tar", m_storageFolder.Path().data(), m_pRMSensor->GetFriendlyName());
        m_tarball.reset(new Io::Tarball(fileName));
        m_storageCondVar.notify_all();
    }
}

void RMCameraReader::ResetStorageFolder()
{
    std::lock_guard<std::mutex> storage_guard(m_storageMutex);
    if (m_storageFolder)
    {
        DumpCalibration();
        DumpFrameLocations();
        m_tarball.reset();
        m_storageFolder = nullptr;
    }
}

void RMCameraReader::SetWorldCoordSystem(const SpatialCoordinateSystem& coordSystem)
{
    m_worldCoordSystem = coordSystem;
}

std::string CreateHeader(const ResearchModeSensorResolution& resolution, int maxBitmapValue)
{
    std::string bitmapFormat = "P5"; 

    // Compose PGM header string
    std::stringstream header;
    header << bitmapFormat << "\n"
        << resolution.Width << " "
        << resolution.Height << "\n"
        << maxBitmapValue << "\n";
    return header.str();
}

void RMCameraReader::SaveDepth(IResearchModeSensorFrame* pSensorFrame, IResearchModeSensorDepthFrame* pDepthFrame)
{        
    // Get resolution (will be used for PGM header)
    ResearchModeSensorResolution resolution;    
    pSensorFrame->GetResolution(&resolution);   
            
    bool isLongThrow = (m_pRMSensor->GetSensorType() == DEPTH_LONG_THROW);

    const UINT16* pAbImage = nullptr;
    size_t outAbBufferCount = 0;
    wchar_t outputAbPath[MAX_PATH];

    const UINT16* pDepth = nullptr;
    size_t outDepthBufferCount = 0;
    wchar_t outputDepthPath[MAX_PATH];

    const BYTE* pSigma = nullptr;
    size_t outSigmaBufferCount = 0;

    HundredsOfNanoseconds timestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp));

    if (isLongThrow)
    {
        winrt::check_hresult(pDepthFrame->GetSigmaBuffer(&pSigma, &outSigmaBufferCount));
    }

    winrt::check_hresult(pDepthFrame->GetAbDepthBuffer(&pAbImage, &outAbBufferCount));
    winrt::check_hresult(pDepthFrame->GetBuffer(&pDepth, &outDepthBufferCount));

    // Get header for AB and Depth (16 bits)
    // Prepare the data to save for AB
    const std::string abHeaderString = CreateHeader(resolution, 65535);
    swprintf_s(outputAbPath, L"%llu_ab.pgm", timestamp.count());
    std::vector<BYTE> abPgmData;
    abPgmData.reserve(abHeaderString.size() + outAbBufferCount * sizeof(UINT16));
    abPgmData.insert(abPgmData.end(), abHeaderString.c_str(), abHeaderString.c_str() + abHeaderString.size());
    
    // Prepare the data to save for Depth
    const std::string depthHeaderString = CreateHeader(resolution, 65535);
    swprintf_s(outputDepthPath, L"%llu.pgm", timestamp.count());
    std::vector<BYTE> depthPgmData;
    depthPgmData.reserve(depthHeaderString.size() + outDepthBufferCount * sizeof(UINT16));
    depthPgmData.insert(depthPgmData.end(), depthHeaderString.c_str(), depthHeaderString.c_str() + depthHeaderString.size());
    
    assert(outAbBufferCount == outDepthBufferCount);
    if (isLongThrow)
        assert(outAbBufferCount == outSigmaBufferCount);
    // Validate depth
    for (size_t i = 0; i < outAbBufferCount; ++i)
    {
        UINT16 abVal;
        UINT16 d;
        const bool invalid = isLongThrow ? ((pSigma[i] & Depth::InvalidationMasks::Invalid) > 0) :
                                           (pDepth[i] >= Depth::AHAT_INVALID_VALUE);
        if (invalid)
        {
            d = 0;
        }
        else
        {            
            d = pDepth[i];
        }

        abVal = pAbImage[i];

        abPgmData.push_back((BYTE)(abVal >> 8));
        abPgmData.push_back((BYTE)abVal);
        depthPgmData.push_back((BYTE)(d >> 8));
        depthPgmData.push_back((BYTE)d);
    }

    m_tarball->AddFile(outputAbPath, &abPgmData[0], abPgmData.size());
    m_tarball->AddFile(outputDepthPath, &depthPgmData[0], depthPgmData.size());
}

void RMCameraReader::SaveVLC(IResearchModeSensorFrame* pSensorFrame, IResearchModeSensorVLCFrame* pVLCFrame)
{        
    wchar_t outputPath[MAX_PATH];

    // Get PGM header
    int maxBitmapValue = 255;
    ResearchModeSensorResolution resolution;
    winrt::check_hresult(pSensorFrame->GetResolution(&resolution));

    const std::string headerString = CreateHeader(resolution, maxBitmapValue);

    // Compose the output file name using absolute ticks
    swprintf_s(outputPath, L"%llu.pgm", m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds(checkAndConvertUnsigned(m_prevTimestamp))).count());

    // Convert the software bitmap to raw bytes    
    std::vector<BYTE> pgmData;
    size_t outBufferCount = 0;
    const BYTE* pImage = nullptr;

    winrt::check_hresult(pVLCFrame->GetBuffer(&pImage, &outBufferCount));

    pgmData.reserve(headerString.size() + outBufferCount);
    pgmData.insert(pgmData.end(), headerString.c_str(), headerString.c_str() + headerString.size());
    pgmData.insert(pgmData.end(), pImage, pImage + outBufferCount);

    m_tarball->AddFile(outputPath, &pgmData[0], pgmData.size());
}

void RMCameraReader::SaveFrame(IResearchModeSensorFrame* pSensorFrame)
{
    AddFrameLocation();

	IResearchModeSensorVLCFrame* pVLCFrame = nullptr;
	IResearchModeSensorDepthFrame* pDepthFrame = nullptr;

    HRESULT hr = pSensorFrame->QueryInterface(IID_PPV_ARGS(&pVLCFrame));

	if (FAILED(hr))
	{
		hr = pSensorFrame->QueryInterface(IID_PPV_ARGS(&pDepthFrame));
	}

	if (pVLCFrame)
	{
		SaveVLC(pSensorFrame, pVLCFrame);
        pVLCFrame->Release();
	}

	if (pDepthFrame)
	{		
		SaveDepth(pSensorFrame, pDepthFrame);
        pDepthFrame->Release();
	}    
}

bool RMCameraReader::AddFrameLocation()
{
    auto timestamp = PerceptionTimestampHelper::FromSystemRelativeTargetTime(HundredsOfNanoseconds(checkAndConvertUnsigned(m_prevTimestamp)));
    auto location = m_locator.TryLocateAtTimestamp(timestamp, m_worldCoordSystem);
    if (!location)
    {
        return false;
    }
    const float4x4 dynamicNodeToCoordinateSystem = make_float4x4_from_quaternion(location.Orientation()) * make_float4x4_translation(location.Position());
    auto absoluteTimestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp)).count();
    m_frameLocations.push_back(std::move(FrameLocation{absoluteTimestamp, dynamicNodeToCoordinateSystem}));

    return true;
}

// TODO(jmhenaff): avoid code duplication between this and AddFrameLocation()
bool RMCameraReader::updateFrameLocation()
{
    //auto timestamp = PerceptionTimestampHelper::FromSystemRelativeTargetTime(HundredsOfNanoseconds(checkAndConvertUnsigned(m_prevTimestamp)));
    //auto location = m_locator.TryLocateAtTimestamp(timestamp, m_worldCoordSystem);
    //if (!location)
    //{
    //    return false;
    //}
    //const float4x4 dynamicNodeToCoordinateSystem = make_float4x4_from_quaternion(location.Orientation()) * make_float4x4_translation(location.Position());
    //auto absoluteTimestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp)).count();

    //m_frameLocation = FrameLocation{ absoluteTimestamp, dynamicNodeToCoordinateSystem };

    //return true;


    //// Initialize world coordinate with first frame location
    //// TODO: use Cannon MixedReality methods ? (see StreamRecorder sample)
    //if (!m_worldCoordSystem)
    //{
    //    SetWorldCoordSystem(m_locator.CreateStationaryFrameOfReferenceAtCurrentLocation().CoordinateSystem());
    //}

    assert( m_worldCoordSystem );

    auto timestamp = PerceptionTimestampHelper::FromSystemRelativeTargetTime(HundredsOfNanoseconds(checkAndConvertUnsigned(m_prevTimestamp)));
    auto location = m_locator.TryLocateAtTimestamp(timestamp, m_worldCoordSystem);

    //ISpatialCoordinateSystem* m_UnitySpatialCoordinateSystem = nullptr;
    //spatialCoordinateSystem.as<IUnknown>()->QueryInterface(winrt::guid_of<ISpatialCoordinateSystem>(), (void**)(&m_UnitySpatialCoordinateSystem));
    //auto location = m_locator.TryLocateAtTimestamp(timestamp, m_worldCoordSystem);

    if (!location)
    {
        return false;
    }

    const float4x4 dynamicNodeToCoordinateSystem = make_float4x4_from_quaternion(location.Orientation()) * make_float4x4_translation(location.Position());
    auto absoluteTimestamp = m_converter.RelativeTicksToAbsoluteTicks(HundredsOfNanoseconds((long long)m_prevTimestamp)).count();

    m_frameLocation = FrameLocation{ absoluteTimestamp, dynamicNodeToCoordinateSystem };

    return true;
}
