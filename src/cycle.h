///----------------------------------------------------------------------------------------------------
/// MarkerCycle / 一键循环标记
/// cycle.h - 纯逻辑（无 Windows 依赖），可脱离游戏单独编译测试。
///----------------------------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace markercycle
{
/// 团队标记一共 8 种：1=箭头 2=圆圈 3=心形 4=方形 5=星星 6=漩涡 7=三角 8=叉
constexpr int kMaxMarker = 8;

/// 游戏内置的团队标记动作在 EGameBinds 里的起始值（与 nexus/Nexus.h 保持一致）
constexpr int kSquadMarkerWorldBase = 86; // GB_SquadMarkerPlaceWorld1
constexpr int kSquadMarkerAgentBase = 94; // GB_SquadMarkerSetAgent1

/// 配置项 TargetMode 的三种取值（v1 遗留，仅用于迁移旧配置）
constexpr int kTargetModeAuto  = -1; // 自动检测
constexpr int kTargetModeWorld = 0;  // 在地面放置标记
constexpr int kTargetModeAgent = 1;  // 标记当前目标

/// 循环列表里一项的动作类型
enum ActionKind
{
	/// 触发游戏内的某个动作：走 Nexus 的 GameBinds。
	/// 它按“游戏绑定项”触发，和玩家自己把该动作绑到哪个键无关，
	/// 所以玩家在游戏里改成自定义快捷键后依然生效，也不会被国服客户端拦模拟输入。
	kActionBind = 0,
	/// 直接向游戏窗口发送一次按键（虚拟键码 + 可选修饰键）。
	kActionKey  = 1,
};

/// 循环列表里的一项
struct Action
{
	int  kind  = kActionBind; ///< ActionKind
	int  value = 0;           ///< kActionBind: EGameBinds 数值；kActionKey: 虚拟键码
	bool ctrl  = false;
	bool alt   = false;
	bool shift = false;
};

//----------------------------------------------------------------------------------------------------
// 字符串小工具
//----------------------------------------------------------------------------------------------------

inline std::string Trim(const std::string& aText)
{
	static const char* const kTrimChars = " \t\r\n";
	const std::size_t first = aText.find_first_not_of(kTrimChars);
	if (first == std::string::npos)
	{
		return std::string();
	}
	const std::size_t last = aText.find_last_not_of(kTrimChars);
	return aText.substr(first, last - first + 1);
}

inline std::string ToLower(const std::string& aText)
{
	std::string result = aText;
	for (std::size_t i = 0; i < result.size(); ++i)
	{
		const char c = result[i];
		if (c >= 'A' && c <= 'Z')
		{
			result[i] = static_cast<char>(c - 'A' + 'a');
		}
	}
	return result;
}

/// 解析十进制或 0x 前缀的十六进制整数
inline bool ParseInt(const std::string& aText, int& aOut)
{
	const std::string text = Trim(aText);
	if (text.empty())
	{
		return false;
	}

	const char* begin = text.c_str();
	char* end = nullptr;
	const int base = (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;
	const long value = std::strtol(begin, &end, base);
	if (end == begin || value < 0 || value > 65535)
	{
		return false;
	}

	aOut = static_cast<int>(value);
	return true;
}

inline std::vector<std::string> Split(const std::string& aText, char aSeparator)
{
	std::vector<std::string> parts;
	std::string current;
	for (std::size_t i = 0; i < aText.size(); ++i)
	{
		if (aText[i] == aSeparator)
		{
			parts.push_back(current);
			current.clear();
			continue;
		}
		current.push_back(aText[i]);
	}
	parts.push_back(current);
	return parts;
}

//----------------------------------------------------------------------------------------------------
// Action 序列化 / 反序列化
//
//   配置文件里的写法：  bind:86        → 触发游戏动作（EGameBinds=86，团队标记1）
//                       key:70+ctrl    → 发送 Ctrl+F1
//----------------------------------------------------------------------------------------------------

inline bool ParseAction(const char* aText, Action& aOut)
{
	if (aText == nullptr)
	{
		return false;
	}

	const std::string text = Trim(aText);
	if (text.empty())
	{
		return false;
	}

	const std::size_t colon = text.find(':');
	if (colon == std::string::npos)
	{
		return false;
	}

	const std::string kind = ToLower(Trim(text.substr(0, colon)));
	const std::string rest = Trim(text.substr(colon + 1));

	if (kind == "bind")
	{
		int value = 0;
		if (!ParseInt(rest, value) || value <= 0)
		{
			return false;
		}

		Action action;
		action.kind  = kActionBind;
		action.value = value;
		aOut = action;
		return true;
	}

	if (kind == "key")
	{
		Action action;
		action.kind = kActionKey;

		const std::vector<std::string> parts = Split(rest, '+');
		bool haveValue = false;
		for (std::size_t i = 0; i < parts.size(); ++i)
		{
			const std::string part = ToLower(Trim(parts[i]));
			if (part.empty())
			{
				continue;
			}

			if (part == "ctrl" || part == "control")
			{
				action.ctrl = true;
			}
			else if (part == "alt")
			{
				action.alt = true;
			}
			else if (part == "shift")
			{
				action.shift = true;
			}
			else
			{
				int value = 0;
				if (haveValue || !ParseInt(part, value))
				{
					return false;
				}
				action.value = value;
				haveValue = true;
			}
		}

		if (!haveValue || action.value <= 0 || action.value > 255)
		{
			return false;
		}

		aOut = action;
		return true;
	}

	return false;
}

inline std::string FormatAction(const Action& aAction)
{
	char buffer[64] = {};
	if (aAction.kind == kActionBind)
	{
		std::snprintf(buffer, sizeof(buffer), "bind:%d", aAction.value);
		return std::string(buffer);
	}

	std::string text = "key:";
	text += std::to_string(aAction.value);
	if (aAction.ctrl)
	{
		text += "+ctrl";
	}
	if (aAction.alt)
	{
		text += "+alt";
	}
	if (aAction.shift)
	{
		text += "+shift";
	}
	return text;
}

/// 判断序列里是否已经包含某个标记序号。
inline bool Contains(const std::vector<uint8_t>& aSequence, uint8_t aValue)
{
	for (std::size_t i = 0; i < aSequence.size(); ++i)
	{
		if (aSequence[i] == aValue)
		{
			return true;
		}
	}
	return false;
}

/// 解析 "1,2,3" 形式的标记序列（v1 的配置格式，v2 用于迁移旧配置）。
/// 分隔符可以是逗号、分号、空格或制表符；越界值（不在 1..8）丢弃；重复值只保留第一次出现。
inline std::vector<uint8_t> ParseSequence(const char* aText)
{
	std::vector<uint8_t> result;
	if (aText == nullptr)
	{
		return result;
	}

	const char* p = aText;
	while (*p != '\0')
	{
		while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';' || *p == '|')
		{
			++p;
		}
		if (*p == '\0')
		{
			break;
		}

		char* end = nullptr;
		long value = std::strtol(p, &end, 10);
		if (end == p)
		{
			++p; // 非数字字符，跳过
			continue;
		}
		p = end;

		if (value >= 1 && value <= kMaxMarker)
		{
			const uint8_t marker = static_cast<uint8_t>(value);
			if (!Contains(result, marker))
			{
				result.push_back(marker);
			}
		}
	}

	return result;
}

/// 解析 TargetMode 配置文本：auto（或空）→ 自动，0 → 地面，1 → 目标；其它值按自动处理。
inline int ParseTargetMode(const char* aText)
{
	if (aText == nullptr)
	{
		return kTargetModeAuto;
	}

	const std::string text = Trim(aText);
	if (text.empty())
	{
		return kTargetModeAuto;
	}
	if (text == "0")
	{
		return kTargetModeWorld;
	}
	if (text == "1")
	{
		return kTargetModeAgent;
	}
	return kTargetModeAuto; // "auto" / "AUTO" / 其它
}

/// 结合自动检测结果，算出真正使用的模式。
/// aConfigured 为 0/1 时以配置为准；为自动时看哪一组游戏内标记快捷键有绑定。
inline int ResolveTargetMode(int aConfigured, bool aWorldBound, bool aAgentBound)
{
	if (aConfigured == kTargetModeWorld || aConfigured == kTargetModeAgent)
	{
		return aConfigured;
	}
	if (aAgentBound && !aWorldBound)
	{
		return kTargetModeAgent;
	}
	return kTargetModeWorld;
}

/// 把 1..8 的标记序号翻译成 Action 列表（aBaseBind 取 kSquadMarkerWorldBase / kSquadMarkerAgentBase）
inline std::vector<Action> ActionsFromSequence(const std::vector<uint8_t>& aSequence, int aBaseBind)
{
	std::vector<Action> result;
	for (std::size_t i = 0; i < aSequence.size(); ++i)
	{
		Action action;
		action.kind  = kActionBind;
		action.value = aBaseBind + static_cast<int>(aSequence[i]) - 1;
		result.push_back(action);
	}
	return result;
}

/// 默认循环列表：团队标记 1..8（在地面放置）
inline std::vector<Action> DefaultActions()
{
	std::vector<Action> result;
	for (int marker = 1; marker <= kMaxMarker; ++marker)
	{
		Action action;
		action.kind  = kActionBind;
		action.value = kSquadMarkerWorldBase + marker - 1;
		result.push_back(action);
	}
	return result;
}

/// 取出本次应当触发的标记序号（1..8），并把游标推进到下一位（到末尾后回到开头）。
/// 序列为空时返回 0。
inline uint8_t CurrentAndAdvance(const std::vector<uint8_t>& aSequence, std::size_t& aCursor)
{
	if (aSequence.empty())
	{
		return 0;
	}

	const std::size_t index = aCursor % aSequence.size();
	const uint8_t marker = aSequence[index];
	aCursor = (index + 1) % aSequence.size();
	return marker;
}

/// 下一项的游标（到末尾后回到 0）；列表为空时返回 0。
inline std::size_t NextIndex(std::size_t aCursor, std::size_t aSize)
{
	if (aSize == 0)
	{
		return 0;
	}
	const std::size_t index = aCursor % aSize;
	return (index + 1) % aSize;
}

//----------------------------------------------------------------------------------------------------
// 多个循环（v3）
//
// 每个循环有自己的名字、列表、游标、上次触发时间，并在 Nexus 输入绑定里各占一个标识：
//   8 个循环的标识统一是 "MarkerCycle-循环1" … "MarkerCycle-循环8"，
//   与面板里显示的名字一一对应（v1.3 及以前第 1 个叫 "MarkerCycle-循环放置"）。
//----------------------------------------------------------------------------------------------------

/// 最多支持几个并行循环（每个循环在 Nexus 输入绑定里占一个标识）
constexpr int kMaxCycles = 8;

// 长按连发（按住触发键，按固定间隔连续输出列表里的项）
constexpr int kDefaultRepeatDelayMs    = 300; ///< 按住多久之后开始连发
constexpr int kMinRepeatDelayMs        = 0;
constexpr int kMaxRepeatDelayMs        = 5000;
constexpr int kDefaultRepeatIntervalMs = 150; ///< 连发间隔
constexpr int kMinRepeatIntervalMs     = 30;
constexpr int kMaxRepeatIntervalMs     = 2000;

/// 把“起手延迟”夹到合法范围
inline int ClampRepeatDelay(int aMs)
{
	if (aMs < kMinRepeatDelayMs)
	{
		return kMinRepeatDelayMs;
	}
	if (aMs > kMaxRepeatDelayMs)
	{
		return kMaxRepeatDelayMs;
	}
	return aMs;
}

/// 把“连发间隔”夹到合法范围
inline int ClampRepeatInterval(int aMs)
{
	if (aMs < kMinRepeatIntervalMs)
	{
		return kMinRepeatIntervalMs;
	}
	if (aMs > kMaxRepeatIntervalMs)
	{
		return kMaxRepeatIntervalMs;
	}
	return aMs;
}

/// 长按连发：这一刻是否该再执行一项（纯逻辑，便于单元测试）
inline bool ShouldRepeat(bool aHeld, bool aHoldRepeat, unsigned long long aNow, unsigned long long aHoldNext)
{
	return aHeld && aHoldRepeat && aNow >= aHoldNext;
}

/// 连发下一次的时间点：从现在起再等 aIntervalMs 毫秒
inline unsigned long long NextRepeatTime(unsigned long long aNow, int aIntervalMs)
{
	return aNow + static_cast<unsigned long long>(aIntervalMs);
}

/// 第 1 个循环沿用的老标识（v1/v2 用户已经绑好的键不会丢）
inline const char* FirstBindIdentifier()
{
	return "MarkerCycle-循环1";
}

/// 第 aIndex 个循环（0 起）在 Nexus 输入绑定里的标识。
inline std::string CycleBindIdentifier(int aIndex)
{
	if (aIndex < 0)
	{
		aIndex = 0;
	}

	char buffer[64] = {};
	std::snprintf(buffer, sizeof(buffer), "MarkerCycle-循环%d", aIndex + 1);
	return std::string(buffer);
}

/// 新循环的默认名字
inline std::string DefaultCycleName(int aIndex)
{
	char buffer[64] = {};
	std::snprintf(buffer, sizeof(buffer), "循环%d", aIndex + 1);
	return std::string(buffer);
}

/// 一个循环
struct Cycle
{
	int                 slot = 1;      ///< 绑定槽位（1 起，1..kMaxCycles）。槽位决定 Nexus 输入绑定里的标识，删掉别的循环也不会变
	std::string         name;          ///< 面板里显示的名字（可改）
	std::vector<Action> actions;       ///< 循环列表
	std::size_t         cursor   = 0;  ///< 下一次执行列表里的第几项
	unsigned long long  lastPress = 0; ///< 上次触发时间（毫秒，用于空闲重置）

	// 运行态，不写进 INI：
	bool                held     = false; ///< 触发键是否还按着（长按连发用）
	unsigned long long  holdNext = 0;     ///< 连发模式下，下一次该执行的时间点（毫秒）
};

/// 造一个默认循环（团队标记 1..8，在地面放置）
inline Cycle MakeDefaultCycle(int aIndex)
{
	Cycle cycle;
	cycle.name = DefaultCycleName(aIndex);
	cycle.actions = DefaultActions();
	return cycle;
}

/// 造一个“标记目标”版本的默认循环（GB_SquadMarkerSetAgent1..8）
inline Cycle MakeAgentCycle(int aIndex)
{
	Cycle cycle;
	cycle.name = DefaultCycleName(aIndex);
	for (int marker = 1; marker <= kMaxMarker; ++marker)
	{
		Action action;
		action.kind = kActionBind;
		action.value = kSquadMarkerAgentBase + marker - 1;
		cycle.actions.push_back(action);
	}
	return cycle;
}

/// 取出本次要执行的动作，并把游标推进一格；列表为空时返回 false。
inline bool TakeNextAction(Cycle& aCycle, Action& aOut)
{
	if (aCycle.actions.empty())
	{
		return false;
	}

	const std::size_t index = aCycle.cursor % aCycle.actions.size();
	aOut = aCycle.actions[index];
	aCycle.cursor = (index + 1) % aCycle.actions.size();
	return true;
}

/// 列表变动后保证游标仍然有效
inline void ClampCursor(Cycle& aCycle)
{
	if (aCycle.actions.empty() || aCycle.cursor >= aCycle.actions.size())
	{
		aCycle.cursor = 0;
	}
}

/// 把循环数组整理成固定 aMax 个（槽位 1..aMax，第 i 个循环的槽位是 i+1）。
/// 已有的循环按槽位原样保留（名字/列表/游标/时间都不动），缺的补一个空列表循环。
/// 这样 8 个绑定标识从加载那一刻起就都在 Nexus 输入绑定里，玩家不必先“新建循环”。
inline void EnsureCycleSlots(std::vector<Cycle>& aCycles, int aMax)
{
	std::vector<Cycle> result;
	result.reserve(static_cast<std::size_t>(aMax > 0 ? aMax : 0));

	for (int slot = 1; slot <= aMax; ++slot)
	{
		Cycle cycle;
		cycle.slot = slot;
		cycle.name = DefaultCycleName(slot - 1);

		for (std::size_t i = 0; i < aCycles.size(); ++i)
		{
			if (aCycles[i].slot != slot)
			{
				continue;
			}

			cycle        = aCycles[i];
			cycle.slot   = slot;
			if (cycle.name.empty())
			{
				cycle.name = DefaultCycleName(slot - 1);
			}
			ClampCursor(cycle);
			break;
		}

		result.push_back(cycle);
	}

	aCycles = result;
}

///----------------------------------------------------------------------------------------------------
/// 配置文件的写出（放在这里而不是 MarkerCycle.cpp，是为了能用普通单元测试验证格式）。
/// 返回 UTF-8 文本，行尾统一 \r\n。
///
/// 关键点：Windows 的 INI 接口读的时候 lpAppName 是 **不带方括号** 的小节名（"Cycle1"），
/// 但文件里必须写成 "[Cycle1]"。曾经这里写成裸 Cycle1，导致后面的键被并进上一节，
/// 循环列表写出去就再也读不回来。WriteSectionHeader() 就是为此存在的。
///----------------------------------------------------------------------------------------------------

/// 循环在 INI 里的小节名（不带方括号）
inline std::string CycleSectionName(int aSlot)
{
	char buffer[32] = {};
	std::snprintf(buffer, sizeof(buffer), "Cycle%d", aSlot);
	return std::string(buffer);
}

/// 循环在 INI 里的小节头（带方括号）
inline std::string CycleSectionHeader(int aSlot)
{
	return "[" + CycleSectionName(aSlot) + "]";
}

inline std::string SerializeConfig(const std::vector<Cycle>& aCycles, int aPressMs, int aResetSeconds, bool aShowAlert,
                                   bool aHoldRepeat = false, int aRepeatDelayMs = kDefaultRepeatDelayMs,
                                   int aRepeatIntervalMs = kDefaultRepeatIntervalMs)
{
	std::string text;
	text += "; 一键循环标记 (MarkerCycle) - Nexus 插件配置\r\n";
	text += "; 用面板改最省事：Nexus 插件列表 -> 一键循环标记 -> 设置（改完立刻写回这里）。\r\n";
	text += "; 也可以直接编辑本文件，保存后在 Nexus 里重新加载插件生效。\r\n";
	text += "\r\n[MarkerCycle]\r\n";

	char line[512] = {};

	text += "\r\n; 固定 8 个循环，每个循环在 Nexus 的「输入绑定」里各占一个标识：\r\n";
	text += ";   循环1 = MarkerCycle-循环1     循环2 = MarkerCycle-循环2   ……   循环8 = MarkerCycle-循环8\r\n";
	text += "; 列表留空的循环，按它对应的键不会有任何动作。\r\n";

	text += "\r\n; 动作/按键按住的时长（毫秒）。按了没反应可以改成 80。\r\n";
	std::snprintf(line, sizeof(line), "PressDurationMs=%d\r\n", aPressMs);
	text += line;

	text += "; 空闲多少秒后从第一项重新开始；0 = 一直循环，不重置。\r\n";
	std::snprintf(line, sizeof(line), "ResetAfterSeconds=%d\r\n", aResetSeconds);
	text += line;

	text += "; 1 = 每次执行时在屏幕上提示当前循环是第几项；0 = 不提示。\r\n";
	std::snprintf(line, sizeof(line), "ShowAlert=%d\r\n", aShowAlert ? 1 : 0);
	text += line;

	text += "\r\n; 长按连发：1 = 按住触发键不放，会按下面的间隔连续执行；0 = 按一次只走一项。\r\n";
	std::snprintf(line, sizeof(line), "HoldRepeat=%d\r\n", aHoldRepeat ? 1 : 0);
	text += line;

	text += "; 按住多久之后开始连发（毫秒）。设 300 就是“按住约 0.3 秒后开始连发”，轻按一下只走一项。\r\n";
	std::snprintf(line, sizeof(line), "RepeatDelayMs=%d\r\n", aRepeatDelayMs);
	text += line;

	text += "; 连发间隔（毫秒），30 - 2000。150 = 每 0.15 秒走一项。\r\n";
	std::snprintf(line, sizeof(line), "RepeatIntervalMs=%d\r\n", aRepeatIntervalMs);
	text += line;

	for (std::size_t i = 0; i < aCycles.size(); ++i)
	{
		const Cycle& cycle = aCycles[i];

		text += "\r\n; ==================== 循环 ";
		text += std::to_string(cycle.slot);
		text += " ====================\r\n";
		text += "; 触发键标识：";
		text += CycleBindIdentifier(cycle.slot - 1);
		text += "（在 Nexus 的「输入绑定」里设置）\r\n";

		text += CycleSectionHeader(cycle.slot);
		text += "\r\n; 面板里显示的名字。\r\nName=";
		text += cycle.name;
		text += "\r\n";

		text += "; 列表长度，可以先写 ActionCount 再写 Action1..ActionN。\r\n";
		text += ";   bind:<数字>        触发游戏动作，数字是 Nexus 的 EGameBinds 枚举值（团队标记1 = 86，标记目标1 = 94）\r\n";
		text += ";   key:<数字>+ctrl    直接向游戏发送按键，数字是 Windows 虚拟键码，可加 +ctrl / +alt / +shift\r\n";
		std::snprintf(line, sizeof(line), "ActionCount=%d\r\n", static_cast<int>(cycle.actions.size()));
		text += line;

		for (std::size_t a = 0; a < cycle.actions.size(); ++a)
		{
			std::snprintf(line, sizeof(line), "Action%d=%s\r\n", static_cast<int>(a + 1), FormatAction(cycle.actions[a]).c_str());
			text += line;
		}
	}

	return text;
}
}
