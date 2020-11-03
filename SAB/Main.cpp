#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>

#define dwLocalPlayer 0xD3DD14
#define dwEntityList 0x4D523AC
#define m_dwBoneMatrix 0x26A8
#define m_iTeamNum 0xF4
#define m_iHealth 0x100
#define m_vecOrigin 0x138
#define m_bDormant 0xED
#define dwViewMatrix 0x4D43CC4

const int SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
const int SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);

const int CROSSHAIR_X = SCREEN_WIDTH / 2;
const int CROSSHAIR_Y = SCREEN_HEIGHT / 2;

struct ViewMatrix {
	float matrix[16];
};

HWND window;
DWORD processId;
uintptr_t baseAddress;
HANDLE csgoProcess;
ViewMatrix viewMatrix;
int closestEnemy;

template<typename T> T ReadMemory(SIZE_T address) {
	T buffer;
	ReadProcessMemory(csgoProcess, (LPCVOID)address, &buffer, sizeof(T), NULL);
	return buffer;
}

struct Vector3 {
	float x, y, z;
	Vector3() : x(0.f), y(0.f), z(0.f) {}
	Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct BoneMatrix {
	byte pad3[12];
	float x;
	byte pad1[12];
	float y;
	byte pad2[12];
	float z;
};

uintptr_t getBaseMemoryAddress() {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
	if (snapshot != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 moduleEntry;
		moduleEntry.dwSize = sizeof(moduleEntry);
		if (Module32First(snapshot, &moduleEntry)) {
			do {
				if (!strcmp(moduleEntry.szModule, "client.dll")) {
					CloseHandle(snapshot);
					return (uintptr_t)moduleEntry.modBaseAddr;
				}
			} while (Module32Next(snapshot, &moduleEntry));
		}
	}
}

uintptr_t getCurrentPlayerAddress() {
	return ReadMemory<uintptr_t>(baseAddress + dwLocalPlayer);
}

uintptr_t getPlayerAddress(int index) {
	return ReadMemory<uintptr_t>(baseAddress + dwEntityList + index * 0x10);
}

int getTeam(uintptr_t playerAddress) {
	return ReadMemory<int>(playerAddress + m_iTeamNum);
}

int getPlayerHealth(uintptr_t playerAddress) {
	return ReadMemory<int>(playerAddress + m_iHealth);
}

bool isNotPlayer(uintptr_t playerAddress) {
	return ReadMemory<int>(playerAddress + m_bDormant);
}

Vector3 getPlayerLocation(uintptr_t playerAddress) {
	return ReadMemory<Vector3>(playerAddress + m_vecOrigin);
}

Vector3 getPlayerHead(uintptr_t playerAddress) {
	uintptr_t bonesAddress = ReadMemory<uintptr_t>(playerAddress + m_dwBoneMatrix);
	BoneMatrix boneMatrix = ReadMemory<BoneMatrix>(bonesAddress + (sizeof(boneMatrix) * 8)); // head is 8th bone
	return Vector3(boneMatrix.x, boneMatrix.y, boneMatrix.z);
}

ViewMatrix getViewMatrix() {
	return ReadMemory<ViewMatrix>(baseAddress + dwViewMatrix);
}

Vector3 getCoordinatesOnScreen(Vector3 position, ViewMatrix vm) {
	Vector3 out;
	float x = vm.matrix[0] * position.x + vm.matrix[1] * position.y + vm.matrix[2] * position.z + vm.matrix[3];
	float y = vm.matrix[4] * position.x + vm.matrix[5] * position.y + vm.matrix[6] * position.z + vm.matrix[7];
	float z = vm.matrix[12] * position.x + vm.matrix[13] * position.y + vm.matrix[14] * position.z + vm.matrix[15];
	x *= 1.f / z;
	y *= 1.f / z;

	out.x = (SCREEN_WIDTH * .5f) + 0.5f * x * SCREEN_WIDTH + 0.5f;
	out.y = (SCREEN_HEIGHT * .5f) - 0.5f * y * SCREEN_HEIGHT + 0.5f;
	out.z = z;

	return out;
}

float pythagorean(float x1, float y1, float x2, float y2) {
	return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

int getClosestEnemyIndex() {
	float closestDistenceFromCrosshair = FLT_MAX;
	int closestEnemyIndex = 0;
	
	int allyTeam = getTeam(getCurrentPlayerAddress());

	for (int i = 1; i < 64; i++) {
		uintptr_t playerAddress = getPlayerAddress(i);
		
		if (allyTeam == getTeam(playerAddress)) continue;
		if (isNotPlayer(playerAddress)) continue;
		
		int playerHealth = getPlayerHealth(playerAddress);
		if (playerHealth < 1 || playerHealth > 100) continue;

		Vector3 rawHead = getPlayerHead(playerAddress);
		viewMatrix = getViewMatrix();
		Vector3 playerHead = getCoordinatesOnScreen(rawHead, viewMatrix);

		float distanceFromCrosshair = pythagorean(playerHead.x, playerHead.y, CROSSHAIR_X, CROSSHAIR_Y);

		if (distanceFromCrosshair < closestDistenceFromCrosshair) {
			closestDistenceFromCrosshair = distanceFromCrosshair;
			closestEnemyIndex = i;
		}
	}
	return closestEnemyIndex;
}

void aimbotLoop() {
	while(true) {
		closestEnemy = getClosestEnemyIndex();
		Vector3 rawHead = getPlayerHead(getPlayerAddress(closestEnemy));
		Vector3 closestHead = getCoordinatesOnScreen(rawHead, viewMatrix);
		float distanceFromCroshair = pythagorean(closestHead.x, closestHead.y, CROSSHAIR_X, CROSSHAIR_Y);
		if (GetAsyncKeyState(VK_LBUTTON) && distanceFromCroshair < 150) {
			SetCursorPos(closestHead.x, closestHead.y);
		}
	}
}

int main() {
	window = FindWindowA(NULL, "Counter-Strike: Global Offensive");
	GetWindowThreadProcessId(window, &processId);
	
	baseAddress = getBaseMemoryAddress();

	if (baseAddress == NULL) {
		printf("Najpierw uruchom CS:GO");
		return 0;
	}

	csgoProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, processId);

	CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)aimbotLoop, NULL, NULL, NULL);

	while (!GetAsyncKeyState(VK_END)) {
		
	}
}
