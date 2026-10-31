///----------------------------------------------------------------------------------------------------
/// inicheck - 用 Windows 的 INI 接口把插件将要读到的东西原样打印出来
///
/// 目的：不用开游戏就能验证 addons\MarkerCycle\MarkerCycle.ini 的小节头写对了没有。
/// GetPrivateProfileStringW 的 lpAppName 是不带方括号的小节名（"Cycle1"），
/// 文件里必须写成 "[Cycle1]"；写成 "Cycle1" 的话后面的键会被并进上一节。
///
/// 用法：inicheck.exe <ini 路径>
///----------------------------------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

static const wchar_t* g_path = nullptr;

static std::wstring ReadString(const wchar_t* aSection, const wchar_t* aKey, const wchar_t* aDefault)
{
	wchar_t buffer[512] = {};
	GetPrivateProfileStringW(aSection, aKey, aDefault, buffer, sizeof(buffer) / sizeof(buffer[0]), g_path);
	return std::wstring(buffer);
}

int wmain(int argc, wchar_t** argv)
{
	if (argc < 2)
	{
		std::printf("usage: inicheck <ini path>\n");
		return 2;
	}

	g_path = argv[1];
	if (GetFileAttributesW(g_path) == INVALID_FILE_ATTRIBUTES)
	{
		std::printf("找不到文件：%ls\n", g_path);
		return 2;
	}

	std::wprintf(L"文件：%ls\n", g_path);

	int nonEmpty = 0;
	int totalActions = 0;

	for (int slot = 1; slot <= 8; ++slot)
	{
		wchar_t section[32] = {};
		std::swprintf(section, sizeof(section) / sizeof(section[0]), L"Cycle%d", slot);

		const int count = GetPrivateProfileIntW(section, L"ActionCount", -1, g_path);
		if (count < 0)
		{
			std::printf("  循环%d：没有这个节（= 空列表）\n", slot);
			continue;
		}

		const std::wstring name = ReadString(section, L"Name", L"(无名字)");
		std::printf("  循环%d：Name=%ls，ActionCount=%d\n", slot, name.c_str(), count);

		for (int i = 1; i <= count && i <= 4; ++i)
		{
			wchar_t key[32] = {};
			std::swprintf(key, sizeof(key) / sizeof(key[0]), L"Action%d", i);
			const std::wstring value = ReadString(section, key, L"(缺)");
			std::printf("      Action%d=%ls\n", i, value.c_str());
		}
		if (count > 4)
		{
			std::printf("      ……\n");
		}

		if (count > 0)
		{
			++nonEmpty;
			totalActions += count;
		}
	}

	const int press = GetPrivateProfileIntW(L"MarkerCycle", L"PressDurationMs", -1, g_path);
	const int reset = GetPrivateProfileIntW(L"MarkerCycle", L"ResetAfterSeconds", -1, g_path);
	const int alert = GetPrivateProfileIntW(L"MarkerCycle", L"ShowAlert", -1, g_path);
	std::printf("[MarkerCycle] PressDurationMs=%d ResetAfterSeconds=%d ShowAlert=%d\n", press, reset, alert);

	// 旧格式兜底：老版本把列表写在 [MarkerCycle] 里
	const int legacy = GetPrivateProfileIntW(L"MarkerCycle", L"ActionCount", -1, g_path);
	std::printf("旧版 [MarkerCycle] ActionCount=%d（存在说明还会走迁移逻辑）\n", legacy);

	std::printf("结论：%d 个循环有列表，共 %d 项\n", nonEmpty, totalActions);
	return (nonEmpty > 0) ? 0 : 1;
}
