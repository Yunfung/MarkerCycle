///----------------------------------------------------------------------------------------------------
/// MarkerCycle (一键循环标记) - Nexus addon for Guild Wars 2
///
/// 可以配多个循环，每个循环一个触发键，各自按顺序执行自己那份列表：
///   第 1 项 -> 第 2 项 -> … -> 最后一项 -> 回到第 1 项
/// 列表可增删、可排序、数量不限，每一项可以是：
///   * 游戏动作（团队标记 1-8、标记目标、清除标记、翻滚、技能……）——走 Nexus 的 GameBinds，
///     与玩家自己把该动作绑到哪个键无关，因此自定义快捷键后依然生效；
///   * 按键（任意键 + Ctrl/Alt/Shift）——走 Nexus 的 WndProc_SendToGameOnly，
///     国产客户端会拦 SendInput，只有这条路径能把按键送进游戏。
///----------------------------------------------------------------------------------------------------

#include <Windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "Shell32.lib") // 配置管理里用 SHGetFolderPathW 定位桌面

#include "cycle.h"
#include "gamebinds.inc"
#include "imgui/imgui.h"
#include "nexus/Nexus.h"

using namespace markercycle;

namespace
{
constexpr const char*    ADDON_NAME        = "一键循环标记";
constexpr const char*    ADDON_AUTHOR      = "摸鱼哥";
constexpr const char*    ADDON_DESCRIPTION = "8 个循环常驻、各自独立：每个循环一个触发键；轻按走一项，开启长按连发后按住不放会按自定义间隔连续执行。";
constexpr const char*    LOG_CHANNEL       = "MarkerCycle";
constexpr const char*    BIND_DEFAULT      = "CTRL+E";
constexpr const char*    CONFIG_FOLDER     = "MarkerCycle";
constexpr const char*    CONFIG_FILENAME   = "MarkerCycle.ini";
constexpr uint32_t       ADDON_SIGNATURE   = 1296236545u; /* 0x4D430001，全局唯一 */

constexpr int DEFAULT_PRESS_MS = 50;  /* 动作/按键的按下时长 */
constexpr int MIN_PRESS_MS     = 5;
constexpr int MAX_PRESS_MS     = 500;
constexpr int MIN_CYCLE_GAP_MS = 100; /* 防止同一个物理按键重复触发 */
constexpr int MAX_ACTIONS      = 512; /* 单个循环的列表长度上限 */

//----------------------------------------------------------------------------------------------------
// 全局状态
//----------------------------------------------------------------------------------------------------

AddonAPI_t*              g_api = nullptr;
std::wstring             g_iniPath;
std::vector<Cycle>       g_cycles;
std::vector<std::string> g_bindIds; ///< 与 g_cycles 一一对应，保存稳定的绑定标识字符串
int                      g_activeCycle = 0;

int   g_pressMs           = DEFAULT_PRESS_MS;
int   g_resetAfterSeconds = 0;
bool  g_showAlert         = false;

bool  g_holdRepeat     = false;                   // 长按连发开关
int   g_repeatDelayMs  = kDefaultRepeatDelayMs;   // 按住多久后开始连发
int   g_repeatIntervalMs = kDefaultRepeatIntervalMs; // 连发间隔

int   g_captureIndex = -1;    // 正在录制按键的行号（当前循环内），-1 表示没有录制
bool  g_captureArmed = false; // 录制前要求所有键先松开，避免把“点击录制”那一下当成按键
bool  g_dirty        = false; // 有未写盘的修改

HWND  g_gameWindow = nullptr;

char  g_nameEdit[64] = {};
int   g_nameEditSlot = -1; // 名字输入框里当前是哪个槽位的内容
char  g_bindFilter[128] = {};

//----------------------------------------------------------------------------------------------------
// 编码 / 日志小工具
//----------------------------------------------------------------------------------------------------

std::wstring Widen(const char* aText)
{
	if (aText == nullptr || *aText == '\0')
	{
		return std::wstring();
	}

	const int length = MultiByteToWideChar(CP_UTF8, 0, aText, -1, nullptr, 0);
	if (length <= 0)
	{
		return std::wstring();
	}

	std::wstring result(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, aText, -1, result.data(), length);
	result.resize(static_cast<std::size_t>(length) - 1);
	return result;
}

std::string Narrow(const std::wstring& aText)
{
	if (aText.empty())
	{
		return std::string();
	}

	const int length = WideCharToMultiByte(CP_UTF8, 0, aText.c_str(), static_cast<int>(aText.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return std::string();
	}

	std::string result(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, aText.c_str(), static_cast<int>(aText.size()), result.data(), length, nullptr, nullptr);
	return result;
}

/// INI 读出来的是 Unicode，转成 UTF-8 给逻辑层用
std::string ToUtf8(const std::wstring& aText)
{
	return Narrow(aText);
}

void Log(ELogLevel aLevel, const char* aMessage)
{
	if (g_api != nullptr && g_api->Log != nullptr)
	{
		g_api->Log(aLevel, LOG_CHANNEL, aMessage);
	}
}

int Clamp(int aValue, int aMin, int aMax)
{
	if (aValue < aMin)
	{
		return aMin;
	}
	if (aValue > aMax)
	{
		return aMax;
	}
	return aValue;
}

//----------------------------------------------------------------------------------------------------
// 配置文件
//----------------------------------------------------------------------------------------------------

std::wstring GetConfigDirectory()
{
	std::wstring gameDir = Widen(g_api->Paths_GetGameDirectory());
	while (!gameDir.empty() && (gameDir.back() == L'\\' || gameDir.back() == L'/'))
	{
		gameDir.pop_back();
	}

	gameDir += L"\\addons\\";
	gameDir += Widen(CONFIG_FOLDER);
	CreateDirectoryW(gameDir.c_str(), nullptr); // 目录已存在时返回 FALSE，忽略即可

	return gameDir;
}

std::wstring ReadIniString(const wchar_t* aSection, const wchar_t* aKey, const wchar_t* aDefault)
{
	wchar_t buffer[1024] = {};
	GetPrivateProfileStringW(aSection, aKey, aDefault, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), g_iniPath.c_str());
	return std::wstring(buffer);
}

int ReadIniInt(const wchar_t* aSection, const wchar_t* aKey, int aDefault)
{
	return static_cast<int>(GetPrivateProfileIntW(aSection, aKey, aDefault, g_iniPath.c_str()));
}

/// 以系统 ANSI 写文件（与 INI API 的读写编码保持一致，中文注释/名字不会变成乱码）
void WriteFileAcp(const std::wstring& aPath, const std::wstring& aContent)
{
	const int length = WideCharToMultiByte(CP_ACP, 0, aContent.c_str(), static_cast<int>(aContent.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0)
	{
		return;
	}

	std::string bytes(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_ACP, 0, aContent.c_str(), static_cast<int>(aContent.size()), bytes.data(), length, nullptr, nullptr);

	HANDLE file = CreateFileW(aPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		Log(LOGL_WARNING, "无法写入配置文件。");
		return;
	}

	DWORD written = 0;
	WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
	CloseHandle(file);
}

/// 把内存里的设置和所有循环写回 INI（面板上每次改动后立即调用）
void SaveConfig()
{
	// 文本由 cycle.h 里的 SerializeConfig 生成（纯逻辑，单元测试可覆盖）
	const std::string text = SerializeConfig(g_cycles, g_pressMs, g_resetAfterSeconds, g_showAlert != 0,
	                                        g_holdRepeat, g_repeatDelayMs, g_repeatIntervalMs);
	WriteFileAcp(g_iniPath, Widen(text.c_str()));
}

/// 读某个小节里的 ActionCount / ActionN；返回 false 表示这一节没有列表
bool ReadActions(const wchar_t* aSection, std::vector<Action>& aOut)
{
	aOut.clear();

	const std::wstring countText = ReadIniString(aSection, L"ActionCount", L"");
	if (countText.empty())
	{
		return false;
	}

	const int count = Clamp(_wtoi(countText.c_str()), 0, MAX_ACTIONS);
	for (int i = 1; i <= count; ++i)
	{
		wchar_t key[32] = {};
		std::swprintf(key, sizeof(key) / sizeof(key[0]), L"Action%d", i);

		const std::string raw = ToUtf8(ReadIniString(aSection, key, L""));
		Action action;
		if (ParseAction(raw.c_str(), action))
		{
			aOut.push_back(action);
		}
		else
		{
			const std::string message = "配置里有无法识别的列表项，已跳过：" + raw;
			Log(LOGL_WARNING, message.c_str());
		}
	}
	return true;
}

void LoadConfig()
{
	g_iniPath = GetConfigDirectory() + L"\\" + Widen(CONFIG_FILENAME);

	g_cycles.clear();
	g_pressMs           = DEFAULT_PRESS_MS;
	g_resetAfterSeconds = 0;
	g_showAlert         = false;
	g_holdRepeat        = false;
	g_repeatDelayMs     = kDefaultRepeatDelayMs;
	g_repeatIntervalMs  = kDefaultRepeatIntervalMs;

	if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		Cycle first = MakeDefaultCycle(0);
		first.slot  = 1;
		g_cycles.push_back(first);
		EnsureCycleSlots(g_cycles, kMaxCycles);
		SaveConfig();
		return;
	}

	g_pressMs           = Clamp(ReadIniInt(L"MarkerCycle", L"PressDurationMs", DEFAULT_PRESS_MS), MIN_PRESS_MS, MAX_PRESS_MS);
	g_resetAfterSeconds = Clamp(ReadIniInt(L"MarkerCycle", L"ResetAfterSeconds", 0), 0, 86400);
	g_showAlert         = ReadIniInt(L"MarkerCycle", L"ShowAlert", 0) != 0;
	g_holdRepeat        = ReadIniInt(L"MarkerCycle", L"HoldRepeat", 0) != 0;
	g_repeatDelayMs     = ClampRepeatDelay(ReadIniInt(L"MarkerCycle", L"RepeatDelayMs", kDefaultRepeatDelayMs));
	g_repeatIntervalMs  = ClampRepeatInterval(ReadIniInt(L"MarkerCycle", L"RepeatIntervalMs", kDefaultRepeatIntervalMs));

	// 固定 8 个循环：每个 [CycleN] 小节就是一个循环，缺的小节 = 空列表
	for (int slot = 1; slot <= kMaxCycles; ++slot)
	{
		const std::wstring section     = Widen(CycleSectionName(slot).c_str());
		const std::wstring defaultName = Widen(DefaultCycleName(slot - 1).c_str());

		Cycle cycle;
		cycle.slot = slot;
		cycle.name = ToUtf8(ReadIniString(section.c_str(), L"Name", defaultName.c_str()));
		ReadActions(section.c_str(), cycle.actions);
		g_cycles.push_back(cycle);
	}

	// 老配置（v1 的 Sequence+TargetMode、v2 的单个 [MarkerCycle] 列表）统统搬进循环1
	bool anyList = false;
	for (std::size_t i = 0; i < g_cycles.size(); ++i)
	{
		if (!g_cycles[i].actions.empty())
		{
			anyList = true;
			break;
		}
	}

	if (!anyList)
	{
		std::vector<Action> legacy;
		if (ReadActions(L"MarkerCycle", legacy))
		{
			Log(LOGL_INFO, "检测到旧版配置（只有一个列表），已迁移到循环1。");
		}
		else
		{
			const std::string sequenceText = ToUtf8(ReadIniString(L"MarkerCycle", L"Sequence", L""));
			const std::vector<uint8_t> sequence = ParseSequence(sequenceText.c_str());
			if (!sequence.empty())
			{
				const int targetMode = ParseTargetMode(ToUtf8(ReadIniString(L"MarkerCycle", L"TargetMode", L"auto")).c_str());
				const int baseBind = (targetMode == kTargetModeAgent) ? kSquadMarkerAgentBase : kSquadMarkerWorldBase;
				legacy = ActionsFromSequence(sequence, baseBind);
				Log(LOGL_INFO, "检测到 v1 配置，已迁移到循环1。");
			}
		}

		if (!legacy.empty())
		{
			g_cycles[0].actions = legacy;
			g_cycles[0].cursor  = 0;
			if (g_cycles[0].name.empty())
			{
				g_cycles[0].name = DefaultCycleName(0);
			}
		}
	}

	EnsureCycleSlots(g_cycles, kMaxCycles);

	for (std::size_t i = 0; i < g_cycles.size(); ++i)
	{
		ClampCursor(g_cycles[i]);
	}

	SaveConfig(); // 统一格式（顺便把旧版配置升级成新格式）
}

//----------------------------------------------------------------------------------------------------
// 发送按键（走 Nexus 的 WndProc_SendToGameOnly，国产客户端只认这条路径）
//----------------------------------------------------------------------------------------------------

bool IsExtendedKey(int aVirtualKey)
{
	switch (aVirtualKey)
	{
	case VK_RMENU:
	case VK_RCONTROL:
	case VK_INSERT:
	case VK_DELETE:
	case VK_HOME:
	case VK_END:
	case VK_PRIOR:
	case VK_NEXT:
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
	case VK_NUMLOCK:
	case VK_DIVIDE:
	case VK_SNAPSHOT:
		return true;
	default:
		return false;
	}
}

LPARAM MakeKeyLParam(int aVirtualKey, bool aIsUp)
{
	const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(aVirtualKey), MAPVK_VK_TO_VSC);
	LPARAM lParam = 1 | (static_cast<LPARAM>(scanCode) << 16);
	if (IsExtendedKey(aVirtualKey))
	{
		lParam |= (1 << 24);
	}
	if (aIsUp)
	{
		lParam |= (1 << 30);  // previous key state
		lParam |= (1L << 31); // transition state
	}
	return lParam;
}

