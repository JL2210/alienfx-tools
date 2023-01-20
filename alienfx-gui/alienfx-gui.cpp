#include "alienfx-gui.h"
#include <ColorDlg.h>
#include <Dbt.h>
#include "EventHandler.h"
#include "MonHelper.h"
#include "common.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"msimg32.lib")

// Global Variables:
HINSTANCE hInst;
bool isNewVersion = false;
bool needUpdateFeedback = false;
bool needRemove = false;

void ResetDPIScale();

HWND InitInstance(HINSTANCE, int);

BOOL CALLBACK MainDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK TabLightsDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK TabColorDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK TabProfilesDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK TabSettingsDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK TabFanDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

FXHelper* fxhl;
ConfigHandler* conf;
EventHandler* eve;
extern AlienFan_SDK::Control* acpi;
ConfigFan* fan_conf = NULL;
MonHelper* mon = NULL;
ThreadHelper* updateUI = NULL;

HWND mDlg = NULL, dDlg = NULL;

// color selection:
AlienFX_SDK::Afx_action* mod;
HANDLE stopColorRefresh = 0;

// tooltips
HWND sTip1 = 0, sTip2 = 0, sTip3 = 0;

// Tab selections:
int tabSel = 0;
int tabLightSel = 0;

// Explorer restart event
UINT newTaskBar = RegisterWindowMessage(TEXT("TaskbarCreated"));

// last light selected
int eItem = 0;

// last zone selected
groupset* mmap = NULL;

// Effect mode list
static const vector<string> effModes{ "Off", "Monitoring", "Ambient", "Haptics", "Grid"};

bool DetectFans() {
	if (conf->fanControl && (conf->fanControl = EvaluteToAdmin())) {
		mon = new MonHelper();
		if (mon->monThread) {
			fan_conf->lastSelectedSensor = acpi->sensors.front().sid;
		}
		else {
			delete mon;
			conf->fanControl = false;
		}
	}
	return conf->fanControl;
}

void SetHotkeys() {
	if (conf->keyShortcuts) {
		//register global hotkeys...
		RegisterHotKey(mDlg, 1, MOD_CONTROL | MOD_SHIFT, VK_F12);
		RegisterHotKey(mDlg, 2, MOD_CONTROL | MOD_SHIFT, VK_F11);
		RegisterHotKey(mDlg, 3, 0, VK_F18);
		RegisterHotKey(mDlg, 4, MOD_CONTROL | MOD_SHIFT, VK_F10);
		RegisterHotKey(mDlg, 5, MOD_CONTROL | MOD_SHIFT, VK_F9);
		RegisterHotKey(mDlg, 6, 0, VK_F17);
		//profile change hotkeys...
		for (int i = 0; i < 10; i++)
			RegisterHotKey(mDlg, 10 + i, MOD_CONTROL | MOD_SHIFT, 0x30 + i); // 1,2,3...
		//power mode hotkeys
		if (acpi)
			for (int i = 0; i < acpi->powers.size(); i++)
				RegisterHotKey(mDlg, 30 + i, MOD_CONTROL | MOD_ALT, 0x30 + i); // 0,1,2...
	}
	else {
		//unregister global hotkeys...
		UnregisterHotKey(mDlg, 1);
		UnregisterHotKey(mDlg, 2);
		UnregisterHotKey(mDlg, 3);
		UnregisterHotKey(mDlg, 4);
		UnregisterHotKey(mDlg, 5);
		UnregisterHotKey(mDlg, 6);
		//profile/power change hotkeys...
		for (int i = 0; i < 10; i++) {
			UnregisterHotKey(mDlg, 10 + i);
			UnregisterHotKey(mDlg, 30 + i);
		}
	}
}

