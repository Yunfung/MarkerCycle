///----------------------------------------------------------------------------------------------------
/// iniroundtrip - 端到端验证「插件写出的 INI 文本」能被 Windows 的 INI 接口读回来
///
/// 背景：v1.2 把小节头写成了裸 "Cycle1"（少了方括号），导致循环列表写出去就再也读不回来。
/// 现在写文本的动作在 cycle.h 的 SerializeConfig（有单元测试覆盖格式），这里再补一刀：
/// 真正落盘成 ANSI 文件，再用 GetPrivateProfileStringW/IntW 读，比对循环名和列表项。
///
/// 用法：iniroundtrip.exe [输出路径]
///----------------------------------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/cycle.h"

using namespace markercycle;

static std::wstring Widen(const std::string& aText)
{
	if (aText.empty())
	{
		return std::wstring();
	}

	const int length = MultiByteToWideChar(CP_UTF8, 0, aText.c_str(), -1, nullptr, 0);
	if (length <= 0)
	{
		return std::wstring();
	}

	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, aText.c_str(), -1, result.data(), length);
	result.resize(static_cast<std::size_t>(length) - 1);
	return result;
}

static std::string Narrow(const std::wstring& aText)
{
	if (aText.empty())
	{
		return std::string();
	}

	const int length = WideCharToMultiByte(CP_UTF8, 0, aText.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return std::string();
	}

	std::string result(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, aText.c_str(), -1, result.data(), length, nullptr, nullptr);
	result.resize(static_cast<std::size_t>(length) - 1);
	return result;
}

/// 与插件里的 WriteFileAcp 相同：按系统 ANSI 落盘
static bool WriteAcp(const std::wstring& aPath, const std::wstring& aContent)
{
	const int length = WideCharToMultiByte(CP_ACP, 0, aContent.c_str(), static_cast<int>(aContent.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return false;
	}

	std::string bytes(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_ACP, 0, aContent.c_str(), static_cast<int>(aContent.size()), bytes.data(), length, nullptr, nullptr);

	HANDLE file = CreateFileW(aPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD written = 0;
	const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE;
	CloseHandle(file);
	return ok;
}

static std::wstring ReadString(const wchar_t* aPath, const wchar_t* aSection, const wchar_t* aKey, const wchar_t* aDefault)
{
	wchar_t buffer[512] = {};
	GetPrivateProfileStringW(aSection, aKey, aDefault, buffer, sizeof(buffer) / sizeof(buffer[0]), aPath);
	return std::wstring(buffer);
}

int g_failures = 0;

static void Check(bool aOk, const char* aWhat)
{
	if (!aOk)
	{
		++g_failures;
		std::printf("  FAIL %s\n", aWhat);
	}
}

int wmain(int argc, wchar_t** argv)
{
	const std::wstring path = (argc >= 2) ? argv[1] : L"build\\_roundtrip.ini";

	// 造两个循环：循环1 = 8 个游戏动作，循环2 = 3 个自定义按键（含修饰键）
	std::vector<Cycle> cycles;
	cycles.push_back(MakeDefaultCycle(0));
	EnsureCycleSlots(cycles, kMaxCycles);

	cycles[1].name = "标记目标";
	cycles[1].actions.clear();
	cycles[1].actions.push_back(Action{kActionKey, 0x46, false, false, true}); // shift+F
	cycles[1].actions.push_back(Action{kActionBind, 94, false, false, false});
	cycles[1].actions.push_back(Action{kActionKey, 'Q', true, false, false}); // ctrl+Q

	const std::string text = SerializeConfig(cycles, 80, 5, true);
	if (!WriteAcp(path, Widen(text)))
	{
		std::printf("FAIL 写文件失败：%ls\n", path.c_str());
		return 2;
	}

	std::printf("已写出：%ls（%d 字节 UTF-8）\n", path.c_str(), static_cast<int>(text.size()));

	// 全局设置
	Check(GetPrivateProfileIntW(L"MarkerCycle", L"PressDurationMs", -1, path.c_str()) == 80, "PressDurationMs 读回 80");
	Check(GetPrivateProfileIntW(L"MarkerCycle", L"ResetAfterSeconds", -1, path.c_str()) == 5, "ResetAfterSeconds 读回 5");
	Check(GetPrivateProfileIntW(L"MarkerCycle", L"ShowAlert", -1, path.c_str()) == 1, "ShowAlert 读回 1");

	// 循环1
	Check(GetPrivateProfileIntW(L"Cycle1", L"ActionCount", -1, path.c_str()) == 8, "Cycle1 ActionCount=8");
	Check(ReadString(path.c_str(), L"Cycle1", L"Name", L"") == L"循环1", "Cycle1 Name 读回 循环1");
	Check(ReadString(path.c_str(), L"Cycle1", L"Action1", L"") == L"bind:86", "Cycle1 Action1 读回 bind:86");

	// 循环2
	Check(GetPrivateProfileIntW(L"Cycle2", L"ActionCount", -1, path.c_str()) == 3, "Cycle2 ActionCount=3");
	Check(ReadString(path.c_str(), L"Cycle2", L"Name", L"") == Widen("标记目标"), "Cycle2 Name 读回 标记目标");
	Check(ReadString(path.c_str(), L"Cycle2", L"Action1", L"") == L"key:70+shift", "Cycle2 Action1 读回 key:70+shift");
	Check(ReadString(path.c_str(), L"Cycle2", L"Action2", L"") == L"bind:94", "Cycle2 Action2 读回 bind:94");
	Check(ReadString(path.c_str(), L"Cycle2", L"Action3", L"") == L"key:81+ctrl", "Cycle2 Action3 读回 key:81+ctrl");

	// 循环3..8 应该是空列表小节
	for (int slot = 3; slot <= kMaxCycles; ++slot)
	{
		const std::wstring section = Widen(CycleSectionName(slot));
		Check(GetPrivateProfileIntW(section.c_str(), L"ActionCount", -1, path.c_str()) == 0, "空循环 ActionCount=0");
	}

	if (g_failures == 0)
	{
		std::printf("Round trip OK.\n");
		return 0;
	}

	std::printf("%d check(s) FAILED.\n", g_failures);
	return 1;
}
