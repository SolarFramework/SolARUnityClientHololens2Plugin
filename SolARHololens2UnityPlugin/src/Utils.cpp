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

#include "Utils.h"

namespace bcom::hololensdemo
{

    Eigen::Matrix4d Utils::m_T_Cms_Cocv = (Eigen::Matrix4d() <<
        1.0, 0.0, 0.0, 0.0,
        0.0, -1.0, 0.0, 0.0,
        0.0, 0.0, -1.0, 0.0,
        0.0, 0.0, 0.0, 1.0).finished();

	void Utils::convertToSolARPose(const std::array<double, 16>& hololensCamToWorld,
		                                 std::array<double, 16>& SolARCamToWorld)
	{

       //// R(cam MS) -> R(cam OpenCV)
        //Eigen::Matrix4d m_T_Cms_Cocv;
        //m_T_Cms_Cocv <<
        //    1.0, 0.0, 0.0, 0.0,
        //    0.0, -1.0, 0.0, 0.0,
        //    0.0, 0.0, -1.0, 0.0,
        //    0.0, 0.0, 0.0, 1.0;
        // T_Cocv_Cms = T_Cms_Cocv.T
        //Eigen::Matrix4d T_Cocv_Cms = m_T_Cms_Cocv.transpose();

        // Convert to Eigen matrix
        Eigen::Matrix4d camToWorldTransform;
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                camToWorldTransform(i, j) = hololensCamToWorld[i * 4 + j];
            }
        }

        Eigen::Matrix4d T_Cms_W = camToWorldTransform.transpose();

        // T_Cocv_W = T_Cms_W @ T_Cocv_Cms
        Eigen::Matrix4d T_Cocv_W = T_Cms_W * m_T_Cms_Cocv;

        // Convert back to array
        for (size_t i = 0; i < 4; i++)
        {
            for (size_t j = 0; j < 4; j++)
            {
                SolARCamToWorld[i * 4 + j] = T_Cocv_W(i, j);
            }
        }
	}

}