#include "GridHelper.h"
#include "EventHandler.h"
#include "FXHelper.h"

extern EventHandler* eve;
extern ConfigHandler* conf;
extern FXHelper* fxhl;

extern AlienFX_SDK::Afx_action* Code2Act(AlienFX_SDK::Afx_colorcode* c);

void GridHelper::StartGridRun(groupset* grp, zonemap* cz, int x, int y) {
	int cx = max(x, cz->xMax - x), cy = max(y, cz->yMax - y);
	switch (grp->gauge) {
	case 1:
		grp->gridop.size = cx;
		break;
	case 2:
		grp->gridop.size = cy;
		break;
	case 0: case 3: case 4:
		grp->gridop.size = cx + cy;
		break;
	case 5:
		grp->gridop.size = max(cx, cy);
		break;
	}
	grp->gridop.gridX = x;
	grp->gridop.gridY = y;
	fxhl->SetZone(grp, { *Code2Act(&grp->effect.from) });
	grp->gridop.start_tact = tact;
	grp->gridop.passive = false;
}

LRESULT CALLBACK GridKeyProc(int nCode, WPARAM wParam, LPARAM lParam) {

	LRESULT res = CallNextHookEx(NULL, nCode, wParam, lParam);

	if (wParam == WM_KEYDOWN && !(GetAsyncKeyState(((LPKBDLLHOOKSTRUCT)lParam)->vkCode) & 0xf000)) {
		char keyname [32];
		GetKeyNameText(MAKELPARAM(0,((LPKBDLLHOOKSTRUCT)lParam)->scanCode), keyname, 31);
 		for (auto it = conf->active_set->begin(); it < conf->active_set->end(); it++)
			if (it->effect.trigger == 3 && it->gridop.passive) { // keyboard effect
				// Is it have a key pressed?
				zonemap* zone = conf->FindZoneMap(it->group);
				for (auto pos = zone->lightMap.begin(); pos != zone->lightMap.end(); pos++)
					if (conf->afx_dev.GetMappingByDev(conf->afx_dev.GetDeviceById(LOWORD(pos->light), 0), HIWORD(pos->light))->name == (string)keyname) {
						eve->grid->StartGridRun(&(*it), zone, pos->x, pos->y);
						break;
					}
			}
	}

	return res;
}

void GridUpdate(LPVOID param) {
	if (conf->lightsNoDelay)
		fxhl->RefreshGrid(((GridHelper*)param)->tact++);
}

void GridHelper::StartCommonRun(groupset* ce, zonemap* cz) {
	switch (ce->gauge) {
	case 0: case 1:
		StartGridRun(&(*ce), cz, 0, 0);
		break;
	case 2:
		StartGridRun(&(*ce), cz, 0, 0);
		break;
	case 3: case 4:
		StartGridRun(&(*ce), cz, 0, 0);
		break;
	case 5:
		StartGridRun(&(*ce), cz, cz->xMax / 2, cz->yMax / 2);
		break;
	}
}

void GridTriggerWatch(LPVOID param) {
	GridHelper* src = (GridHelper*)param;
	for (auto ce = conf->active_set->begin(); ce < conf->active_set->end(); ce++) {
		if (/*ce->gauge && */ce->effect.trigger && ce->gridop.passive) {
			zonemap* cz = conf->FindZoneMap(ce->group);
			switch (ce->effect.trigger) {
			case 1: // Continues
				src->StartCommonRun(&(*ce), cz);
				break;
			case 2: { // Random
				uniform_int_distribution<int> pntX(0, cz->xMax-1);
				uniform_int_distribution<int> pntY(0, cz->yMax-1);
				src->StartGridRun(&(*ce), cz, pntX(src->rnd), pntY(src->rnd));
			} break;
			case 4: { // Indicator
				for (auto ev = ce->events.begin(); ev != ce->events.end(); ev++)
					if (ev->state == MON_TYPE_IND) {
						int ccut = ev->cut, cVal = 0;
						switch (ev->source) {
						case 0: cVal = fxhl->eData.HDD; break;
						case 1: cVal = fxhl->eData.NET; break;
						case 2: cVal = fxhl->eData.Temp - ccut; break;
						case 3: cVal = fxhl->eData.RAM - ccut; break;
						case 4: cVal = fxhl->eData.Batt - ccut; break;
						case 5: cVal = fxhl->eData.KBD; break;
						}

						if (cVal > 0) {
							// Triggering effect...
							src->StartCommonRun(&(*ce), cz);
						}
					}
			} break;
			}
		}
	}
}

GridHelper::GridHelper() {
	tact = 0;
	eve->StartEvents();
	gridTrigger = new ThreadHelper(GridTriggerWatch, (LPVOID)this, 100);
	gridThread = new ThreadHelper(GridUpdate, (LPVOID)this, 100);
#ifndef _DEBUG
	kEvent = SetWindowsHookExW(WH_KEYBOARD_LL, GridKeyProc, NULL, 0);
#endif // !_DEBUG
}

GridHelper::~GridHelper()
{
#ifndef _DEBUG
	UnhookWindowsHookEx(kEvent);
#endif
	eve->StopEvents();
	delete gridTrigger;
	delete gridThread;
}