BOOL CALLBACK FindGameWindowProc(HWND aWindow, LPARAM aParam)
{
	wchar_t className[64] = {};
	GetClassNameW(aWindow, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
	if (wcscmp(className, L"ArenaNet_Dx_Window_Class") != 0)
	{
		return TRUE;
	}

	DWORD processId = 0;
	GetWindowThreadProcessId(aWindow, &processId);
	if (processId != GetCurrentProcessId())
	{
		return TRUE;
	}

	*reinterpret_cast<HWND*>(aParam) = aWindow;
	return FALSE;
}

HWND GameWindow()
{
	if (g_gameWindow != nullptr && IsWindow(g_gameWindow))
	{
		return g_gameWindow;
	}

	g_gameWindow = nullptr;
	EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&g_gameWindow));
	if (g_gameWindow == nullptr)
	{
		g_gameWindow = GetForegroundWindow(); // 兜底
	}
	return g_gameWindow;
}

void PostKey(HWND aWindow, int aVirtualKey, bool aIsUp, bool aIsSys)
{
	const UINT message = aIsUp ? (aIsSys ? WM_SYSKEYUP : WM_KEYUP) : (aIsSys ? WM_SYSKEYDOWN : WM_KEYDOWN);
	g_api->WndProc_SendToGameOnly(aWindow, message, static_cast<WPARAM>(aVirtualKey), MakeKeyLParam(aVirtualKey, aIsUp));
}

