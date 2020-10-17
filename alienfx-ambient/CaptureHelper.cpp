#include "CaptureHelper.h"
#include <WinNT.h>
//#include <DaramCam.MediaFoundationGenerator.h>
#include <map>
#include <list>
#include <algorithm>
#include "resource.h"
#include "opencv2/core/mat.hpp"
#include "opencv2/imgproc.hpp"
#include <opencv2\imgproc\types_c.h>
//#include "opencv2/opencv.hpp"

using namespace cv;
using namespace std;

DWORD WINAPI CInProc(LPVOID);
DWORD WINAPI CDlgProc(LPVOID);
DWORD WINAPI CFXProc(LPVOID);
DWORD WINAPI ColorProc(LPVOID);

bool inWork = true;

HWND hDlg;

ConfigHandler* config;
FXHelper* fxh;

UCHAR  imgz[4 * 3 * 3];

DWORD uiThread, cuThread;
HANDLE uiHandle = 0, cuHandle = 0;

CaptureHelper::CaptureHelper(HWND dlg, ConfigHandler* conf, FXHelper* fhh)
{
	DCStartup();
	//DCMFStartup();
	SetCaptureScreen(conf->mode);
	hDlg = dlg;
	config = conf;
	fxh = fhh;// new FXHelper(conf);
}

CaptureHelper::~CaptureHelper()
{
	//DCMFShutdown();
	DCShutdown();
}

void CaptureHelper::SetCaptureScreen(int mode) {
	if (screenCapturer != NULL)
		delete screenCapturer;
	switch (mode) {
	case 0: screenCapturer = DCCreateDXGIScreenCapturer(DCDXGIScreenCapturerRange_MainMonitor);
		break; // primary monitor.
	case 1: screenCapturer = DCCreateDXGIScreenCapturer(DCDXGIScreenCapturerRange_SubMonitors);
		break; // other monitors.
	}
}

void CaptureHelper::Start()
{
	inWork = true;
	//fxh->StartFX();
	dwHandle = CreateThread(
		NULL,              // default security
		0,                 // default stack size
		CInProc,        // name of the thread function
		screenCapturer,
		0,                 // default startup flags
		&dwThreadID);
}

void CaptureHelper::Stop()
{
	DWORD exitCode;
	inWork = false;
	GetExitCodeThread(dwHandle, &exitCode);
	while (exitCode == STILL_ACTIVE) {
		Sleep(100);
		GetExitCodeThread(dwHandle, &exitCode);
	}
	CloseHandle(dwHandle);
	GetExitCodeThread(uiHandle, &exitCode);
	while (exitCode == STILL_ACTIVE) {
		Sleep(100);
		GetExitCodeThread(uiHandle, &exitCode);
	}
	CloseHandle(uiHandle);
	GetExitCodeThread(cuHandle, &exitCode);
	if (exitCode == STILL_ACTIVE)
		CloseHandle(cuHandle);
	dwHandle = uiHandle = cuHandle = 0;
	/*GetExitCodeThread(cuHandle, &exitCode);
	while (exitCode == STILL_ACTIVE) {
		Sleep(100);
		GetExitCodeThread(cuHandle, &exitCode);
	}*/
	//fxh->StopFX();
	//uiThread.stop;
	//cuThread.stop;
}

void CaptureHelper::Restart() {
	Stop();
	SetCaptureScreen(config->mode);
	Start();
}

cv::Mat extractHPts(const cv::Mat& inImage)
{
	// container for storing Hue Points
	cv::Mat listOfHPts(inImage.cols * inImage.rows, 1, CV_32FC1);
	//listOfHPts = cv::Mat::zeros(inImage.cols * inImage.rows, 1, CV_32FC1);
	Mat res[3];
	split(inImage, res);
	res[0].reshape(1, inImage.cols * inImage.rows).convertTo(listOfHPts, CV_32FC1);
	return listOfHPts;
}

