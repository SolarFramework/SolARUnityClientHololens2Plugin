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

#include "SensorScenario.h"

extern "C"
HMODULE LoadLibraryA(
	LPCSTR lpLibFileName
);

static ResearchModeSensorConsent camAccessCheck;
static HANDLE camConsentGiven;

SensorScenario::SensorScenario(const std::vector<ResearchModeSensorType>& kEnabledSensorTypes) :
	m_kEnabledSensorTypes(kEnabledSensorTypes)
{
}

SensorScenario::~SensorScenario()
{
	if (m_pLFCameraSensor)
	{
		m_pLFCameraSensor->Release();
	}
	if (m_pRFCameraSensor)
	{
		m_pRFCameraSensor->Release();
	}
	if (m_pLLCameraSensor)
	{
		m_pLLCameraSensor->Release();
	}
	if (m_pRRCameraSensor)
	{
		m_pRRCameraSensor->Release();
	}
	if (m_pLTSensor)
	{
		m_pLTSensor->Release();
	}
	if (m_pAHATSensor)
	{
		m_pAHATSensor->Release();
	}

	if (m_pSensorDevice)
	{
		m_pSensorDevice->EnableEyeSelection();
		m_pSensorDevice->Release();
	}

	if (m_pSensorDeviceConsent)
	{
		m_pSensorDeviceConsent->Release();
	}
}

void SensorScenario::GetRigNodeId(GUID& outGuid) const
{
	IResearchModeSensorDevicePerception* pSensorDevicePerception;
	winrt::check_hresult(m_pSensorDevice->QueryInterface(IID_PPV_ARGS(&pSensorDevicePerception)));
	winrt::check_hresult(pSensorDevicePerception->GetRigNodeId(&outGuid));
	pSensorDevicePerception->Release();
}

void SensorScenario::InitializeSensors()
{
	size_t sensorCount = 0;
	camConsentGiven = CreateEvent(nullptr, true, false, nullptr);

	// Load Research Mode library
	HMODULE hrResearchMode = LoadLibraryA("ResearchModeAPI");
	if (hrResearchMode)
	{
		typedef HRESULT(__cdecl* PFN_CREATEPROVIDER) (IResearchModeSensorDevice** ppSensorDevice);
		PFN_CREATEPROVIDER pfnCreate = reinterpret_cast<PFN_CREATEPROVIDER>(GetProcAddress(hrResearchMode, "CreateResearchModeSensorDevice"));
		if (pfnCreate)
		{
			winrt::check_hresult(pfnCreate(&m_pSensorDevice));
		}
	}

	// Manage Sensor Consent
	winrt::check_hresult(m_pSensorDevice->QueryInterface(IID_PPV_ARGS(&m_pSensorDeviceConsent)));
	winrt::check_hresult(m_pSensorDeviceConsent->RequestCamAccessAsync(SensorScenario::CamAccessOnComplete));	

	m_pSensorDevice->DisableEyeSelection();

	m_pSensorDevice->GetSensorCount(&sensorCount);
	m_sensorDescriptors.resize(sensorCount);

	m_pSensorDevice->GetSensorDescriptors(m_sensorDescriptors.data(), m_sensorDescriptors.size(), &sensorCount);

	// TODO(jmhenaff): it's not an assert, replace with clean error reporting to user
	// (e.g. upon build() is a builder is user, when enabling one of the two depth sensor, by returning a bool,...)
	// Or maybe make either depth sensor enabling function override the other, so that only the last choice is chosen (+ warning message)
	assert(!(std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), DEPTH_LONG_THROW) != m_kEnabledSensorTypes.end()
		  && std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), DEPTH_AHAT) != m_kEnabledSensorTypes.end()));

	for (auto& sensorDescriptor : m_sensorDescriptors)
	{
		if (sensorDescriptor.sensorType == LEFT_FRONT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), LEFT_FRONT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pLFCameraSensor));
		}

		if (sensorDescriptor.sensorType == RIGHT_FRONT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), RIGHT_FRONT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pRFCameraSensor));
		}

		if (sensorDescriptor.sensorType == LEFT_LEFT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), LEFT_LEFT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pLLCameraSensor));
		}

		if (sensorDescriptor.sensorType == RIGHT_RIGHT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), RIGHT_RIGHT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pRRCameraSensor));
		}

		if (sensorDescriptor.sensorType == DEPTH_LONG_THROW)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), DEPTH_LONG_THROW) == m_kEnabledSensorTypes.end())
			{
				continue;
			}
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pLTSensor));
		}

		if (sensorDescriptor.sensorType == DEPTH_AHAT)
		{
			if (std::find(m_kEnabledSensorTypes.begin(), m_kEnabledSensorTypes.end(), DEPTH_AHAT) == m_kEnabledSensorTypes.end())
			{
				continue;
			}
			winrt::check_hresult(m_pSensorDevice->GetSensor(sensorDescriptor.sensorType, &m_pAHATSensor));
		}
	}	
}