void SendKey(const Action& aAction)
{
	if (g_api->WndProc_SendToGameOnly == nullptr)
	{
		Log(LOGL_WARNING, "Nexus 未提供 WndProc_SendToGameOnly，无法发送按键。");
		return;
	}

	const HWND window = GameWindow();
	if (window == nullptr)
	{
		Log(LOGL_WARNING, "找不到游戏窗口，无法发送按键。");
		return;
	}

	if (aAction.shift)
	{
		PostKey(window, VK_SHIFT, false, false);
	}
	if (aAction.ctrl)
	{
		PostKey(window, VK_CONTROL, false, false);
	}
	if (aAction.alt)
	{
		PostKey(window, VK_MENU, false, true);
	}

	PostKey(window, aAction.value, false, aAction.alt);
	PostKey(window, aAction.value, true, aAction.alt);

	if (aAction.alt)
	{
		PostKey(window, VK_MENU, true, true);
	}
	if (aAction.ctrl)
	{
		PostKey(window, VK_CONTROL, true, false);
	}
	if (aAction.shift)
	{
		PostKey(window, VK_SHIFT, true, false);
	}
}

//----------------------------------------------------------------------------------------------------
// 执行
//----------------------------------------------------------------------------------------------------

std::string KeyName(int aVirtualKey)
{
	wchar_t name[64] = {};
	const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(aVirtualKey), MAPVK_VK_TO_VSC);
	LONG lParam = static_cast<LONG>(scanCode << 16);
	if (IsExtendedKey(aVirtualKey))
	{
		lParam |= (1 << 24);
	}

	if (GetKeyNameTextW(lParam, name, static_cast<int>(sizeof(name) / sizeof(name[0]))) > 0)
	{
		return Narrow(std::wstring(name));
	}

	char buffer[32] = {};
	std::snprintf(buffer, sizeof(buffer), "VK 0x%02X", aVirtualKey);
	return std::string(buffer);
}

std::string DescribeAction(const Action& aAction)
{
	if (aAction.kind == kActionBind)
	{
		return std::string(BindDisplay(FindBindEntry(aAction.value)));
	}

	std::string text;
	if (aAction.ctrl)
	{
		text += "Ctrl+";
	}
	if (aAction.alt)
	{
		text += "Alt+";
	}
	if (aAction.shift)
	{
		text += "Shift+";
	}
	text += KeyName(aAction.value);
	return text;
}

void ExecuteAction(const Action& aAction)
{
	if (aAction.kind == kActionBind)
	{
		g_api->GameBinds_InvokeAsync(static_cast<EGameBinds>(aAction.value), g_pressMs);
	}
	else
	{
		SendKey(aAction);
	}
}

//----------------------------------------------------------------------------------------------------
// 绑定：每个循环一个标识
//----------------------------------------------------------------------------------------------------

void OnBindEvent(const char* aIdentifier, bool aIsRelease);

