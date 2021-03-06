#include "stdafx.h"
#include "kinect.h"
#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>
#include<opencv2/core/core.hpp>
#include<System.h>
#include "Camera_Intrinsic_Parameters.h"
#include <conio.h>

using namespace cv;
using namespace std;

// 安全释放指针
template<class Interface>
inline void SafeRelease(Interface *& pInterfaceToRelease)
{
	if (pInterfaceToRelease != NULL)
	{
		pInterfaceToRelease->Release();
		pInterfaceToRelease = NULL;
	}
}

const double camera_factor = 1000;
const double camera_cx = 263.73696;
const double camera_cy = 201.72450;
const double camera_fx = 379.40726;
const double camera_fy = 378.54472;

int _tmain(int argc, _TCHAR* argv[])
{
	//相机内参
	Eigen::Matrix3d intricRGB;
	intricRGB << 1094.75283, 0, 942.00992,
				 0, 1087.37528, 530.35240,
				 0, 0, 1;
	Eigen::Matrix3d intricDepth;
	intricDepth << camera_fx, 0, camera_cx,
				   0, camera_fy, camera_cy,
		           0, 0, 1;
	Camera_Intrinsic_Parameters intricParams(263.73696, 201.72450, 379.40726, 378.54472, 1000);

	//两个相机之间的关系，目前缺少一个刚体变换阵
	Eigen::Matrix3d intricDepth2RGB;
	intricDepth2RGB = intricRGB*intricDepth.inverse();

	Eigen::Matrix3d intricRGB2Depth;
	intricRGB2Depth = intricDepth*intricRGB.inverse();
	// 获取Kinect设备
	IKinectSensor* m_pKinectSensor;
	HRESULT hr;
	hr = GetDefaultKinectSensor(&m_pKinectSensor);
	if (FAILED(hr))
	{
		return hr;
	}

	IMultiSourceFrameReader* m_pMultiFrameReader=NULL;
	if (m_pKinectSensor)
	{
		hr = m_pKinectSensor->Open();
		if (SUCCEEDED(hr))
		{
			// 获取多数据源到读取器  
			hr = m_pKinectSensor->OpenMultiSourceFrameReader(
				FrameSourceTypes::FrameSourceTypes_Color |
				FrameSourceTypes::FrameSourceTypes_Infrared |
				FrameSourceTypes::FrameSourceTypes_Depth,
				&m_pMultiFrameReader);
		}
	}

	if (!m_pKinectSensor || FAILED(hr))
	{
		return E_FAIL;
	}
	// 三个数据帧及引用
	IDepthFrameReference* m_pDepthFrameReference = NULL;
	IColorFrameReference* m_pColorFrameReference = NULL;
	IInfraredFrameReference* m_pInfraredFrameReference = NULL;
	IInfraredFrame* m_pInfraredFrame = NULL;
	IDepthFrame* m_pDepthFrame = NULL;
	IColorFrame* m_pColorFrame = NULL;
	IMultiSourceFrame* m_pMultiFrame = nullptr;
	// 三个图片格式
	Mat i_rgb(1080, 1920, CV_8UC4);      //注意：这里必须为4通道的图，Kinect的数据只能以Bgra格式传出
	UINT16 *depthData = new UINT16[424 * 512];
	UINT16 *InfraredData = new UINT16[424 * 512];
	CameraSpacePoint* m_pCameraCoordinates = new CameraSpacePoint[512 * 424];
	ColorSpacePoint* m_pColorCoordinates = new ColorSpacePoint[512 * 424];

	Mat i_Infrared(424, 512, CV_8UC1);
	Mat current_depth(424, 512, CV_16UC1);
	Mat current_calibrate_rgb(424, 512, CV_8UC3);
	Mat i_depthToRgb(424, 512, CV_8UC4);
	int idxFrame=0;
	Eigen::Matrix4d pose = Eigen::Matrix4d::Identity();
	//E:\LearnCode\ORBSLAM24Windows\Vocabulary\ORBvoc.txt 
	//E:\LearnCode\ORBSLAM24Windows\Examples\RGB-D\TUM2.yaml  
	//E:\LearnCode\ORBSLAM24Windows\rgbd_dataset_freiburg2_desk
	//E:\LearnCode\ORBSLAM24Windows\Examples\RGB-D\associations\fr2_desk.txt
	std::string voc="E:\\LearnCode\\ORBSLAM24Windows\\Vocabulary\\ORBvoc.txt";
	std::string camearParams = "E:\\CodeWork\\AxKinect2\\AxOrbSLAM\\AxOrbSLAM\\TUM2.yaml";
	ORB_SLAM2::System SLAM(voc, camearParams, ORB_SLAM2::System::RGBD, true);
	cv::Mat imRGB, imD, imIR;
	while (true)
	{
		// 获取新的一个多源数据帧
		hr = m_pMultiFrameReader->AcquireLatestFrame(&m_pMultiFrame);
		if (FAILED(hr) || !m_pMultiFrame)
		{
			continue;
		}
		// 从多源数据帧中分离出彩色数据，深度数据和红外数据
		if (SUCCEEDED(hr))
			hr = m_pMultiFrame->get_ColorFrameReference(&m_pColorFrameReference);
		if (SUCCEEDED(hr))
			hr = m_pColorFrameReference->AcquireFrame(&m_pColorFrame);
		if (SUCCEEDED(hr))
			hr = m_pMultiFrame->get_DepthFrameReference(&m_pDepthFrameReference);
		if (SUCCEEDED(hr))
			hr = m_pDepthFrameReference->AcquireFrame(&m_pDepthFrame);
		if (SUCCEEDED(hr))
			hr = m_pMultiFrame->get_InfraredFrameReference(&m_pInfraredFrameReference);
		if (SUCCEEDED(hr))
			hr = m_pInfraredFrameReference->AcquireFrame(&m_pInfraredFrame);

		// color拷贝到图片中
		UINT nColorBufferSize = 1920 * 1080 * 4;
		if (SUCCEEDED(hr))
			hr = m_pColorFrame->CopyConvertedFrameDataToArray(nColorBufferSize, reinterpret_cast<BYTE*>(i_rgb.data), ColorImageFormat::ColorImageFormat_Bgra);

		if (SUCCEEDED(hr))
		{
			hr = m_pInfraredFrame->CopyFrameDataToArray(424 * 512, InfraredData);
			for (int i = 0; i < 512 * 424; i++)
			{
				// 0-255深度图，为了显示明显，只取深度数据的低8位
				BYTE intensity = static_cast<BYTE>(InfraredData[i] % 256);
				reinterpret_cast<BYTE*>(i_Infrared.data)[i] = intensity;
			}
		}

		// depth拷贝到图片中
		if (SUCCEEDED(hr))
		{
			hr = m_pDepthFrame->CopyFrameDataToArray(424 * 512, depthData);
			for (int i = 0; i < 512 * 424; i++)
			{
				// 0-255深度图，为了显示明显，只取深度数据的低8位
				UINT16 intensity = static_cast<UINT16>(depthData[i]);
				reinterpret_cast<UINT16*>(current_depth.data)[i] = intensity;
			}
			// 实际是16位unsigned int数据
			//hr = m_pDepthFrame->CopyFrameDataToArray(424 * 512, reinterpret_cast<UINT16*>(i_depth.data));
		}
		ICoordinateMapper*      m_pCoordinateMapper = NULL;
		hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);

		HRESULT hr = m_pCoordinateMapper->MapDepthFrameToColorSpace(512 * 424, depthData, 512 * 424, m_pColorCoordinates);


		if (SUCCEEDED(hr))
		{
			for (int i = 0; i < 424 * 512; i++)
			{
				ColorSpacePoint p = m_pColorCoordinates[i];
				if (p.X != -std::numeric_limits<float>::infinity() && p.Y != -std::numeric_limits<float>::infinity())
				{
					int colorX = static_cast<int>(p.X + 0.5f);
					int colorY = static_cast<int>(p.Y + 0.5f);

					if ((colorX >= 0 && colorX < 1920) && (colorY >= 0 && colorY < 1080))
					{
						i_depthToRgb.data[i * 4] = i_rgb.data[(colorY * 1920 + colorX) * 4];
						i_depthToRgb.data[i * 4 + 1] = i_rgb.data[(colorY * 1920 + colorX) * 4 + 1];
						i_depthToRgb.data[i * 4 + 2] = i_rgb.data[(colorY * 1920 + colorX) * 4 + 2];
						i_depthToRgb.data[i * 4 + 3] = i_rgb.data[(colorY * 1920 + colorX) * 4 + 3];
					}
				}
			}
		}
		if (SUCCEEDED(hr))
		{
			HRESULT hr = m_pCoordinateMapper->MapDepthFrameToCameraSpace(512 * 424, depthData, 512 * 424, m_pCameraCoordinates);
		}
		int icor = 0;
		for (int row = 0; row < 1080; row++)
		{
			for (int col = 0; col < 1920; col++)
			{
				/*UINT16* p = (UINT16*)i_rgb.data;
				UINT16 r = static_cast<UINT16>(p[4*(row * 1920 + col)]);
				UINT16 g = static_cast<UINT16>(p[4 * (row * 1920 + col)+1]);
				UINT16 b = static_cast<UINT16>(p[4 * (row * 1920 + col)+2]);
				UINT16 a = static_cast<UINT16>(p[4 * (row * 1920 + col) + 3]);*/
				//cout << "depthValue       " << depthValue << endl;
				//if (depthValue != -std::numeric_limits<UINT16>::infinity() && depthValue != -std::numeric_limits<UINT16>::infinity() && depthValue != 0 && depthValue != 65535)
				{
					// 彩色图投影到深度图上的坐标
					Eigen::Vector3d uv_color(col, row, 1.0f);
					Eigen::Vector3d uv_depth = intricRGB2Depth* uv_color;//  / 1000.f +T / 1000;

					int X = static_cast<int>(uv_depth[0] / uv_depth[2]);
					int Y = static_cast<int>(uv_depth[1] / uv_depth[2]);
					if ((X >= 0 && X < 512) && (Y >= 0 && Y < 424))
					{
						current_calibrate_rgb.data[3 * (Y * 512 + X)] = i_rgb.data[4 * icor];
						current_calibrate_rgb.data[3 * (Y * 512 + X) + 1] = i_rgb.data[4 * icor + 1];
						current_calibrate_rgb.data[3 * (Y * 512 + X) + 2] = i_rgb.data[4 * icor + 2];
						//result.data[4 * (Y * 512 + X) + 3] = i_rgb.data[4 * icor + 3];
					}
				}
				icor++;
			}
		}
		flip(current_depth, imD, 1);
		//current_depth.copyTo(imD);
		flip(current_calibrate_rgb, imRGB, 1);
		//flip(i_Infrared, imIR, 1);
		//current_calibrate_rgb.copyTo(imRGB);		
		SYSTEMTIME st;
		GetLocalTime(&st);
		char output_file[32];
		char output_RGB[32];
		char output_depth[32];
		char output_ir[32];
		double tframe = 10000.0*st.wYear + 100.0*st.wMonth + st.wDay + 0.01*st.wHour + 0.0001*st.wMinute  + 0.000001*st.wSecond ;
		sprintf_s(output_file, "%15f.txt", tframe);
		sprintf_s(output_RGB, "%15f-rgb.png", tframe);
		sprintf_s(output_depth, "%15f-depth.png", tframe);
		sprintf_s(output_ir, "%15f-ir.png", tframe);
		imwrite(output_RGB, imRGB);
		imwrite(output_depth, imD);
		//imwrite(output_ir, imIR);
		FILE *file = fopen(output_file, "w");
		int idx = 0;

		for (int i = 0; i < 512 * 424; i++)
		{
			CameraSpacePoint p = m_pCameraCoordinates[i];
			if (p.X != -std::numeric_limits<float>::infinity() && p.Y != -std::numeric_limits<float>::infinity() && p.Z != -std::numeric_limits<float>::infinity())
			{
				float cameraX = static_cast<float>(p.X);
				float cameraY = static_cast<float>(p.Y);
				float cameraZ = static_cast<float>(p.Z);
				if (file)
				{
					int b = int(i_depthToRgb.data[i * 4 + 0]);
					int g = int(i_depthToRgb.data[i * 4 + 1]);
					int r = int(i_depthToRgb.data[i * 4 + 2]);
					fprintf(file, "%.4f %.4f %.4f %d %d %d\n", cameraX, cameraY, cameraZ, r, g, b);
				}
			}
		}

		fclose(file);
		idxFrame++;
		
		SLAM.TrackRGBD(imRGB, imD, tframe);
		// 释放资源
		SafeRelease(m_pColorFrame);
		SafeRelease(m_pDepthFrame);
		SafeRelease(m_pInfraredFrame);
		SafeRelease(m_pColorFrameReference);
		SafeRelease(m_pDepthFrameReference);
		SafeRelease(m_pInfraredFrameReference);
		SafeRelease(m_pMultiFrame);
		Sleep(100);
		if (_kbhit())
		{
			int ch = _getch();
			if (ch == 32)
			{
				break;
			}
		}
	}
	// 关闭窗口，设备
	cv::destroyAllWindows();
	// Stop all threads
	SLAM.Shutdown();
	// Save camera trajectory
	SLAM.SaveTrajectoryTUM("CameraTrajectory.txt");
	SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");
	m_pKinectSensor->Close();
	std::system("pause");

	return 0;
}