void ReloadProfileList();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	//UNREFERENCED_PARAMETER(lpCmdLine);
	//UNREFERENCED_PARAMETER(nCmdShow);

	ResetDPIScale(lpCmdLine);

	conf = new ConfigHandler();
	conf->Load();

	if (!conf->afx_dev.GetGrids()->size()) {
		conf->afx_dev.GetGrids()->push_back({ 0, 20, 8, "Main" });
		conf->afx_dev.GetGrids()->back().grid = new AlienFX_SDK::Afx_groupLight[20 * 8]{ 0 };
	}
	conf->mainGrid = &conf->afx_dev.GetGrids()->front();

	fan_conf = conf->fan_conf;

	if (conf->activeProfile->flags & PROF_FANS)
		fan_conf->lastProf = &conf->activeProfile->fansets;

	if (conf->esif_temp || conf->fanControl || conf->awcc_disable)
		if (EvaluteToAdmin()) {
			conf->wasAWCC = DoStopService(conf->awcc_disable, true);
			DetectFans();
		}
		else
			conf->fanControl = false;

	fxhl = new FXHelper();
	//fxhl->FillAllDevs(acpi);

	eve = new EventHandler();

	if (conf->startMinimized)
		eve->StartProfiles();

	hInst = hInstance;

	if (CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MAINWINDOW), NULL, (DLGPROC)MainDialog)) {

		SendMessage(mDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ALIENFXGUI)));
		SendMessage(mDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ALIENFXGUI), IMAGE_ICON, 16, 16, 0));

		SetHotkeys();
		// Power notifications...
		RegisterPowerSettingNotification(mDlg, &GUID_MONITOR_POWER_ON, 0);
		RegisterPowerSettingNotification(mDlg, &GUID_LIDSWITCH_STATE_CHANGE, 0);
		//sParams->PowerSetting == GUID_MONITOR_POWER_ON ||
		//	sParams->PowerSetting == GUID_CONSOLE_DISPLAY_STATE ||
		//	sParams->PowerSetting == GUID_SESSION_DISPLAY_STATUS ||
		//	sParams->PowerSetting == GUID_LIDSWITCH_STATE_CHANGE

		ShowWindow(mDlg, conf->startMinimized ? SW_HIDE : SW_SHOW);

		ReloadProfileList();

		HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ALIENFXGUI));

		MSG msg;
		// Main message loop:
		while ((GetMessage(&msg, 0, 0, 0)) != 0) {
			if (!TranslateAccelerator(mDlg, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	delete eve;
	fxhl->Refresh(2);
	delete fxhl;

	DoStopService(conf->wasAWCC, false);

	if (acpi) {
		delete mon;
	}

	delete conf;

	return 0;
}

void SetTrayTip() {
	//string name = "Profile: " + (conf->activeProfile ? conf->activeProfile->name : "Undefined") + "\nEffect: " + (conf->GetEffect() < effModes.size() ? effModes[conf->GetEffect()] : "Global");
	string name = "Profile: " + conf->activeProfile->name + "\nEffect: " + effModes[conf->GetEffect()];
	strcpy_s(conf->niData.szTip, 128, name.c_str());
	Shell_NotifyIcon(NIM_MODIFY, &conf->niData);
}

void RedrawButton(HWND ctrl, AlienFX_SDK::Afx_colorcode* act) {
	RECT rect;
	HBRUSH Brush = act ? CreateSolidBrush(RGB(act->r, act->g, act->b)) : CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	GetClientRect(ctrl, &rect);
	HDC cnt = GetWindowDC(ctrl);
	FillRect(cnt, &rect, Brush);
	DrawEdge(cnt, &rect, EDGE_RAISED, BF_RECT);
	DeleteObject(Brush);
	ReleaseDC(ctrl, cnt);
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG: {
		SetDlgItemText(hDlg, IDC_STATIC_VERSION, ("Version: " + GetAppVersion()).c_str());
		return (INT_PTR)TRUE;
	} break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK: case IDCANCEL:
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		} break;
		}
		break;
	case WM_NOTIFY:
		switch (LOWORD(wParam)) {
		case IDC_SYSLINK_HOMEPAGE:
			switch (((LPNMHDR)lParam)->code)
			{
			case NM_CLICK:
			case NM_RETURN:
			{
				ShellExecute(NULL, "open", "https://github.com/T-Troll/alienfx-tools", NULL, NULL, SW_SHOWNORMAL);
			} break;
			} break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

void ResizeTab(HWND tab) {
	RECT dRect, rcDisplay;
	GetClientRect(GetParent(tab), &rcDisplay);
	rcDisplay.left = GetSystemMetrics(SM_CXBORDER);
	rcDisplay.top = GetSystemMetrics(SM_CYSMSIZE) - GetSystemMetrics(SM_CYBORDER);
	rcDisplay.right -= 2 * GetSystemMetrics(SM_CXBORDER) + 1;
	rcDisplay.bottom -= GetSystemMetrics(SM_CYBORDER) + 1;
	GetClientRect(tab, &dRect);
	int deltax = dRect.right - (rcDisplay.right - rcDisplay.left),
		deltay = dRect.bottom - (rcDisplay.bottom - rcDisplay.top);
	if (deltax || deltay) {
		//DebugPrint("Resize needed!\n");
		GetWindowRect(GetParent(GetParent(tab)), &dRect);
		SetWindowPos(GetParent(GetParent(tab)), NULL, 0, 0, dRect.right - dRect.left + deltax, dRect.bottom - dRect.top + deltay, SWP_NOZORDER | SWP_NOMOVE);
		rcDisplay.right += deltax;
		rcDisplay.bottom += deltay;
	}
	SetWindowPos(tab, NULL,
		rcDisplay.left, rcDisplay.top,
		rcDisplay.right - rcDisplay.left,
		rcDisplay.bottom - rcDisplay.top,
		SWP_SHOWWINDOW /*| SWP_NOSIZE*/);
}

void OnSelChanged(HWND hwndDlg)
{
	// Get the dialog header data.
	DLGHDR* pHdr = (DLGHDR*)GetWindowLongPtr( hwndDlg, GWLP_USERDATA);

	// Get the index of the selected tab.
	TCITEM tie{ TCIF_PARAM };
	TabCtrl_GetItem(hwndDlg, TabCtrl_GetCurSel(hwndDlg), &tie);
	tabSel = (int)tie.lParam;

	// Destroy the current child dialog box, if any.
	if (pHdr->hwndDisplay) {
		DestroyWindow(pHdr->hwndDisplay);
		//pHdr->hwndDisplay = NULL;
		//pHdr->hwndDisplay = CreateDialogIndirect(hInst, (DLGTEMPLATE*)pHdr->apRes[tabSel], hwndDlg, pHdr->apProc[tabSel]);
	}

	pHdr->hwndDisplay = CreateDialogIndirect(hInst, (DLGTEMPLATE*)pHdr->apRes[tabSel], hwndDlg, pHdr->apProc[tabSel]);
	ResizeTab(pHdr->hwndDisplay);
}

void ReloadModeList(HWND mode_list = NULL, int mode = conf->GetEffect()) {
	if (IsWindowVisible(mDlg)) {
		if (!mode_list) {
			mode_list = GetDlgItem(mDlg, IDC_EFFECT_MODE);
		}

		EnableWindow(mode_list, conf->enableMon);
		UpdateCombo(mode_list, effModes, mode, { 0,1,2,3,4 });

		if (tabSel == TAB_LIGHTS) {
			OnSelChanged(GetDlgItem(mDlg, IDC_TAB_MAIN));
		}
	}
}

void ReloadProfileList() {
	if (IsWindowVisible(mDlg)) {
		HWND profile_list = GetDlgItem(mDlg, IDC_PROFILES);
		ComboBox_ResetContent(profile_list);
		for (int i = 0; i < conf->profiles.size(); i++) {
			ComboBox_SetItemData(profile_list, ComboBox_AddString(profile_list, conf->profiles[i]->name.c_str()), conf->profiles[i]->id);
			if (conf->profiles[i]->id == conf->activeProfile->id) {
				ComboBox_SetCurSel(profile_list, i);
				ReloadModeList();
				if (tabSel == TAB_FANS && conf->activeProfile->flags & PROF_FANS) {
					OnSelChanged(GetDlgItem(mDlg, IDC_TAB_MAIN));
				}
			}
		}
		DebugPrint("Profile list reloaded.\n");
	}
}

void UpdateState(bool checkMode) {
	fxhl->SetState();
	eve->ChangeEffectMode();
	if (checkMode) {
		ReloadModeList();
	}
	if (tabSel == TAB_SETTINGS)
		OnSelChanged(GetDlgItem(mDlg, IDC_TAB_MAIN));
}

void RestoreApp() {
	ShowWindow(mDlg, SW_RESTORE);
	SetForegroundWindow(mDlg);
	ReloadProfileList();
}

void ClearOldTabs(HWND tab) {
	DLGHDR* pHdr = (DLGHDR*)GetWindowLongPtr(tab, GWLP_USERDATA);

	if (pHdr) {
		// close tab dialog and clean data
		DestroyWindow(pHdr->hwndControl);
		DestroyWindow(pHdr->hwndDisplay);
		delete[] pHdr->apRes;
		delete[] pHdr->apProc;
		delete pHdr;
		TabCtrl_DeleteAllItems(tab);
	}
}

void CreateTabControl(HWND parent, vector<string> names, vector<DWORD> resID, vector<DLGPROC> func) {

	ClearOldTabs(parent);

	DLGHDR* pHdr = new DLGHDR{ 0 };// (DLGHDR*)LocalAlloc(LPTR, sizeof(DLGHDR));
	SetWindowLongPtr(parent, GWLP_USERDATA, (LONG_PTR)pHdr);

	int tabsize = (int)names.size();

	pHdr->apRes = new DLGTEMPLATE*[tabsize];// (DLGTEMPLATE**)LocalAlloc(LPTR, tabsize * sizeof(DLGTEMPLATE*));
	pHdr->apProc = new DLGPROC[tabsize];// (DLGPROC*)LocalAlloc(LPTR, tabsize * sizeof(DLGPROC));

	TCITEM tie{ TCIF_TEXT | TCIF_PARAM };

	for (int i = 0; i < tabsize; i++) {
		if (tabsize < 5)
			switch (i) { // check disable tabs
			case 0: if (conf->afx_dev.fxdevs.empty()) continue; break;
			case 1: if (!acpi) continue; break;
			}
		pHdr->apRes[i] = (DLGTEMPLATE*)LockResource(LoadResource(hInst, FindResource(NULL, MAKEINTRESOURCE(resID[i]), RT_DIALOG)));
		tie.pszText = (LPSTR)names[i].c_str();
		tie.lParam = i;
		pHdr->apProc[i] = func[i];
		int pos = TabCtrl_InsertItem(parent, i, (LPARAM)&tie);
		if ((tabsize < 5 && i == tabSel) || (tabsize > 4 && i == tabLightSel))
			TabCtrl_SetCurSel(parent, pos);
	}
}

void SetMainTabs() {
	CreateTabControl(GetDlgItem(mDlg, IDC_TAB_MAIN),
		{ "Lights", "Fans and Power", "Profiles", "Settings" },
		{ IDD_DIALOG_LIGHTS, IDD_DIALOG_FAN, IDD_DIALOG_PROFILES, IDD_DIALOG_SETTINGS },
		{ (DLGPROC)TabLightsDialog, (DLGPROC)TabFanDialog, (DLGPROC)TabProfilesDialog, (DLGPROC)TabSettingsDialog }
	);
	OnSelChanged(GetDlgItem(mDlg, IDC_TAB_MAIN));
}

BOOL CALLBACK MainDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	HWND tab_list = GetDlgItem(hDlg, IDC_TAB_MAIN),
		profile_list = GetDlgItem(hDlg, IDC_PROFILES),
		mode_list = GetDlgItem(hDlg, IDC_EFFECT_MODE);

	if (message == newTaskBar && AddTrayIcon(&conf->niData, conf->updateCheck)) {
		// Started/restarted explorer...
		SetTrayTip();
	}

	switch (message)
	{
	case WM_INITDIALOG:
	{

		conf->niData.hWnd = mDlg = hDlg;

		SetMainTabs();

		conf->SetStates();

		if (AddTrayIcon(&conf->niData, conf->updateCheck))
			SetTrayTip();

	} break;
	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDC_BUT_PREV: case IDC_BUT_NEXT: case IDC_BUT_LAST: case IDC_BUT_FIRST:
			if (dDlg)
				SendMessage(dDlg, message, wParam, lParam);
			break;
		case IDOK: case IDCANCEL: case IDCLOSE: case IDM_EXIT:
			SendMessage(hDlg, WM_CLOSE, 0, 0);
			break;
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, About);
			break;
		case IDM_HELP:
			ShellExecute(NULL, "open", "https://github.com/T-Troll/alienfx-tools/blob/master/Doc/alienfx-gui.md", NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDM_CHECKUPDATE:
			// check update....
			needUpdateFeedback = true;
			CreateThread(NULL, 0, CUpdateCheck, &conf->niData, 0, NULL);
			break;
		case IDC_BUTTON_MINIMIZE:
			SendMessage(hDlg, WM_SIZE, SIZE_MINIMIZED, 0);
			break;
		case IDC_BUTTON_REFRESH:
			fxhl->Refresh();
			ReloadProfileList();
			break;
		case IDC_BUTTON_SAVE:
			conf->afx_dev.SaveMappings();
			conf->Save();
			fxhl->Refresh(2); // set def. colors
			ShowNotification(&conf->niData, "Configuration saved!", "Configuration saved successfully.", true);
			break;
		case IDC_EFFECT_MODE:
		{
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
			{
				conf->activeProfile->effmode = (WORD) ComboBox_GetItemData(mode_list, ComboBox_GetCurSel(mode_list));
				eve->ChangeEffectMode();
				if (tabSel == TAB_LIGHTS)
					OnSelChanged(tab_list);
			} break;
			}
		} break;
		case IDC_PROFILES: {
			switch (HIWORD(wParam))
			{
			case CBN_SELCHANGE: {
				int prid = (int)ComboBox_GetItemData(profile_list, ComboBox_GetCurSel(profile_list));
				eve->SwitchActiveProfile(conf->FindProfile(prid));
				ReloadModeList();
				OnSelChanged(tab_list);
			} break;
			}
		} break;
		} break;
	} break;
	case WM_NOTIFY:
		if (((NMHDR*)lParam)->idFrom == IDC_TAB_MAIN && ((NMHDR*)lParam)->code == TCN_SELCHANGE) {
			OnSelChanged(tab_list);
		}
		break;
	case WM_WINDOWPOSCHANGING: {
		WINDOWPOS* pos = (WINDOWPOS*)lParam;
		if (pos->flags & SWP_SHOWWINDOW && eve) {
			eve->StopProfiles();
		} else
			if (pos->flags & SWP_HIDEWINDOW && eve) {
				eve->StartProfiles();
			}
			else
			if (!(pos->flags & SWP_NOSIZE) && (pos->cx || pos->cy)) {
				RECT oldRect;
				GetWindowRect(mDlg, &oldRect);
				int deltax = pos->cx - oldRect.right + oldRect.left,
					deltay = pos->cy - oldRect.bottom + oldRect.top;
				if (deltax || deltay) {
					GetWindowRect(tab_list, &oldRect);
					SetWindowPos(tab_list, NULL, 0, 0, oldRect.right - oldRect.left + deltax, oldRect.bottom - oldRect.top + deltay, SWP_NOZORDER | SWP_NOMOVE);
					GetWindowRect(GetDlgItem(mDlg, IDC_PROFILES), &oldRect);
					SetWindowPos(GetDlgItem(mDlg, IDC_PROFILES), NULL, 0, 0, oldRect.right - oldRect.left + deltax, oldRect.bottom - oldRect.top, SWP_NOOWNERZORDER | SWP_NOMOVE);
					GetWindowRect(GetDlgItem(mDlg, IDC_EFFECT_MODE), &oldRect);
					ScreenToClient(mDlg, (LPPOINT)&oldRect);
					SetWindowPos(GetDlgItem(mDlg, IDC_EFFECT_MODE), NULL, oldRect.left + deltax, oldRect.top, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE);
					GetWindowRect(GetDlgItem(mDlg, IDC_STATIC_EFFECTS), &oldRect);
					ScreenToClient(mDlg, (LPPOINT)&oldRect);
					SetWindowPos(GetDlgItem(mDlg, IDC_STATIC_EFFECTS), NULL, oldRect.left + deltax, oldRect.top, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE);
					GetWindowRect(GetDlgItem(mDlg, IDC_BUTTON_REFRESH), &oldRect);
					ScreenToClient(mDlg, (LPPOINT)&oldRect);
					SetWindowPos(GetDlgItem(mDlg, IDC_BUTTON_REFRESH), NULL, oldRect.left, oldRect.top + deltay, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE);
					GetWindowRect(GetDlgItem(mDlg, IDC_BUTTON_MINIMIZE), &oldRect);
					ScreenToClient(mDlg, (LPPOINT)&oldRect);
					SetWindowPos(GetDlgItem(mDlg, IDC_BUTTON_MINIMIZE), NULL, oldRect.left + deltax, oldRect.top + deltay, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
					GetWindowRect(GetDlgItem(mDlg, IDC_BUTTON_SAVE), &oldRect);
					ScreenToClient(mDlg, (LPPOINT)&oldRect);
					SetWindowPos(GetDlgItem(mDlg, IDC_BUTTON_SAVE), NULL, oldRect.left + deltax, oldRect.top + deltay, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
				}
			}
		return false;
	} break;
	case WM_SIZE:
		switch (wParam) {
		case SIZE_MINIMIZED: {
			// go to tray...
			ShowWindow(hDlg, SW_HIDE);
			SendMessage(dDlg, WM_DESTROY, 0, 0);
		} break;
		}
		break;
	case WM_APP + 1: {
		switch (lParam)
		{
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONUP:
			RestoreApp();
			break;
		case WM_RBUTTONUP: case WM_CONTEXTMENU:
		{
			POINT lpClickPoint;
			HMENU tMenu = LoadMenu(hInst, MAKEINTRESOURCEA(IDR_MENU_TRAY));
			tMenu = GetSubMenu(tMenu, 0);
			MENUINFO mi{ sizeof(MENUINFO), MIM_STYLE, MNS_NOTIFYBYPOS };
			SetMenuInfo(tMenu, &mi);
			MENUITEMINFO mInfo{ sizeof(MENUITEMINFO), MIIM_STRING | MIIM_ID | MIIM_STATE };
			HMENU pMenu;
			// add profiles...
			if (!conf->enableProf) {
				pMenu = CreatePopupMenu();
				mInfo.wID = ID_TRAYMENU_PROFILE_SELECTED;
				for (int i = 0; i < conf->profiles.size(); i++) {
					mInfo.dwTypeData = (LPSTR) conf->profiles[i]->name.c_str();
					mInfo.fState = conf->profiles[i]->id == conf->activeProfile->id ? MF_CHECKED : MF_UNCHECKED;
					InsertMenuItem(pMenu, i, false, &mInfo);
				}
				ModifyMenu(tMenu, ID_TRAYMENU_PROFILES, MF_ENABLED | MF_BYCOMMAND | MF_STRING | MF_POPUP, (UINT_PTR) pMenu, "Profiles...");
			}
			// add effects menu...
			if (conf->enableMon) {
				pMenu = CreatePopupMenu();
				mInfo.wID = ID_TRAYMENU_MONITORING_SELECTED;
				int i = 0;
				for (i=0; i < effModes.size(); i++) {
					mInfo.dwTypeData = (LPSTR)effModes[i].c_str();
					mInfo.fState = i == conf->GetEffect() ? MF_CHECKED : MF_UNCHECKED;
					InsertMenuItem(pMenu, i, false, &mInfo);
				}
				ModifyMenu(tMenu, ID_TRAYMENU_MONITORING, MF_ENABLED | MF_BYCOMMAND | MF_POPUP | MF_STRING, (UINT_PTR) pMenu, "Effects...");
			}

			CheckMenuItem(tMenu, ID_TRAYMENU_ENABLEEFFECTS, conf->enableMon ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(tMenu, ID_TRAYMENU_LIGHTSON, conf->lightsOn ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(tMenu, ID_TRAYMENU_DIMLIGHTS, conf->IsDimmed() ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(tMenu, ID_TRAYMENU_PROFILESWITCH, conf->enableProf? MF_CHECKED : MF_UNCHECKED);

			GetCursorPos(&lpClickPoint);
			SetForegroundWindow(hDlg);
			TrackPopupMenu(tMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
				lpClickPoint.x, lpClickPoint.y, 0, hDlg, NULL);
		} break;
		case NIN_BALLOONHIDE : case NIN_BALLOONTIMEOUT:
			if (needRemove) {
				Shell_NotifyIcon(NIM_DELETE, &conf->niData);
				Shell_NotifyIcon(NIM_ADD, &conf->niData);
			}
			isNewVersion = false;
			break;
		case NIN_BALLOONUSERCLICK:
		{
			if (isNewVersion) {
				ShellExecute(NULL, "open", "https://github.com/T-Troll/alienfx-tools/releases", NULL, NULL, SW_SHOWNORMAL);
				isNewVersion = false;
			}
		} break;
		}
		break;
	} break;
	case WM_MENUCOMMAND: {
		int idx = LOWORD(wParam);
		switch (GetMenuItemID((HMENU)lParam, idx)) {
		case ID_TRAYMENU_EXIT:
			SendMessage(hDlg, WM_CLOSE, 0, 0);
		    return true;
		case ID_TRAYMENU_REFRESH:
			fxhl->Refresh();
			break;
		case ID_TRAYMENU_LIGHTSON:
			conf->lightsOn = !conf->lightsOn;
			UpdateState(false);
			break;
		case ID_TRAYMENU_DIMLIGHTS:
		    conf->SetDimmed();
			UpdateState(false);
			break;
		case ID_TRAYMENU_ENABLEEFFECTS:
			conf->enableMon = !conf->enableMon;
			UpdateState(true);
			break;
		case ID_TRAYMENU_MONITORING_SELECTED:
			conf->activeProfile->effmode = idx;
			UpdateState(true);
			break;
		case ID_TRAYMENU_PROFILESWITCH:
			eve->StopProfiles();
			conf->enableProf = !conf->enableProf;
			eve->StartProfiles();
			ReloadProfileList();
			break;
		case ID_TRAYMENU_RESTORE:
			RestoreApp();
			break;
		case ID_TRAYMENU_PROFILE_SELECTED: {
			if (conf->profiles[idx]->id != conf->activeProfile->id) {
				eve->SwitchActiveProfile(conf->profiles[idx]);
				ReloadProfileList();
			}
		} break;
		}
	} break;
	case WM_POWERBROADCAST:
		switch (wParam) {
		case PBT_APMRESUMEAUTOMATIC: {
			// resume from sleep/hibernate
			DebugPrint("Resume from Sleep/hibernate initiated\n");
			conf->stateOn = conf->lightsOn; // patch for later StateScreen update
			if (acpi)
				mon->Start();
			fxhl->Start();
			eve->StartEffects();
			eve->StartProfiles();
			if (conf->updateCheck) {
				needUpdateFeedback = false;
				CreateThread(NULL, 0, CUpdateCheck, &conf->niData, 0, NULL);
			}
		} break;
		case PBT_APMPOWERSTATUSCHANGE:
			// ac/batt change
			DebugPrint("Power source change initiated\n");
			eve->ChangePowerState();
			break;
		case PBT_POWERSETTINGCHANGE: {
			POWERBROADCAST_SETTING* sParams = (POWERBROADCAST_SETTING*) lParam;
			//if (sParams->PowerSetting == GUID_MONITOR_POWER_ON)
			conf->stateScreen = !conf->offWithScreen || sParams->Data[0];
			//else
			if (sParams->PowerSetting == GUID_LIDSWITCH_STATE_CHANGE)
				conf->stateScreen = conf->stateScreen || GetSystemMetrics(SM_CMONITORS) > 1;
			DebugPrint("Screen state changed to " + to_string(conf->stateScreen) + " (source: " +
				(sParams->PowerSetting == GUID_LIDSWITCH_STATE_CHANGE ? "Lid" : "Monitor")
				+ ")\n");
			fxhl->SetState();
			//if (sParams->PowerSetting == GUID_MONITOR_POWER_ON/* ||
			//	sParams->PowerSetting == GUID_CONSOLE_DISPLAY_STATE ||
			//	sParams->PowerSetting == GUID_SESSION_DISPLAY_STATUS */||
			//	sParams->PowerSetting == GUID_LIDSWITCH_STATE_CHANGE) {
			//	//eve->ChangeScreenState(sParams->Data[0]);
			//	conf->stateScreen = conf->offWithScreen && sParams->Data[0];
			//	fxhl->SetState();
			//}
		} break;
		case PBT_APMSUSPEND:
			// Sleep initiated.
			DebugPrint("Sleep/hibernate initiated\n");
			conf->Save();
			eve->StopProfiles();
			// need to restore lights if followed screen
			if (conf->offWithScreen) {
				conf->stateScreen = true;
				fxhl->SetState();
			}
			eve->StopEffects();
			fxhl->Refresh(2);
			fxhl->Stop();
			if (acpi)
				mon->Stop();
			break;
		}
		break;
	case WM_DEVICECHANGE:
		if (wParam == DBT_DEVNODES_CHANGED) {
			DebugPrint("Device list changed \n");
			vector<AlienFX_SDK::Functions*> devList = conf->afx_dev.AlienFXEnumDevices(acpi);
			if (devList.size() != conf->afx_dev.activeDevices) {
				DebugPrint("Active list changed!\n");
				bool updated = fxhl->updateThread;
				fxhl->Stop();
				conf->afx_dev.AlienFXApplyDevices(devList, conf->finalBrightness, conf->finalPBState);
				if (conf->afx_dev.activeDevices && updated) {
					fxhl->Start();
					fxhl->Refresh();
				}
				SetMainTabs();
			}
		}
		break;
	case WM_DISPLAYCHANGE:
		// Monitor configuration changed
	    if (eve->capt) eve->capt->Restart();
		break;
	case WM_ENDSESSION:
		// Shutdown/restart scheduled....
		DebugPrint("Shutdown initiated\n");
		conf->Save();
		delete eve;
		fxhl->Refresh(2);
		if (acpi)
			mon->Stop();
		fxhl->Stop();
		return 0;
	case WM_HOTKEY:
		if (wParam > 9 && wParam < 21) { // Profile switch
			if (wParam == 10)
				eve->SwitchActiveProfile(conf->FindDefaultProfile());
			else
				if (wParam - 10 < conf->profiles.size())
					eve->SwitchActiveProfile(conf->profiles[wParam - 10]);
			ReloadProfileList();
			break;
		}
		if (acpi && wParam > 29 && wParam - 30 < acpi->powers.size()) { // PowerMode switch
			conf->fan_conf->lastProf->powerStage = (WORD)wParam - 30;
			if (tabSel == TAB_FANS)
				OnSelChanged(tab_list);
			break;
		}
		switch (wParam) {
		case 1: // on/off
			conf->lightsOn = !conf->lightsOn;
			UpdateState(false);
			break;
		case 2: // dim
			conf->SetDimmed();
			UpdateState(false);
			break;
		case 3: // off-dim-full circle
			if (conf->lightsOn) {
				if (conf->IsDimmed())
					conf->lightsOn = false;
				conf->SetDimmed();
			}
			else
				conf->lightsOn = true;
			UpdateState(false);
			break;
		case 4: // effects
			conf->enableMon = !conf->enableMon;
			UpdateState(true);
			break;
		case 5: // profile autoswitch
			eve->StopProfiles();
			conf->enableProf = !conf->enableProf;
			eve->StartProfiles();
			ReloadProfileList();
			break;
		case 6: // G-key for Dell G-series power switch
			if (acpi) {
				mon->SetCurrentGmode(!conf->fan_conf->lastProf->gmode);
				if (tabSel == TAB_FANS)
					OnSelChanged(tab_list);
			}
			break;
		default: return false;
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hDlg);
		break;
	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &conf->niData);
		conf->Save();
		PostQuitMessage(0);
		break;
	default: return false;
	}
	return true;
}


AlienFX_SDK::Afx_colorcode *Act2Code(AlienFX_SDK::Afx_action *act) {
	return new AlienFX_SDK::Afx_colorcode({act->b,act->g,act->r});
}

AlienFX_SDK::Afx_action *Code2Act(AlienFX_SDK::Afx_colorcode *c) {
	return new AlienFX_SDK::Afx_action({0,0,0,c->r,c->g,c->b});
}

DWORD CColorRefreshProc(LPVOID param) {
	AlienFX_SDK::Afx_action last = *mod;
	groupset* mmap = (groupset*)param;
	int delay = conf->afx_dev.GetGroupById(mmap->group)->have_power ? 1000 : 200;
	while (WaitForSingleObject(stopColorRefresh, delay)) {
		if (last.r != mod->r || last.g != mod->g || last.b != mod->b) {
			last = *mod;
			if (mmap) fxhl->RefreshOne(mmap);
		}
	}
	return 0;
}

UINT_PTR Lpcchookproc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	DRAWITEMSTRUCT* item = 0;

	switch (message)
	{
	case WM_INITDIALOG:
		mod = (AlienFX_SDK::Afx_action*)((CHOOSECOLOR*)lParam)->lCustData;
		break;
	case WM_CTLCOLOREDIT:
		mod->r = GetDlgItemInt(hDlg, COLOR_RED, NULL, false);
		mod->g = GetDlgItemInt(hDlg, COLOR_GREEN, NULL, false);
		mod->b = GetDlgItemInt(hDlg, COLOR_BLUE, NULL, false);
		break;
	}
	return 0;
}

bool SetColor(HWND ctrl, /*groupset* mmap, */AlienFX_SDK::Afx_action* map) {
	CHOOSECOLOR cc{ sizeof(cc), ctrl, NULL, RGB(map->r, map->g, map->b), (LPDWORD)conf->customColors,
		CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR | CC_ENABLEHOOK, (LPARAM)map, Lpcchookproc };
	bool ret;

	AlienFX_SDK::Afx_action savedColor = *map;

	mod = map;
	stopColorRefresh = CreateEvent(NULL, false, false, NULL);
	HANDLE crRefresh = CreateThread(NULL, 0, CColorRefreshProc, mmap, 0, NULL);

	if (!(ret=ChooseColor(&cc)))
		(*map) = savedColor;

	SetEvent(stopColorRefresh);
	WaitForSingleObject(crRefresh, 500);
	CloseHandle(crRefresh);
	CloseHandle(stopColorRefresh);

	fxhl->RefreshOne(mmap);
	RedrawButton(ctrl, Act2Code(map));

	return ret;
}

bool SetColor(HWND ctrl, AlienFX_SDK::Afx_colorcode *clr) {
	CHOOSECOLOR cc{ sizeof(cc), ctrl };
	bool ret;

	cc.lpCustColors = (LPDWORD) conf->customColors;
	cc.rgbResult = RGB(clr->r, clr->g, clr->b);
	cc.Flags = CC_FULLOPEN | CC_RGBINIT;

	if (ret = ChooseColor(&cc)) {
		clr->r = cc.rgbResult & 0xff;
		clr->g = cc.rgbResult >> 8 & 0xff;
		clr->b = cc.rgbResult >> 16 & 0xff;
	}
	RedrawButton(ctrl, clr);
	return ret;
}

bool IsLightInGroup(DWORD lgh, AlienFX_SDK::Afx_group* grp) {
	if (grp)
		for (auto pos = grp->lights.begin(); pos < grp->lights.end(); pos++)
			if (pos->lgh == lgh)
				return true;
	return false;
}

bool IsGroupUnused(DWORD gid) {
	bool inSet = true;
	for (auto prof = conf->profiles.begin(); inSet && prof != conf->profiles.end(); prof++) {
		for (auto ls = (*prof)->lightsets.begin(); ls != (*prof)->lightsets.end(); ls++)
			if (ls->group == gid) {
				inSet = false;
				break;
			}
	}
	return inSet;
}

void RemoveUnused(vector<groupset>* lightsets) {
	for (auto it = lightsets->begin(); it != lightsets->end();)
		if (!(it->color.size() + it->events.size() + it->ambients.size() + it->haptics.size() + it->effect.type)) {
			lightsets->erase(it);
			it = lightsets->begin();
		}
		else
			it++;
}

void RemoveLightFromGroup(AlienFX_SDK::Afx_group* grp, WORD devid, WORD lightid) {
	AlienFX_SDK::Afx_groupLight cur{ devid, lightid };
	for (auto pos = grp->lights.begin(); pos != grp->lights.end(); pos++)
		if (pos->lgh == cur.lgh) {
			if (conf->afx_dev.GetFlags(devid, lightid) & ALIENFX_FLAG_POWER)
				grp->have_power = false;
			grp->lights.erase(pos);
			break;
		}
}

void RemoveLightAndClean(int dPid, int eLid) {
	// delete from all groups...
	for (auto iter = conf->afx_dev.GetGroups()->begin(); iter < conf->afx_dev.GetGroups()->end(); iter++) {
		RemoveLightFromGroup(&(*iter), dPid, eLid);
	}
	// Clean from grids...
	DWORD lcode = MAKELPARAM(dPid, eLid);
	for (auto g = conf->afx_dev.GetGrids()->begin(); g < conf->afx_dev.GetGrids()->end(); g++) {
		for (int ind = 0; ind < g->x * g->y; ind++)
			if (g->grid[ind].lgh == lcode)
				g->grid[ind].lgh = 0;
	}
}


