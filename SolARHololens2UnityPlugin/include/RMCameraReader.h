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

#pragma once

#include "ResearchModeApi.h"
#include "Tar.h"
#include "TimeConverter.h"

#include <mutex>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Preview.h>


// Struct to store per-frame rig2world transformations
// See also https://docs.microsoft.com/en-us/windows/mixed-reality/locatable-camera
struct FrameLocation
{
	long long timestamp;	
	winrt::Windows::Foundation::Numerics::float4x4 rigToWorldtransform;
};


struct RMFrame
{
	long long timestamp;
	winrt::Windows::Foundation::Numerics::float4x4 PVtoWorldtransform;
	float fx;
	float fy;
	uint8_t* pixelBufferData;
	uint32_t pixelBufferSize;
	uint32_t width;
	uint32_t height;
};

class RMCameraReader
{
public:
	RMCameraReader(IResearchModeSensor* pLLSensor, HANDLE camConsentGiven, ResearchModeSensorConsent* camAccessConsent, const GUID& guid)
	{
		m_pRMSensor = pLLSensor;
		m_pRMSensor->AddRef();
		m_pSensorFrame = nullptr;

		m_camConsentGiven = camConsentGiven;
		m_camAccessConsent = camAccessConsent;

		// Get GUID identifying the rigNode to
		// initialize the SpatialLocator
		SetLocator(guid);
		// Reserve for 10 seconds at 30fps (holds for VLC)
		m_frameLocations.reserve(10 * 30);

		//m_pCameraUpdateThread = new std::thread(CameraUpdateThread, this, camConsentGiven, camAccessConsent);
		//m_pWriteThread = new std::thread(CameraWriteThread, this);
	}

	// Return false if thread is already running
	bool start();
	void stop();

	void SetStorageFolder(const winrt::Windows::Storage::StorageFolder& storageFolder);
	void SetWorldCoordSystem(const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& coordSystem);
	void ResetStorageFolder();	

	virtual ~RMCameraReader()
	{
		stop();

		if (m_pRMSensor)
		{
			m_pRMSensor->CloseStream();
			m_pRMSensor->Release();
		}

		//m_pWriteThread->join();
	}

	bool computeIntrinsics(float& fx, float& fy, float& cx, float& cy, float& avgReprojErr);
//	winrt::com_array<uint8_t> getSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height);
	winrt::com_array<uint8_t> getVlcSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height, bool flip);
	winrt::com_array<uint16_t> getDepthSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height);
	uint32_t getWidth();
	uint32_t getHeight();

protected:
	static void CameraUpdateThread(RMCameraReader* pReader, HANDLE camConsentGiven, ResearchModeSensorConsent* camAccessConsent);
	static void CameraWriteThread(RMCameraReader* pReader);

	bool IsNewTimestamp(IResearchModeSensorFrame* pSensorFrame);

	void SaveFrame(IResearchModeSensorFrame* pSensorFrame);
	void SaveVLC(IResearchModeSensorFrame* pSensorFrame, IResearchModeSensorVLCFrame* pVLCFrame);
	void SaveDepth(IResearchModeSensorFrame* pSensorFrame, IResearchModeSensorDepthFrame* pDepthFrame);

	void DumpCalibration();

	void SetLocator(const GUID& guid);
	bool AddFrameLocation();
	void DumpFrameLocations();
	bool updateFrameLocation();


	HANDLE m_camConsentGiven;
	ResearchModeSensorConsent* m_camAccessConsent;

	// Mutex to access sensor frame
	std::mutex m_sensorFrameMutex;
	IResearchModeSensor* m_pRMSensor = nullptr;
	IResearchModeSensorFrame* m_pSensorFrame = nullptr;

	std::atomic<bool> m_fExit = false;
	std::unique_ptr<std::thread> m_pCameraUpdateThread;
	std::unique_ptr<std::thread> m_pWriteThread;

	// Mutex to access storage folder
	std::mutex m_storageMutex;
	// conditional variable to enable / disable writing to disk
	std::condition_variable m_storageCondVar;
	winrt::Windows::Storage::StorageFolder m_storageFolder = nullptr;
	std::unique_ptr<Io::Tarball> m_tarball;

	TimeConverter m_converter;
	UINT64 m_prevTimestamp = 0;

	winrt::Windows::Perception::Spatial::SpatialLocator m_locator = nullptr;
	winrt::Windows::Perception::Spatial::SpatialCoordinateSystem m_worldCoordSystem = nullptr;
	std::vector<FrameLocation> m_frameLocations;
	FrameLocation m_frameLocation;

//	winrt::com_array<uint8_t> getVlcSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height);
//	winrt::com_array<uint16_t> getDepthSensorData(uint64_t& timestamp, winrt::com_array<double>& PVtoWorldtransform, uint32_t& pixelBufferSize, uint32_t& width, uint32_t& height);

};