void SensorScenario::CamAccessOnComplete(ResearchModeSensorConsent consent)
{
	camAccessCheck = consent;
	SetEvent(camConsentGiven);
}

void SensorScenario::InitializeCameraReaders()
{
	// Get RigNode id which will be used to initialize
	// the spatial locators for camera readers objects
	GUID guid;
	GetRigNodeId(guid);

	if (m_pLFCameraSensor)
	{
		auto cameraReader = std::make_shared<RMCameraReader>(m_pLFCameraSensor, camConsentGiven, &camAccessCheck, guid);
		m_cameraReaders.insert_or_assign(ResearchModeSensorType::LEFT_FRONT, cameraReader);
	}

	if (m_pRFCameraSensor)
	{
		auto cameraReader = std::make_shared<RMCameraReader>(m_pRFCameraSensor, camConsentGiven, &camAccessCheck, guid);
		m_cameraReaders.insert_or_assign(ResearchModeSensorType::RIGHT_FRONT, cameraReader);
	}

	if (m_pLLCameraSensor)
	{
		auto cameraReader = std::make_shared<RMCameraReader>(m_pLLCameraSensor, camConsentGiven, &camAccessCheck, guid);
		m_cameraReaders.insert_or_assign(ResearchModeSensorType::LEFT_LEFT, cameraReader);
	}

	if (m_pRRCameraSensor)
	{
		auto cameraReader = std::make_shared<RMCameraReader>(m_pRRCameraSensor, camConsentGiven, &camAccessCheck, guid);
		m_cameraReaders.insert_or_assign(ResearchModeSensorType::RIGHT_RIGHT, cameraReader);
	}

	if (m_pLTSensor)
	{
		auto cameraReader = std::make_shared<RMCameraReader>(m_pLTSensor, camConsentGiven, &camAccessCheck, guid);
		m_cameraReaders.insert_or_assign(ResearchModeSensorType::DEPTH_LONG_THROW, cameraReader);
		m_depthCameraReader = cameraReader;
	}

	if (m_pAHATSensor)
	{
		auto cameraReader = std::make_shared<RMCameraReader>(m_pAHATSensor, camConsentGiven, &camAccessCheck, guid);
		m_cameraReaders.insert_or_assign(ResearchModeSensorType::DEPTH_AHAT, cameraReader);
		m_depthCameraReader = cameraReader;
	}	
}

void SensorScenario::StartRecording(const winrt::Windows::Storage::StorageFolder& folder,
									const winrt::Windows::Perception::Spatial::SpatialCoordinateSystem& worldCoordSystem)
{
	for (auto const& [sensorType, camReader] : m_cameraReaders)
	{
		camReader->SetWorldCoordSystem(worldCoordSystem);
		camReader->SetStorageFolder(folder);
		camReader->start();
	}
}

void SensorScenario::StopRecording()
{
	for (auto const& [sensorType, camReader] : m_cameraReaders)
	{
		camReader->ResetStorageFolder();
		camReader->stop();
	}
}