// function for extracting dominant color from foreground pixels
cv::Mat getDominantColor(const cv::Mat& inImage, const cv::Mat& ptsLabel)
{
	// first we determine which cluster is foreground
	// assuming the our object of interest is the biggest object in the image region

	cv::Mat fPtsLabel, sumLabel;
	ptsLabel.convertTo(fPtsLabel, CV_32FC1);

	cv::reduce(fPtsLabel, sumLabel, 0, CV_REDUCE_SUM, CV_32FC1);

	int numFGPts = 0;

	if (sumLabel.at<float>(0, 0) < ptsLabel.rows / 2)
	{
		// invert the 0's and 1's where 1s represent foreground
		fPtsLabel = (fPtsLabel - 1) * (-1);
		numFGPts = fPtsLabel.rows - (int) sumLabel.at<float>(0, 0);
	}
	else
		numFGPts = (int) sumLabel.at<float>(0, 0);

	// to find dominant color, I just average all points belonging to foreground
	cv::Mat dominantColor;
	dominantColor = cv::Mat::zeros(1, 3, CV_32FC1);

	int idx = 0; int fgIdx = 0;
	for (int j = 0; j < inImage.rows; j++)
	{
		for (int i = 0; i < inImage.cols; i++)
		{
			if (fPtsLabel.at<float>(idx++, 0) == 1)
			{
				cv::Vec3b tempVec;
				tempVec = inImage.at<cv::Vec3b>(j, i);
				dominantColor.at<float>(0, 0) += (tempVec[0]);
				dominantColor.at<float>(0, 1) += (tempVec[1]);
				dominantColor.at<float>(0, 2) += (tempVec[2]);

				fgIdx++;
			}
		}
	}

	dominantColor /= numFGPts;

	// convert to uchar so that it can be used directly for visualization
	cv::Mat dColor(1, 3, CV_8UC1);
	//dColor = cv::Mat::zeros(1, 3, CV_8UC1);

	dominantColor.convertTo(dColor, CV_8UC1);

	//std::cout << "Dominant Color is: " <<  dColor << std::endl;

	return dColor;
}

/*struct procData {
	Mat src;
	UCHAR* dst;
};

uint fcount = 0;

DWORD WINAPI ColorProc(LPVOID inp) {
	procData* src = (procData*) inp;
	cv::Mat hPts;
	hPts = extractHPts(src->src);
	cv::Mat ptsLabel, kCenters;
	cv::kmeans(hPts, 2, ptsLabel, cv::TermCriteria(cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 1000, 0.00001)), 5, cv::KMEANS_PP_CENTERS, kCenters);

	cv::Mat dColor;
	dColor = getDominantColor(src->src, ptsLabel);
	src->dst[0] = dColor.ptr<UCHAR>()[0];
	src->dst[1] = dColor.ptr<UCHAR>()[1];
	src->dst[2] = dColor.ptr<UCHAR>()[2];
	delete src;
	fcount++;
	return 0;
}*/

void FillColors(Mat src) {
	uint w = src.cols / 4, h = src.rows / 3;
	Mat cPos;
	//fcount = 0;
	//procData* callData = NULL;
	for (uint dy = 0; dy < 3; dy++)
		for (uint dx = 0; dx < 4; dx++) {
			//callData = new procData();
			uint ptr = (dy * 4 + dx) * 3;
			cPos = src.rowRange(dy * h + 1, (dy + 1) * h)
				.colRange(dx * w + 1, (dx + 1) * w);
			/*callData->src = cPos;
			callData->dst = imgz + ptr;
			CreateThread(
				NULL,              // default security
				4*1024*1024,                 // default stack size
				ColorProc,        // name of the thread function
				callData,
				0,                 // default startup flags
				&uiThread);*/
			cv::Mat hPts;
			hPts = extractHPts(cPos);
			cv::Mat ptsLabel, kCenters;
			cv::kmeans(hPts, 2, ptsLabel, cv::TermCriteria(cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 1000, 0.00001)), 5, cv::KMEANS_PP_CENTERS, kCenters);

			cv::Mat dColor;
			dColor = getDominantColor(cPos, ptsLabel);
			imgz[ptr] = dColor.ptr<UCHAR>()[0];
			imgz[ptr + 1] = dColor.ptr<UCHAR>()[1];
			imgz[ptr + 2] = dColor.ptr<UCHAR>()[2];

		}
	//while (fcount != 12) Sleep(20);
}