int FindCycleByIdentifier(const char* aIdentifier)
{
	if (aIdentifier == nullptr)
	{
		return -1;
	}

	for (std::size_t i = 0; i < g_bindIds.size(); ++i)
	{
		if (g_bindIds[i] == aIdentifier)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

void RegisterCycleBind(int aIndex)
{
	if (aIndex < 0 || aIndex >= static_cast<int>(g_cycles.size()) || aIndex >= static_cast<int>(g_bindIds.size()))
	{
		return;
	}

	const std::string identifier = CycleBindIdentifier(g_cycles[static_cast<std::size_t>(aIndex)].slot - 1);
	g_bindIds[static_cast<std::size_t>(aIndex)] = identifier;

	// 第一个循环给个默认键，后面的留空（交给玩家自己在 Nexus 输入绑定里设，避免互相抢键）
	const char* defaultKey = (aIndex == 0) ? BIND_DEFAULT : "";
	g_api->InputBinds_RegisterWithString(g_bindIds[static_cast<std::size_t>(aIndex)].c_str(), OnBindEvent, defaultKey);
}

void DeregisterCycleBind(int aIndex)
{
	if (aIndex < 0 || aIndex >= static_cast<int>(g_bindIds.size()))
	{
		return;
	}

	if (!g_bindIds[static_cast<std::size_t>(aIndex)].empty())
	{
		g_api->InputBinds_Deregister(g_bindIds[static_cast<std::size_t>(aIndex)].c_str());
	}
	g_bindIds[static_cast<std::size_t>(aIndex)].clear();
}

/// 执行当前循环的“下一项”，并把游标推进一格（按下、长按连发都走这里）
void FireNext(Cycle& aCycle)
{
	if (aCycle.actions.empty())
	{
		return;
	}

	const ULONGLONG now = GetTickCount64();

	// 可选：空闲一段时间后回到第一项
	if (g_resetAfterSeconds > 0 && aCycle.lastPress != 0 &&
		now - aCycle.lastPress > static_cast<unsigned long long>(g_resetAfterSeconds) * 1000ull)
	{
		aCycle.cursor = 0;
	}
	aCycle.lastPress = now;

	const std::size_t position = aCycle.cursor % aCycle.actions.size();
	Action action;
	if (!TakeNextAction(aCycle, action))
	{
		return;
	}

	ExecuteAction(action);

	if (g_showAlert && g_api->GUI_SendAlert != nullptr)
	{
		char message[128] = {};
		// Nexus 的提示条字体不一定有中文字形，这里用 ASCII，保证一定能看清是哪个循环第几项
		std::snprintf(message, sizeof(message), "MarkerCycle cycle%d %u/%u",
			aCycle.slot, static_cast<unsigned>(position + 1), static_cast<unsigned>(aCycle.actions.size()));
		g_api->GUI_SendAlert(message);
	}
}

void OnBindEvent(const char* aIdentifier, bool aIsRelease)
{
	if (g_api == nullptr)
	{
		return;
	}

	const int index = FindCycleByIdentifier(aIdentifier);
	if (index < 0)
	{
		return;
	}

	Cycle& cycle = g_cycles[static_cast<std::size_t>(index)];

	if (aIsRelease)
	{
		cycle.held = false; // 松手：停止长按连发（这一次的进度保留）
		return;
	}

	if (cycle.actions.empty())
	{
		Log(LOGL_WARNING, "这个循环的列表是空的，请先在 Nexus 插件面板里添加列表项。");
		return;
	}

	const ULONGLONG now = GetTickCount64();
	if (now - cycle.lastPress < static_cast<unsigned long long>(MIN_CYCLE_GAP_MS))
	{
		return; // 系统的按键自动重复，不当作新的一次
	}

	FireNext(cycle);

	// 长按连发：从这一刻开始计时，按住达到起手延迟后由 Tick 继续推进
	cycle.held     = g_holdRepeat;
	cycle.holdNext = NextRepeatTime(now, g_repeatDelayMs);
}

/// 每帧调用（Nexus 的 RT_Render），负责长按连发的时间推进
void TickRender()
{
	if (!g_holdRepeat || g_api == nullptr)
	{
		return;
	}

	const ULONGLONG now = GetTickCount64();
	for (std::size_t i = 0; i < g_cycles.size(); ++i)
	{
		Cycle& cycle = g_cycles[i];
		if (cycle.actions.empty() || !ShouldRepeat(cycle.held, g_holdRepeat, now, cycle.holdNext))
		{
			continue;
		}

		FireNext(cycle);
		cycle.holdNext = NextRepeatTime(now, g_repeatIntervalMs);
	}
}

//----------------------------------------------------------------------------------------------------
// 面板（Nexus 插件列表 -> 设置）
//----------------------------------------------------------------------------------------------------

bool ContainsIgnoreCase(const std::string& aHaystack, const std::string& aNeedleLower)
{
	if (aNeedleLower.empty())
	{
		return true;
	}
	const std::string haystack = ToLower(aHaystack);
	return haystack.find(aNeedleLower) != std::string::npos;
}

bool MatchesFilter(const BindEntry& aEntry, const std::string& aNeedleLower)
{
	if (aNeedleLower.empty())
	{
		return true;
	}
	if (ContainsIgnoreCase(aEntry.Name != nullptr ? aEntry.Name : "", aNeedleLower))
	{
		return true;
	}
	if (aEntry.Chinese != nullptr && std::string(aEntry.Chinese).find(aNeedleLower) != std::string::npos)
	{
		return true;
	}
	return false;
}

void DrawBindCombo(Action& aAction)
{
	const BindEntry* current = FindBindEntry(aAction.value);

	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("##bind", BindDisplay(current), ImGuiComboFlags_HeightLarge))
	{
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##filter", "筛选：标记 / 技能 / 翻滚 …", g_bindFilter, sizeof(g_bindFilter));

		ImGui::BeginChild("##bindlist", ImVec2(0.0f, 300.0f), true);
		const std::string needle = ToLower(Trim(std::string(g_bindFilter)));
		for (int i = 0; i < kGameBindCount; ++i)
		{
			const BindEntry& entry = kGameBinds[i];
			if (!MatchesFilter(entry, needle))
			{
				continue;
			}

			ImGui::PushID(i);
			const bool selected = (entry.Value == aAction.value);
			if (ImGui::Selectable(BindDisplay(&entry), selected))
			{
				aAction.value = entry.Value;
				aAction.kind  = kActionBind;
				g_dirty       = true;
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::EndCombo();
	}
}

bool AnyKeyDown()
{
	for (int vk = 0x08; vk <= 0xFE; ++vk)
	{
		if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
		{
			continue;
		}
		if ((GetAsyncKeyState(vk) & 0x8000) != 0)
		{
			return true;
		}
	}
	return false;
}

bool IsModifierKey(int aVirtualKey)
{
	switch (aVirtualKey)
	{
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU:
		return true;
	default:
		return false;
	}
}

void PollKeyCapture()
{
	if (g_captureIndex < 0)
	{
		return;
	}

	if (g_activeCycle < 0 || g_activeCycle >= static_cast<int>(g_cycles.size()))
	{
		g_captureIndex = -1;
		return;
	}

	Cycle& cycle = g_cycles[static_cast<std::size_t>(g_activeCycle)];
	if (static_cast<std::size_t>(g_captureIndex) >= cycle.actions.size())
	{
		g_captureIndex = -1;
		return;
	}

	if (!g_captureArmed)
	{
		if (!AnyKeyDown())
		{
			g_captureArmed = true;
		}
		return;
	}

	for (int vk = 0x08; vk <= 0xFE; ++vk)
	{
		if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2 || IsModifierKey(vk))
		{
			continue;
		}
		if ((GetAsyncKeyState(vk) & 0x8000) == 0)
		{
			continue;
		}

		if (vk == VK_ESCAPE)
		{
			g_captureIndex = -1;
			return;
		}

		Action& action = cycle.actions[static_cast<std::size_t>(g_captureIndex)];
		action.kind  = kActionKey;
		action.value = vk;
		action.ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		action.alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
		action.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		g_dirty        = true;
		g_captureIndex = -1;
		return;
	}
}

void StartCapture(int aIndex)
{
	g_captureIndex = aIndex;
	g_captureArmed = false;
}

/// 新建一个循环（槽位取最小的空槽），列表用传入的初始内容
void DrawActionRow(Cycle& aCycle, int aIndex)
{
	Action& action = aCycle.actions[static_cast<std::size_t>(aIndex)];
	ImGui::PushID(aIndex);

	char indexLabel[24] = {};
	std::snprintf(indexLabel, sizeof(indexLabel), "%d.", aIndex + 1);
	ImGui::TextUnformatted(indexLabel);
	ImGui::SameLine();

	int kind = action.kind;
	ImGui::SetNextItemWidth(110.0f);
	if (ImGui::Combo("##kind", &kind, "游戏动作\0按键\0"))
	{
		action.kind = kind;
		if (kind == kActionBind && FindBindEntry(action.value) == nullptr)
		{
			action.value = kSquadMarkerWorldBase;
		}
		if (kind == kActionKey && (action.value <= 0 || action.value > 255))
		{
			action.value = VK_F1;
		}
		g_dirty = true;
	}
	ImGui::SameLine();

	if (action.kind == kActionBind)
	{
		DrawBindCombo(action);
	}
	else
	{
		ImGui::TextUnformatted(DescribeAction(action).c_str());
		ImGui::SameLine();
		if (g_captureIndex == aIndex)
		{
			ImGui::TextUnformatted("[ 请按下要用的键，Esc 取消 ]");
		}
		else if (ImGui::SmallButton("录制"))
		{
			StartCapture(aIndex);
		}
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("试"))
	{
		ExecuteAction(action);
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("上") && aIndex > 0)
	{
		std::swap(aCycle.actions[static_cast<std::size_t>(aIndex)], aCycle.actions[static_cast<std::size_t>(aIndex - 1)]);
		g_dirty = true;
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("下") && aIndex + 1 < static_cast<int>(aCycle.actions.size()))
	{
		std::swap(aCycle.actions[static_cast<std::size_t>(aIndex)], aCycle.actions[static_cast<std::size_t>(aIndex + 1)]);
		g_dirty = true;
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("删"))
	{
		aCycle.actions.erase(aCycle.actions.begin() + aIndex);
		ClampCursor(aCycle);
		g_dirty = true;
	}

	ImGui::PopID();
}

//----------------------------------------------------------------------------------------------------
// 配置管理（照搬 GW2-QuickChat 那套：profiles 目录 + 保存/加载/删除/重命名/导入/导出）
//----------------------------------------------------------------------------------------------------

std::string ConfigDirUtf8()
{
	const std::string path = ToUtf8(g_iniPath);
	const std::size_t sep  = path.find_last_of('\\');
	return (sep == std::string::npos) ? std::string(".") : path.substr(0, sep);
}

std::string ProfilePathUtf8(const std::string& aName)
{
	return ConfigDirUtf8() + "\\profiles\\" + aName + ".ini";
}

std::string TimestampSuffix()
{
	SYSTEMTIME st = {};
	GetLocalTime(&st);

	char buffer[48] = {};
	std::snprintf(buffer, sizeof(buffer), "-%04u%02u%02u-%02u%02u%02u",
		static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay),
		static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));
	return std::string(buffer);
}

