#ifndef HOLOLENS_DEMO_UTILS
#define HOLOLENS_DEMO_UTILS

#include <Eigen/dense>

#include <array>

namespace bcom::hololensdemo
{
	class Utils
	{
	public:

		// R(cam MS) -> R(cam OpenCV)
		static Eigen::Matrix4d m_T_Cms_Cocv;

		static void convertToSolARPose(const std::array<double, 16>& hololensCamToWorld,
			                                 std::array<double, 16>& SolARCamToWorld);
	};
}

#endif