DWORD WINAPI CInProc(LPVOID param)
{
	//IStream* stream = NULL;
	DCScreenCapturer* screenCapturer = (DCScreenCapturer*)param;
	UINT w, h, st, cdp;
	UCHAR* img = NULL;
	DWORD exitCode = 0;

	UINT div = config->divider;
	while (inWork) {
		screenCapturer->Capture();
		img = screenCapturer->GetCapturedBitmap()->GetByteArray();
		w = screenCapturer->GetCapturedBitmap()->GetWidth();
		h = screenCapturer->GetCapturedBitmap()->GetHeight();
		cdp = screenCapturer->GetCapturedBitmap()->GetColorDepth();
		st = screenCapturer->GetCapturedBitmap()->GetStride();
		// Resize & calc
		if (img != NULL) {
			cv::Mat redCenter;
			if (cdp == 4) {
				Mat src(h, w, CV_8UC4, img, st);
				Mat reduced;// (h / div, w / div, CV_8UC4);
				cv::resize(src, reduced, Size(w / div, h / div), 0, 0, INTER_AREA);
				cv::cvtColor(reduced, redCenter, CV_RGBA2RGB);
			}
			else {
				Mat src(h, w, CV_8UC3, img, st);
				cv::resize(src, redCenter, Size(w / div, h / div), 0, 0, INTER_AREA);
			}

			FillColors(redCenter);

			// Update lights
			if (uiHandle)
				GetExitCodeThread(uiHandle, &exitCode);
			if (exitCode != STILL_ACTIVE)
				uiHandle = CreateThread(
					NULL,              // default security
					0,                 // default stack size
					CFXProc,        // name of the thread function
					imgz,
					0,                 // default startup flags
					&uiThread);
			// Update UI
			if (cuHandle)
				GetExitCodeThread(cuHandle, &exitCode);
			if (exitCode != STILL_ACTIVE)
				cuHandle = CreateThread(
					NULL,              // default security
					0,                 // default stack size
					CDlgProc,        // name of the thread function
					imgz,
					0,                 // default startup flags
					&cuThread);
		}
		//free(imgz);
		Sleep(50);
	}
	return 0;
}

DWORD WINAPI CDlgProc(LPVOID param)
{
	UCHAR* img = (UCHAR *)param;
	RECT rect;
	HBRUSH Brush = NULL;
	for (int i = 0; i < 12; i++) {
		HWND tl = GetDlgItem(hDlg, IDC_BUTTON1 + i);
		HWND cBid = GetDlgItem(hDlg, IDC_CHECK1 + i);
		GetWindowRect(tl, &rect);
		HDC cnt = GetWindowDC(tl);
		//SetBkColor(cnt, RGB(255, 0, 0));
		//SetBkMode(cnt, TRANSPARENT);
		rect.bottom -= rect.top;
		rect.right -= rect.left;
		rect.top = rect.left = 0;
		// BGR!
		Brush = CreateSolidBrush(RGB(img[i*3+2], img[i*3+1], img[i*3]));
		FillRect(cnt, &rect, Brush);
		DeleteObject(Brush);
		UINT state = IsDlgButtonChecked(hDlg, IDC_CHECK1 + i); //Get state of the button
		if ((state & BST_CHECKED))            // If it is pressed
		{
			DrawEdge(cnt, &rect, EDGE_SUNKEN, BF_RECT);    // Draw a sunken face
		}
		else
		{
			DrawEdge(cnt, &rect, EDGE_RAISED, BF_RECT);    // Draw a raised face
		}
		RedrawWindow(cBid, 0, 0, RDW_INVALIDATE | RDW_UPDATENOW);
	}
	return 0;
}

DWORD WINAPI CFXProc(LPVOID param) {
	fxh->Refresh((UCHAR*)param);
	//fxh->UpdateLights();
	return 0;
}