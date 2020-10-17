#include "FXHelper.h"
#include "../AlienFX-SDK/AlienFX_SDK/AlienFX_SDK.h"

FXHelper::FXHelper(int* freqp, ConfigHandler* conf) {
	freq = freqp;
	config = conf;
	//config->fxh = this;
	//lastUpdate = GetTickCount64();
	done = 0;
	stopped = 0;
	pid = AlienFX_SDK::Functions::AlienFXInitialize(AlienFX_SDK::Functions::vid);
	//std::cout << "PID: " << std::hex << isInit << std::endl;
	if (pid != -1)
	{
		bool result = AlienFX_SDK::Functions::Reset(false);
		if (!result) {
			//std::cout << "Reset faled with " << std::hex << GetLastError() << std::endl;
			return;
		}
		result = AlienFX_SDK::Functions::IsDeviceReady();
		AlienFX_SDK::Functions::LoadMappings();
	}
	FadeToBlack();
};
FXHelper::~FXHelper() {
	AlienFX_SDK::Functions::AlienFXClose();
};

int FXHelper::GetPID() {
	return pid;
}

int FXHelper::Refresh(int numbars)
{
	unsigned i = 0;
	for (i = 0; i < config->mappings.size(); i++) {
		mapping map = config->mappings[i];
		if (map.devid == pid && AlienFX_SDK::Functions::GetFlags(pid, map.lightid) == 0 && map.map.size() > 0) {
			double power = 0.0;
			Colorcode from, to, fin;
			from.ci = map.colorfrom.ci; to.ci = map.colorto.ci;
			// here need to check less bars...
			for (int j = 0; j < map.map.size(); j++)
				power += (freq[map.map[j]] > map.lowcut ? freq[map.map[j]] < map.hicut ? freq[map.map[j]] - map.lowcut : map.hicut - map.lowcut : 0);
			if (map.map.size() > 1)
				power = power / (map.map.size() * (map.hicut - map.lowcut));
			fin.cs.red = (unsigned char)((1 - power) * from.cs.red + power * to.cs.red);
			fin.cs.green = (unsigned char)((1 - power) * from.cs.green + power * to.cs.green);
			fin.cs.blue = (unsigned char)((1 - power) * from.cs.blue + power * to.cs.blue);
			//it's a bug into LightFX - r and b are inverted in this call!
			fin.cs.brightness = (unsigned char)((1 - power) * from.cs.brightness + power * to.cs.brightness);
			AlienFX_SDK::Functions::SetColor(map.lightid,
				fin.cs.red, fin.cs.green, fin.cs.blue);
		}
	}
	AlienFX_SDK::Functions::UpdateColors();
	return 0;
}

void FXHelper::FadeToBlack()
{
	for (int i = 0; i < config->mappings.size(); i++) {
		mapping map = config->mappings[i];
		Colorcode fin = { 0 };
		unsigned r = 0, g = 0, b = 0, size = (unsigned)map.map.size();
		if (map.devid == pid && AlienFX_SDK::Functions::GetFlags(pid, map.lightid) == 0 && size > 0) {
			AlienFX_SDK::Functions::SetColor(map.lightid, 0, 0, 0);
		}
	}
	AlienFX_SDK::Functions::UpdateColors();
}
