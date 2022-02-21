#pragma once

#include "SolARHololens2ResearchMode.g.h"
#include "winrt/impl/SolARHololens2UnityPlugin.0.h"

#include "ResearchModeApi.h"
#include "SensorScenario.h"

#include <stdio.h>
#include <iostream>
#include <sstream>
#include <wchar.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <cmath>
#include <DirectXMath.h>

#include <vector>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Preview.h>
#include <winrt/Windows.Foundation.h>

#include "Cannon/Common/Timer.h"
#include "Cannon/DrawCall.h"
#include "Cannon/FloatingSlate.h"
#include "Cannon/FloatingText.h"
#include "Cannon/MixedReality.h"
#include "Cannon/TrackedHands.h"


#include "VideoFrameProcessor.h"

namespace winrt::SolARHololens2UnityPlugin::implementation
{
    //enum RMSensorType
    //{
    //    LEFT_FRONT,
    //    LEFT_LEFT,
    //    RIGHT_FRONT,
    //    RIGHT_RIGHT,
    //};

    //enum StreamTypes
    //{
    //    PV,  // RGB
    //    EYE  // Eye gaze tracking
    //    // Hands captured by default
    //};

    struct SolARHololens2ResearchMode : SolARHololens2ResearchModeT<SolARHololens2ResearchMode>
    {

        SolARHololens2ResearchMode() = default;

        void Init();
        void EnablePv();
        void EnableVlc(RMSensorType sensorType);
        void EnableDepth(bool isLongThrow);
        // return false when attempting to change recording state while running
        bool EnableRecording();
        bool DisableRecording();
        bool IsRecordingEnabled();
        void Update();
        void Start();
        void Stop();
        boolean IsRunning();



//        void InitializeRGBSensor();
        //void StartRGBSensorCapture();
        //void StopRGBSensorCapture();
        com_array<uint8_t> GetPvData( uint64_t& timestamp, com_array<double>& PVtoWorldtransform,
                                      float& fx, float& fy, uint32_t& pixelBufferSize,
                                      uint32_t& width, uint32_t& height, bool flip = false );
        uint32_t GetPvWidth();
        uint32_t GetPvHeight();
        std::mutex murgb;
        
        // Research Mode cameras
        void InitializeRMSensors();
        bool StartRMSensor(RMSensorType sensor);
        void StopRMSensor(RMSensorType sensor);
        uint32_t GetVlcWidth(RMSensorType sensor);
        uint32_t GetVlcHeight(const RMSensorType sensor);
        com_array<uint8_t> GetVlcData( const RMSensorType sensor,
                                       uint64_t& timestamp,
                                       com_array<double>& PVtoWorldtransform,
                                       float& fx,
                                       float& fy,
                                       uint32_t& pixelBufferSize,
                                       uint32_t& width,
                                       uint32_t& height,
                                       bool flip = false);
        uint32_t GetDepthWidth();
        uint32_t GetDepthHeight();
        com_array<uint16_t> GetDepthData( uint64_t& timestamp,
                                          com_array<double>& PVtoWorldtransform,
                                          float& fx,
                                          float& fy,
                                          uint32_t& pixelBufferSize,
                                          uint32_t& width,
                                          uint32_t& height );

        static ResearchModeSensorType toHololensRMSensorType(RMSensorType sType);


    private:
        MixedReality m_mixedReality;

        /* Supported ResearchMode streams:
        {
            LEFT_FRONT,
            LEFT_LEFT,
            RIGHT_FRONT,
            RIGHT_RIGHT,
            DEPTH_AHAT,
            DEPTH_LONG_THROW
        }*/
        // TODO(jmhenaff): thing about using std::set. Easier to add without checking presence, easy to remove
        // if necessary, with extract()/node_handle (c++ 17)
        std::vector<ResearchModeSensorType> kEnabledRMStreamTypes;
        
        /* Supported not-ResearchMode streams:
        {
            PV,  // RGB
            EYE  // Eye gaze tracking
        }*/
        // TODO(jmhenaff): thing about using std::set. Easier to add without checking presence, easy
        // to remove
        // if necessary, with extract()/node_handle (c++ 17)
        std::vector<StreamTypes> kEnabledStreamTypes;

        std::unique_ptr<VideoFrameProcessor> m_videoFrameProcessor = nullptr;
        winrt::Windows::Foundation::IAsyncAction m_videoFrameProcessorOperation = nullptr;
        winrt::Windows::Foundation::IAsyncAction InitializeVideoFrameProcessorAsync();

        bool m_enable_recording{ false };
        winrt::Windows::Storage::StorageFolder m_archiveFolder = nullptr;
        bool m_is_running{ false };
        bool m_recording{ false };
        inline bool IsQRCodeDetected() { return m_qrCodeValue.length() > 0; };

        std::string m_qrCodeValue = ""; // TODO(jmhenaff) probably useless
        XMMATRIX m_qrCodeTransform;

        bool SetDateTimePath();
        std::wstring m_datetime;

        winrt::Windows::Foundation::IAsyncAction StartRecordingAsync();



        PVFrame          m_RGBFrame;
        std::atomic_bool m_RGBTextureUpdated = false;

        // Research Mode

        //// List of camera to activate
        //const std::vector<ResearchModeSensorType> m_rmCamSensors =
        //{
        //    ResearchModeSensorType::LEFT_FRONT/*,
        //    ResearchModeSensorType::LEFT_LEFT,
        //    ResearchModeSensorType::RIGHT_FRONT,
        //    ResearchModeSensorType::RIGHT_RIGHT*/
        //};
        std::unique_ptr<SensorScenario> m_sensorScenario = nullptr;

    };
}
namespace winrt::SolARHololens2UnityPlugin::factory_implementation
{
    struct SolARHololens2ResearchMode : SolARHololens2ResearchModeT<SolARHololens2ResearchMode, implementation::SolARHololens2ResearchMode>
    {
    };
}