/// 去首尾空白、去成对的引号（拖拽目录进来时 Windows 会加引号）
std::string CleanPath(const char* aText)
{
	std::string p = (aText == nullptr) ? std::string() : std::string(aText);
	while (!p.empty() && (p.back() == ' ' || p.back() == '\t' || p.back() == '\r' || p.back() == '\n'))
	{
		p.pop_back();
	}

	const std::size_t begin = p.find_first_not_of(" \t");
	if (begin == std::string::npos)
	{
		return std::string();
	}
	p = p.substr(begin);

	if (p.size() >= 2 && p.front() == '"' && p.back() == '"')
	{
		p = p.substr(1, p.size() - 2);
	}
	return p;
}

bool PathExistsUtf8(const std::string& aPath)
{
	return GetFileAttributesW(Widen(aPath.c_str()).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool PathIsDirUtf8(const std::string& aPath)
{
	const DWORD attributes = GetFileAttributesW(Widen(aPath.c_str()).c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void MakeDirUtf8(const std::string& aPath)
{
	CreateDirectoryW(Widen(aPath.c_str()).c_str(), nullptr);
}

bool CopyFileUtf8(const std::string& aFrom, const std::string& aTo)
{
	return CopyFileW(Widen(aFrom.c_str()).c_str(), Widen(aTo.c_str()).c_str(), FALSE) != FALSE;
}

bool DeleteFileUtf8(const std::string& aPath)
{
	return DeleteFileW(Widen(aPath.c_str()).c_str()) != FALSE;
}

bool MoveFileUtf8(const std::string& aFrom, const std::string& aTo)
{
	return MoveFileW(Widen(aFrom.c_str()).c_str(), Widen(aTo.c_str()).c_str()) != FALSE;
}

std::string DesktopDirUtf8()
{
	wchar_t buffer[MAX_PATH] = {};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, buffer)))
	{
		return ToUtf8(std::wstring(buffer));
	}
	return std::string();
}

/// 导出/导入路径补全：目录自动补文件名，缺扩展名补 .ini，相对路径放到插件目录
std::string NormalizeConfigPath(const std::string& aRaw)
{
	std::string p = aRaw;
	if (p.empty())
	{
		return p;
	}

	if (PathIsDirUtf8(p) || p.back() == '\\' || p.back() == '/')
	{
		if (p.back() != '\\' && p.back() != '/')
		{
			p += "\\";
		}
		p += "MarkerCycle-config" + TimestampSuffix() + ".ini";
		return p;
	}

	std::size_t sep = p.find_last_of("\\/");
	if (sep == std::string::npos)
	{
		p   = ConfigDirUtf8() + "\\" + p;
		sep = p.find_last_of("\\/");
	}

	const std::string file = p.substr(sep + 1);
	if (file.size() < 4 || _stricmp(file.c_str() + file.size() - 4, ".ini") != 0)
	{
		p += ".ini";
	}

	return p;
}

/// 粗判一个 ini 是不是本插件的配置（导入时用，避免把别的插件配置塞进来）
bool LooksLikeMarkerCycleIni(const std::string& aPath)
{
	HANDLE file = CreateFileW(Widen(aPath.c_str()).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	char buffer[8192] = {};
	DWORD read = 0;
	const bool ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr) != FALSE;
	CloseHandle(file);

	if (!ok)
	{
		return false;
	}

	const std::string text(buffer, read);
	return text.find("[Cycle1]") != std::string::npos || text.find("PressDurationMs=") != std::string::npos;
}

void ListProfiles(std::vector<std::string>& aOut)
{
	aOut.clear();

	const std::string pattern = ConfigDirUtf8() + "\\profiles\\*.ini";
	WIN32_FIND_DATAW data = {};
	HANDLE find = FindFirstFileW(Widen(pattern.c_str()).c_str(), &data);
	if (find == INVALID_HANDLE_VALUE)
	{
		return;
	}

	do
	{
		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			continue;
		}

		const std::string name = ToUtf8(std::wstring(data.cFileName));
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".ini") == 0)
		{
			aOut.push_back(name.substr(0, name.size() - 4));
		}
	} while (FindNextFileW(find, &data) != FALSE);

	FindClose(find);
	std::sort(aOut.begin(), aOut.end());
}

/// 用一份配置文件替换当前配置并重新加载
bool ApplyConfigFile(const std::string& aPath)
{
	if (!CopyFileUtf8(aPath, ToUtf8(g_iniPath)))
	{
		return false;
	}

	g_activeCycle  = 0;
	g_captureIndex = -1;
	g_nameEditSlot = -1;
	LoadConfig();
	g_dirty = false;
	return true;
}

void DrawConfigManager()
{
	static std::vector<std::string> profiles;
	static bool loaded = false;
	static int sel = -1;
	static char nameBuffer[64] = "";
	static char renameBuffer[64] = "";
	static char pathBuffer[260] = "";
	static std::string message;
	static bool wantDelete = false;
	static std::string pendingDelete;

	if (!loaded)
	{
		MakeDirUtf8(ConfigDirUtf8() + "\\profiles");
		ListProfiles(profiles);
		loaded = true;
	}

	ImGui::TextDisabled("当前配置：%s", ToUtf8(g_iniPath).c_str());

	ImGui::SetNextItemWidth(200.0f);
	const char* preview = (sel >= 0 && sel < static_cast<int>(profiles.size())) ? profiles[static_cast<std::size_t>(sel)].c_str() : "（未选择）";
	if (ImGui::BeginCombo("配置列表", preview))
	{
		for (int i = 0; i < static_cast<int>(profiles.size()); ++i)
		{
			if (ImGui::Selectable(profiles[static_cast<std::size_t>(i)].c_str(), sel == i))
			{
				sel = i;
				std::snprintf(renameBuffer, sizeof(renameBuffer), "%s", profiles[static_cast<std::size_t>(i)].c_str());
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("刷新"))
	{
		ListProfiles(profiles);
		if (sel >= static_cast<int>(profiles.size()))
		{
			sel = -1;
		}
	}

	const bool hasSel = (sel >= 0 && sel < static_cast<int>(profiles.size()));

	if (ImGui::Button("保存到选中"))
	{
		if (hasSel)
		{
			SaveConfig();
			MakeDirUtf8(ConfigDirUtf8() + "\\profiles");
			if (CopyFileUtf8(ToUtf8(g_iniPath), ProfilePathUtf8(profiles[static_cast<std::size_t>(sel)])))
			{
				message = "已保存到 " + profiles[static_cast<std::size_t>(sel)];
			}
			else
			{
				message = "保存失败，错误码 " + std::to_string(static_cast<int>(GetLastError()));
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("加载选中"))
	{
		if (hasSel)
		{
			message = ApplyConfigFile(ProfilePathUtf8(profiles[static_cast<std::size_t>(sel)]))
				? ("已加载 " + profiles[static_cast<std::size_t>(sel)])
				: "加载失败";
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("删除选中"))
	{
		if (hasSel)
		{
			pendingDelete = profiles[static_cast<std::size_t>(sel)];
			wantDelete    = true;
		}
	}

	if (wantDelete)
	{
		ImGui::OpenPopup("确认删除配置###mc_delcfg");
		wantDelete = false;
	}
	if (ImGui::BeginPopupModal("确认删除配置###mc_delcfg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("确定删除配置「%s」吗？", pendingDelete.c_str());
		ImGui::TextDisabled("只删除 profiles 目录下的这份配置，当前生效的配置不受影响。");
		ImGui::Spacing();
		if (ImGui::Button("确认删除", ImVec2(100.0f, 0.0f)))
		{
			if (!pendingDelete.empty() && DeleteFileUtf8(ProfilePathUtf8(pendingDelete)))
			{
				message = "已删除 " + pendingDelete;
			}
			else
			{
				message = "删除失败，错误码 " + std::to_string(static_cast<int>(GetLastError()));
			}
			ListProfiles(profiles);
			sel = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("取消", ImVec2(100.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("重命名为", renameBuffer, sizeof(renameBuffer));
	ImGui::SameLine();
	if (ImGui::Button("重命名选中"))
	{
		const std::string name = CleanPath(renameBuffer);
		if (!hasSel)
		{
			message = "请先在列表里选择一个配置";
		}
		else if (name.empty())
		{
			message = "请先填写新名字";
		}
		else if (name == profiles[static_cast<std::size_t>(sel)])
		{
			message = "名字没有变化";
		}
		else if (PathExistsUtf8(ProfilePathUtf8(name)))
		{
			message = "已存在同名配置：" + name + "，请换一个名字";
		}
		else if (MoveFileUtf8(ProfilePathUtf8(profiles[static_cast<std::size_t>(sel)]), ProfilePathUtf8(name)))
		{
			ListProfiles(profiles);
			sel = -1;
			for (int i = 0; i < static_cast<int>(profiles.size()); ++i)
			{
				if (profiles[static_cast<std::size_t>(i)] == name)
				{
					sel = i;
					break;
				}
			}
			message = "已重命名为 " + name;
		}
		else
		{
			message = "重命名失败，错误码 " + std::to_string(static_cast<int>(GetLastError()));
		}
	}

	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("新配置名", nameBuffer, sizeof(nameBuffer));
	ImGui::SameLine();
	if (ImGui::Button("另存为新配置"))
	{
		const std::string name = CleanPath(nameBuffer);
		if (!name.empty())
		{
			MakeDirUtf8(ConfigDirUtf8() + "\\profiles");
			SaveConfig();
			if (CopyFileUtf8(ToUtf8(g_iniPath), ProfilePathUtf8(name)))
			{
				ListProfiles(profiles);
				message = "已保存为新配置 " + name;
			}
			else
			{
				message = "保存失败";
			}
		}
		else
		{
			message = "请先填写配置名";
		}
	}

	ImGui::SetNextItemWidth(320.0f);
	ImGui::InputText("文件路径", pathBuffer, sizeof(pathBuffer));
	ImGui::SameLine();
	if (ImGui::Button("导出到该路径"))
	{
		const std::string target = NormalizeConfigPath(CleanPath(pathBuffer));
		if (target.empty())
		{
			message = "请先填写导出路径";
		}
		else
		{
			const std::size_t sep = target.find_last_of("\\/");
			if (sep != std::string::npos)
			{
				MakeDirUtf8(target.substr(0, sep));
			}
			SaveConfig();
			SetLastError(0);
			if (CopyFileUtf8(ToUtf8(g_iniPath), target))
			{
				message = "已导出：" + target;
			}
			else
			{
				message = "导出失败，错误码 " + std::to_string(static_cast<int>(GetLastError())) + "：" + target;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("导出到桌面"))
	{
		const std::string desktop = DesktopDirUtf8();
		if (desktop.empty())
		{
			message = "无法定位桌面目录";
		}
		else
		{
			const std::string target = desktop + "\\MarkerCycle" + TimestampSuffix() + ".ini";
			SaveConfig();
			SetLastError(0);
			if (CopyFileUtf8(ToUtf8(g_iniPath), target))
			{
				message = "已导出到桌面：" + target;
			}
			else
			{
				message = "导出失败，错误码 " + std::to_string(static_cast<int>(GetLastError()));
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("从该路径导入"))
	{
		const std::string source = CleanPath(pathBuffer);
		if (source.empty())
		{
			message = "请先填写导入路径";
		}
		else if (PathIsDirUtf8(source))
		{
			// 目录：把里面所有格式正确的 .ini 全部导入
			const std::string pattern = source + "\\*.ini";
			WIN32_FIND_DATAW data = {};
			HANDLE find = FindFirstFileW(Widen(pattern.c_str()).c_str(), &data);

			int imported = 0;
			int badFormat = 0;
			int failed = 0;
			std::string newestDest;
			ULARGE_INTEGER newestTime = {0, 0};

			MakeDirUtf8(ConfigDirUtf8() + "\\profiles");
			if (find != INVALID_HANDLE_VALUE)
			{
				do
				{
					if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					{
						continue;
					}

					const std::string fileName = ToUtf8(std::wstring(data.cFileName));
					const std::string filePath = source + "\\" + fileName;
					if (!LooksLikeMarkerCycleIni(filePath))
					{
						++badFormat;
						continue;
					}

					std::string base = fileName;
					const std::size_t dot = base.find_last_of('.');
					if (dot != std::string::npos)
					{
						base = base.substr(0, dot);
					}

					const std::string dest = ProfilePathUtf8(base);
					if (CopyFileUtf8(filePath, dest))
					{
						++imported;

						ULARGE_INTEGER stamp;
						stamp.LowPart  = data.ftLastWriteTime.dwLowDateTime;
						stamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
						if (newestDest.empty() || stamp.QuadPart > newestTime.QuadPart)
						{
							newestTime = stamp;
							newestDest = dest;
						}
					}
					else
					{
						++failed;
					}
				} while (FindNextFileW(find, &data) != FALSE);

				FindClose(find);
			}

			if (imported == 0)
			{
				message = "没有可导入的配置（目录内 .ini 格式不符或为空）";
			}
			else
			{
				ListProfiles(profiles);
				if (!newestDest.empty())
				{
					ApplyConfigFile(newestDest);
				}

				char text[160] = {};
				std::snprintf(text, sizeof(text), "已导入 %d 个配置（格式不符 %d，失败 %d）", imported, badFormat, failed);
				message = text;
			}
		}
		else if (!PathExistsUtf8(source))
		{
			message = "文件不存在：" + source;
		}
		else if (!LooksLikeMarkerCycleIni(source))
		{
			message = "不是一键循环标记的配置文件：" + source;
		}
		else
		{
			std::string base = source;
			const std::size_t sep = base.find_last_of("\\/");
			if (sep != std::string::npos)
			{
				base = base.substr(sep + 1);
			}
			const std::size_t dot = base.find_last_of('.');
			if (dot != std::string::npos)
			{
				base = base.substr(0, dot);
			}

			MakeDirUtf8(ConfigDirUtf8() + "\\profiles");
			const std::string dest = ProfilePathUtf8(base);
			if (CopyFileUtf8(source, dest))
			{
				ListProfiles(profiles);
				message = ApplyConfigFile(dest) ? ("已导入并加载 " + base) : "已导入，加载失败";
			}
			else
			{
				message = "导入失败，错误码 " + std::to_string(static_cast<int>(GetLastError()));
			}
		}
	}

	ImGui::TextDisabled("导入可填 .ini 文件或目录：目录内所有格式正确的 .ini 都会导入并保留为配置");
	if (!message.empty())
	{
		ImGui::TextDisabled("%s", message.c_str());
	}
}

void DrawCyclePicker()
{
	ImGui::TextUnformatted("循环");
	ImGui::SameLine();

	const Cycle& current = g_cycles[static_cast<std::size_t>(g_activeCycle)];
	const std::string preview = current.name.empty() ? DefaultCycleName(current.slot - 1) : current.name;

	ImGui::SetNextItemWidth(240.0f);
	if (ImGui::BeginCombo("##cycle", preview.c_str()))
	{
		for (int i = 0; i < static_cast<int>(g_cycles.size()); ++i)
		{
			const Cycle& cycle = g_cycles[static_cast<std::size_t>(i)];
			const std::string name = cycle.name.empty() ? DefaultCycleName(cycle.slot - 1) : cycle.name;

			char label[192] = {};
			if (cycle.actions.empty())
			{
				std::snprintf(label, sizeof(label), "%s（空，还没用）", name.c_str());
			}
			else
			{
				std::snprintf(label, sizeof(label), "%s（%d 项）", name.c_str(), static_cast<int>(cycle.actions.size()));
			}

			ImGui::PushID(i);
			if (ImGui::Selectable(label, i == g_activeCycle))
			{
				g_activeCycle  = i;
				g_captureIndex = -1;
				g_nameEditSlot = -1;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	// 循环固定 8 个，不能新建也不能删除：8 个标识从加载起就一直挂在「输入绑定」里，
	// 想用哪个就在那里给它设一个键，留空的循环按了也不会做事。
	ImGui::SameLine();
	ImGui::TextUnformatted("（8 个循环常驻，都可单独设键）");
}

// 一行设置：左边名称，右边输入框；输入框宽度吃满整列，面板窄也不会把数字裁掉。
static void IntSettingRow(const char* aName, const char* aId, int* aValue, int aStep, int aStepFast, int aMin, int aMax)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(aName);

	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputInt(aId, aValue, aStep, aStepFast))
	{
		*aValue = Clamp(*aValue, aMin, aMax);
		g_dirty = true;
	}
}

static void DrawExecSettings()
{
	if (!ImGui::BeginTable("mc_exec_settings", 2, ImGuiTableFlags_SizingStretchProp))
	{
		return;
	}

	ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.46f);
	ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.54f);

	IntSettingRow("按下时长(ms)", "##press_ms", &g_pressMs, 5, 25, MIN_PRESS_MS, MAX_PRESS_MS);
	IntSettingRow("空闲重置(秒)", "##reset_s", &g_resetAfterSeconds, 5, 30, 0, 86400);

	if (ImGui::Checkbox("执行时屏幕提示第几项", &g_showAlert))
	{
		g_dirty = true;
	}

	if (ImGui::Checkbox("长按连发（按住触发键不放，就按间隔连续执行）", &g_holdRepeat))
	{
		g_dirty = true;
	}

	if (g_holdRepeat)
	{
		IntSettingRow("起手延迟(ms)", "##repeat_delay", &g_repeatDelayMs, 50, 100, kMinRepeatDelayMs, kMaxRepeatDelayMs);
		IntSettingRow("连发间隔(ms)", "##repeat_interval", &g_repeatIntervalMs, 10, 50, kMinRepeatIntervalMs, kMaxRepeatIntervalMs);
	}

	ImGui::EndTable();

	ImGui::TextWrapped("范围：按下时长 5-500ms；空闲重置 0-86400 秒（0 = 不重置）；起手延迟 0-5000ms（0 = 立即）；连发间隔 30-2000ms。");
	if (g_holdRepeat)
	{
		ImGui::TextWrapped("轻按一下只走一项；按住超过“起手延迟”后，每“连发间隔”走一项，松手就停。");
	}
}

void OptionsRender()
{
	if (g_api == nullptr)
	{
		return;
	}

	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(g_api->ImguiContext));
	ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(g_api->ImguiMalloc),
		reinterpret_cast<void (*)(void*, void*)>(g_api->ImguiFree));

	if (g_cycles.empty())
	{
		return;
	}

	g_activeCycle = Clamp(g_activeCycle, 0, static_cast<int>(g_cycles.size()) - 1);

	PollKeyCapture();

	ImGui::TextUnformatted("8 个循环各自独立：按哪个循环的触发键，就只推进哪个循环的列表。");
	ImGui::TextUnformatted("触发键在 Nexus 的「输入绑定」里设置，标识见下：MarkerCycle-循环1、MarkerCycle-循环2 … MarkerCycle-循环8。");
	ImGui::TextUnformatted("每个循环：按一次 = 执行它自己列表里的下一项，走完最后一项回到第一项。");
	ImGui::Separator();

	DrawCyclePicker();
	ImGui::Separator();

	Cycle& current = g_cycles[static_cast<std::size_t>(g_activeCycle)];

	// 名字（切换循环时把输入框内容同步过去）
	if (g_nameEditSlot != current.slot)
	{
		std::snprintf(g_nameEdit, sizeof(g_nameEdit), "%s", current.name.c_str());
		g_nameEditSlot = current.slot;
	}
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::InputText("名称", g_nameEdit, sizeof(g_nameEdit)))
	{
		current.name = g_nameEdit;
		g_dirty     = true;
	}
	ImGui::SameLine();
	ImGui::Text("绑定标识：%s", CycleBindIdentifier(current.slot - 1).c_str());

	ImGui::Text("循环列表：%d 项（数量不限，可增删、可上下移动）", static_cast<int>(current.actions.size()));
	if (current.actions.empty())
	{
		ImGui::TextUnformatted("列表是空的：先点下面的「+ 游戏动作」添加。");
	}

	ImGui::BeginChild("##actions", ImVec2(0.0f, 300.0f), true);
	for (int i = 0; i < static_cast<int>(current.actions.size()); ++i)
	{
		DrawActionRow(current, i);
		ImGui::Separator();
	}
	ImGui::EndChild();

	if (ImGui::Button("+ 游戏动作"))
	{
		Action action;
		action.kind  = kActionBind;
		action.value = kSquadMarkerWorldBase;
		current.actions.push_back(action);
		g_dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("+ 按键"))
	{
		Action action;
		action.kind  = kActionKey;
		action.value = VK_F1;
		current.actions.push_back(action);
		StartCapture(static_cast<int>(current.actions.size()) - 1);
		g_dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("恢复默认（标记1-8）"))
	{
		current.actions = DefaultActions();
		current.cursor  = 0;
		g_dirty         = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("清空"))
	{
		current.actions.clear();
		current.cursor = 0;
		g_dirty        = true;
	}

	ImGui::Separator();
	ImGui::TextUnformatted("执行设置（对所有循环生效）");

	DrawExecSettings();

	ImGui::Separator();
	ImGui::TextUnformatted("配置文件：addons\\MarkerCycle\\MarkerCycle.ini（面板里的改动会立即保存）");
	ImGui::TextUnformatted("「游戏动作」按游戏绑定项触发，你在游戏里改成什么快捷键都照样生效；");
	ImGui::TextUnformatted("「按键」会直接向游戏窗口发送该键（含修饰键），可用于任何没有对应游戏动作的键。");

	ImGui::Separator();
	if (ImGui::CollapsingHeader("配置管理###mc_cfg_section"))
	{
		DrawConfigManager();
	}

	if (g_dirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		SaveConfig();
		g_dirty = false;
	}
}

//----------------------------------------------------------------------------------------------------
// 插件入口
//----------------------------------------------------------------------------------------------------

void AddonLoad(AddonAPI_t* aAPI)
{
	g_api = aAPI;

	ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(g_api->ImguiContext));
	ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(g_api->ImguiMalloc),
		reinterpret_cast<void (*)(void*, void*)>(g_api->ImguiFree));

	LoadConfig();

	g_activeCycle = 0;
	g_nameEditSlot = -1;
	g_captureIndex = -1;

	g_bindIds.assign(g_cycles.size(), std::string());
	for (int i = 0; i < static_cast<int>(g_cycles.size()); ++i)
	{
		RegisterCycleBind(i);
	}

	g_api->GUI_Register(RT_OptionsRender, OptionsRender);
	g_api->GUI_Register(RT_Render, TickRender); // 每帧走一次，负责长按连发的时间推进

	// 检查列表里的游戏动作是否都在游戏里绑了键（没绑的触发时不会有反应）
	{
		char message[768] = {};
		std::snprintf(message, sizeof(message),
			"已加载。%u 个循环各自独立；触发键标识 %s / %s … %s（Nexus 的输入绑定里设置）；按下时长 %dms；长按连发 %s（起手 %dms，间隔 %dms）。",
			static_cast<unsigned>(g_cycles.size()), CycleBindIdentifier(0).c_str(), CycleBindIdentifier(1).c_str(),
			CycleBindIdentifier(static_cast<int>(g_cycles.size()) - 1).c_str(), g_pressMs,
			g_holdRepeat ? "开" : "关", g_repeatDelayMs, g_repeatIntervalMs);
		Log(LOGL_INFO, message);
	}

	for (std::size_t i = 0; i < g_cycles.size(); ++i)
	{
		const Cycle& cycle = g_cycles[i];
		int unbound = 0;
		for (std::size_t a = 0; a < cycle.actions.size(); ++a)
		{
			if (cycle.actions[a].kind == kActionBind && !g_api->GameBinds_IsBound(static_cast<EGameBinds>(cycle.actions[a].value)))
			{
				++unbound;
			}
		}

		if (cycle.actions.empty())
		{
			continue;
		}

		char message[384] = {};
		std::snprintf(message, sizeof(message),
			"  循环%d（%s）：%u 项，标识 %s，其中 %d 个游戏动作没有绑快捷键。",
			cycle.slot, cycle.name.c_str(), static_cast<unsigned>(cycle.actions.size()),
			CycleBindIdentifier(cycle.slot - 1).c_str(), unbound);
		Log(LOGL_INFO, message);

		if (unbound > 0 && unbound == static_cast<int>(cycle.actions.size()))
		{
			char warning[256] = {};
			std::snprintf(warning, sizeof(warning),
				"循环%d 里的游戏动作一个都没在游戏里绑键，触发时不会有任何反应（可在面板里改用「按键」）。", cycle.slot);
			Log(LOGL_WARNING, warning);
		}
	}
}

void AddonUnload()
{
	if (g_api == nullptr)
	{
		return;
	}

	g_api->GUI_Deregister(OptionsRender);
	g_api->GUI_Deregister(TickRender);

	for (int i = 0; i < static_cast<int>(g_bindIds.size()); ++i)
	{
		DeregisterCycleBind(i);
	}
	g_bindIds.clear();

	if (g_dirty)
	{
		SaveConfig();
		g_dirty = false;
	}

	Log(LOGL_INFO, "已卸载。");
	g_api = nullptr;
}
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
	static AddonDefinition_t definition = {};
	definition.Signature   = ADDON_SIGNATURE;
	definition.APIVersion  = NEXUS_API_VERSION;
	definition.Name        = ADDON_NAME;
	definition.Version     = AddonVersion_t{ 1, 5, 2, 0 };
	definition.Author      = ADDON_AUTHOR;
	definition.Description = ADDON_DESCRIPTION;
	definition.Load        = AddonLoad;
	definition.Unload      = AddonUnload;
	definition.Flags       = AF_None; // 允许游戏内热加载/卸载
	definition.Provider    = UP_None;
	definition.UpdateLink  = "";

	return &definition;
}
