///----------------------------------------------------------------------------------------------------
/// MarkerCycle / 一键循环标记 - 循环逻辑单元测试（不需要游戏，可直接运行）
///----------------------------------------------------------------------------------------------------

#include "../src/cycle.h"

#include <cstdio>
#include <cstring>

using namespace markercycle;

static int g_failures = 0;

#define CHECK(cond)                                                                 \
	do                                                                              \
	{                                                                               \
		if (!(cond))                                                                \
		{                                                                           \
			std::printf("  FAIL  line %d : %s\n", __LINE__, #cond);                 \
			++g_failures;                                                           \
		}                                                                           \
	} while (0)

#define CHECK_SEQ(seq, ...)                                                         \
	do                                                                              \
	{                                                                               \
		const uint8_t expected[] = { __VA_ARGS__ };                                 \
		CHECK((seq).size() == sizeof(expected) / sizeof(expected[0]));              \
		if ((seq).size() == sizeof(expected) / sizeof(expected[0]))                 \
		{                                                                           \
			bool same = true;                                                       \
			for (std::size_t i = 0; i < (seq).size(); ++i)                          \
			{                                                                       \
				if ((seq)[i] != expected[i])                                        \
				{                                                                   \
					same = false;                                                   \
				}                                                                   \
			}                                                                       \
			CHECK(same);                                                            \
		}                                                                           \
	} while (0)

static void TestParseSequence()
{
	std::printf("[parse] Sequence\n");

	CHECK_SEQ(ParseSequence("1,2,3"), 1, 2, 3);
	CHECK_SEQ(ParseSequence("1,2,3,4,5,6,7,8"), 1, 2, 3, 4, 5, 6, 7, 8);
	CHECK_SEQ(ParseSequence(" 7 , 1 ;99| 0 2 "), 7, 1, 2);   // 越界/非法值丢弃
	CHECK_SEQ(ParseSequence("3,3,1"), 3, 1);                 // 去重
	CHECK_SEQ(ParseSequence("8"), 8);
	CHECK(ParseSequence("abc").empty());                     // 全非法
	CHECK(ParseSequence("").empty());
	CHECK(ParseSequence(nullptr).empty());
	CHECK(ParseSequence("9,10,-1,0").empty());               // 全越界
}

static void TestParseTargetMode()
{
	std::printf("[parse] TargetMode\n");

	CHECK(ParseTargetMode("auto") == kTargetModeAuto);
	CHECK(ParseTargetMode("AUTO") == kTargetModeAuto);
	CHECK(ParseTargetMode("") == kTargetModeAuto);
	CHECK(ParseTargetMode(nullptr) == kTargetModeAuto);
	CHECK(ParseTargetMode("0") == kTargetModeWorld);
	CHECK(ParseTargetMode("1") == kTargetModeAgent);
	CHECK(ParseTargetMode(" 1 ") == kTargetModeAgent);
	CHECK(ParseTargetMode("2") == kTargetModeAuto);
}

static void TestResolveTargetMode()
{
	std::printf("[parse] ResolveTargetMode\n");

	// 显式配置优先
	CHECK(ResolveTargetMode(kTargetModeWorld, false, true) == kTargetModeWorld);
	CHECK(ResolveTargetMode(kTargetModeAgent, true, false) == kTargetModeAgent);

	// 自动：哪一组有绑定就用哪一组
	CHECK(ResolveTargetMode(kTargetModeAuto, true, false) == kTargetModeWorld);
	CHECK(ResolveTargetMode(kTargetModeAuto, false, true) == kTargetModeAgent);
	CHECK(ResolveTargetMode(kTargetModeAuto, true, true) == kTargetModeWorld);
	CHECK(ResolveTargetMode(kTargetModeAuto, false, false) == kTargetModeWorld);
}

static void TestCycle()
{
	std::printf("[cycle] 1..8 循环三次\n");

	const std::vector<uint8_t> full = ParseSequence("1,2,3,4,5,6,7,8");
	std::size_t cursor = 0;

	for (int round = 0; round < 3; ++round)
	{
		for (int i = 0; i < 8; ++i)
		{
			const uint8_t marker = CurrentAndAdvance(full, cursor);
			CHECK(marker == static_cast<uint8_t>(i + 1));
		}
	}
	CHECK(cursor == 0);

	std::printf("[cycle] 子集 2,5,7\n");

	const std::vector<uint8_t> subset = ParseSequence("2,5,7");
	cursor = 0;
	CHECK(CurrentAndAdvance(subset, cursor) == 2);
	CHECK(CurrentAndAdvance(subset, cursor) == 5);
	CHECK(CurrentAndAdvance(subset, cursor) == 7);
	CHECK(CurrentAndAdvance(subset, cursor) == 2);

	std::printf("[cycle] 空序列\n");

	const std::vector<uint8_t> none;
	cursor = 0;
	CHECK(CurrentAndAdvance(none, cursor) == 0);
	CHECK(cursor == 0);
}

static void TestActionParse()
{
	std::printf("[action] ParseAction / FormatAction\n");

	Action action;
	CHECK(ParseAction("bind:86", action));
	CHECK(action.kind == kActionBind && action.value == 86 && !action.ctrl && !action.alt && !action.shift);
	CHECK(FormatAction(action) == "bind:86");

	CHECK(ParseAction("  BIND: 94  ", action));
	CHECK(action.kind == kActionBind && action.value == 94);

	CHECK(ParseAction("key:70", action));
	CHECK(action.kind == kActionKey && action.value == 70);
	CHECK(FormatAction(action) == "key:70");

	CHECK(ParseAction("key:0x46+ctrl+shift", action));
	CHECK(action.kind == kActionKey && action.value == 70 && action.ctrl && action.shift && !action.alt);
	CHECK(FormatAction(action) == "key:70+ctrl+shift");

	CHECK(ParseAction("key:ctrl+alt+65", action)); // 修饰键顺序随意
	CHECK(action.value == 65 && action.ctrl && action.alt && !action.shift);

	CHECK(!ParseAction("", action));
	CHECK(!ParseAction(static_cast<const char*>(nullptr), action));
	CHECK(!ParseAction("bind", action));   // 缺少冒号
	CHECK(!ParseAction("bind:0", action));
	CHECK(!ParseAction("bind:abc", action));
	CHECK(!ParseAction("key:", action));
	CHECK(!ParseAction("key:300", action)); // 超出键码范围
	CHECK(!ParseAction("key:70+foo", action));
	CHECK(!ParseAction("mouse:1", action));
}

static void TestActionCycle()
{
	std::printf("[action] 列表循环 / 默认列表 / 旧配置迁移\n");

	const std::vector<Action> defaults = DefaultActions();
	CHECK(defaults.size() == 8);
	CHECK(defaults.front().kind == kActionBind && defaults.front().value == kSquadMarkerWorldBase);
	CHECK(defaults.back().value == kSquadMarkerWorldBase + 7);

	const std::vector<Action> migrated = ActionsFromSequence(ParseSequence("2,5,7"), kSquadMarkerAgentBase);
	CHECK(migrated.size() == 3);
	CHECK(migrated[0].value == kSquadMarkerAgentBase + 1);
	CHECK(migrated[2].value == kSquadMarkerAgentBase + 6);

	std::size_t cursor = 0;
	for (int round = 0; round < 3; ++round)
	{
		for (std::size_t i = 0; i < migrated.size(); ++i)
		{
			CHECK(cursor == i);
			cursor = NextIndex(cursor, migrated.size());
		}
	}
	CHECK(cursor == 0);
	CHECK(NextIndex(0, 0) == 0);
}

/// 多循环（v3）：每个循环有独立的槽位标识、列表和游标
static void TestMultiCycle()
{
	std::printf("[cycle] 多循环 / 绑定标识 / 独立游标\n");

	// 绑定标识：第 1 个沿用老标识（老用户已设的键不会丢），其余按槽位编号
	CHECK(std::string(CycleBindIdentifier(0)) == "MarkerCycle-循环1");
	CHECK(std::string(CycleBindIdentifier(1)) == "MarkerCycle-循环2");
	CHECK(std::string(CycleBindIdentifier(7)) == "MarkerCycle-循环8");
	CHECK(std::string(FirstBindIdentifier()) == CycleBindIdentifier(0));

	CHECK(DefaultCycleName(0) == "循环1");
	CHECK(DefaultCycleName(2) == "循环3");

	// 默认循环：8 项，团队标记 1..8（地面）
	Cycle world = MakeDefaultCycle(0);
	CHECK(world.slot == 1);
	CHECK(world.actions.size() == 8);
	CHECK(world.actions[0].kind == kActionBind);
	CHECK(world.actions[0].value == kSquadMarkerWorldBase);
	CHECK(world.actions[7].value == kSquadMarkerWorldBase + 7);
	CHECK(world.cursor == 0);

	// 标记目标版本的循环
	Cycle agent = MakeAgentCycle(1);
	CHECK(agent.actions.size() == 8);
	CHECK(agent.actions[0].value == kSquadMarkerAgentBase);
	CHECK(agent.actions[7].value == kSquadMarkerAgentBase + 7);

	// 两个循环的游标互不影响
	Action action;
	CHECK(TakeNextAction(world, action));
	CHECK(action.value == kSquadMarkerWorldBase);
	CHECK(world.cursor == 1);
	CHECK(agent.cursor == 0);

	// 转满一圈回到第一项
	for (int i = 0; i < 8; ++i)
	{
		CHECK(TakeNextAction(world, action));
	}
	CHECK(world.cursor == 1);

	// 空列表：取不到动作，游标保持 0
	Cycle empty;
	CHECK(!TakeNextAction(empty, action));
	CHECK(empty.cursor == 0);

	// 列表变短后游标自动回到合法位置
	world.cursor = 7;
	world.actions.resize(3);
	ClampCursor(world);
	CHECK(world.cursor == 0);

	world.cursor = 1;
	ClampCursor(world);
	CHECK(world.cursor == 1);

	// 面板与配置文件共用的文本格式，写出去再读回来必须一致
	const char* samples[] = { "bind:86", "bind:94", "key:70", "key:70+ctrl+shift", "key:112+alt" };
	for (int i = 0; i < 5; ++i)
	{
		Action parsed;
		CHECK(ParseAction(samples[i], parsed));
		CHECK(FormatAction(parsed) == samples[i]);
	}
}

/// 固定 8 个槽位（v1.3）：缺的补空循环，已有的按槽位保留，顺序按槽位排好
static void TestCycleSlots()
{
	std::printf("[cycle] 固定 8 个循环槽位 / 保留已有配置\n");

	// 空数组 -> 补齐成 8 个空循环，槽位 1..8，名字是默认名
	std::vector<Cycle> cycles;
	EnsureCycleSlots(cycles, kMaxCycles);
	CHECK(static_cast<int>(cycles.size()) == kMaxCycles);
	for (int i = 0; i < kMaxCycles; ++i)
	{
		CHECK(cycles[i].slot == i + 1);
		CHECK(cycles[i].actions.empty());
		CHECK(cycles[i].name == DefaultCycleName(i));
	}

	// 只有“循环 5”有内容：补完后仍 8 个，内容留在第 5 个，其余为空
	std::vector<Cycle> partial;
	Cycle five;
	five.slot = 5;
	five.name = "标记目标";
	five.actions = MakeAgentCycle(4).actions;
	five.cursor = 3;
	partial.push_back(five);
	EnsureCycleSlots(partial, kMaxCycles);

	CHECK(static_cast<int>(partial.size()) == kMaxCycles);
	CHECK(partial[4].slot == 5);
	CHECK(partial[4].name == "标记目标");
	CHECK(partial[4].actions.size() == 8);
	CHECK(partial[4].cursor == 3);
	CHECK(partial[0].actions.empty());
	CHECK(partial[7].slot == 8);

	// 乱序 + 重复槽位：按槽位归位，不产生重复
	std::vector<Cycle> messy;
	Cycle a;
	a.slot = 3;
	a.actions = DefaultActions();
	messy.push_back(a);
	Cycle b;
	b.slot = 1;
	b.name = "第一";
	messy.push_back(b);
	Cycle dup;
	dup.slot = 3;
	dup.name = "重复的第三个";
	messy.push_back(dup);
	EnsureCycleSlots(messy, kMaxCycles);

	CHECK(static_cast<int>(messy.size()) == kMaxCycles);
	CHECK(messy[0].name == "第一");
	CHECK(messy[2].slot == 3);
	CHECK(messy[2].name == DefaultCycleName(2)); // 同槽位先到先得：留下先来的那份，空名字补默认名
	CHECK(messy[2].actions.size() == 8);          // 先来的那份带着默认标记 1-8
}

// 配置文件格式：小节头必须带方括号，否则 Windows 的 INI 接口读不回来
// （v1.2 就是写成裸 Cycle1，导致面板里改的列表下次加载全丢）
static void TestConfigText()
{
	std::vector<Cycle> cycles;
	cycles.push_back(MakeDefaultCycle(0));
	EnsureCycleSlots(cycles, kMaxCycles);
	cycles[1].name = "标记目标";
	cycles[1].actions.push_back(Action{kActionBind, 94, false, false, false});

	const std::string text = SerializeConfig(cycles, 50, 0, false);

	CHECK(text.find("\r\n[MarkerCycle]\r\n") != std::string::npos);
	CHECK(text.find("\r\n[Cycle1]\r\n") != std::string::npos);
	CHECK(text.find("\r\n[Cycle8]\r\n") != std::string::npos);
	CHECK(text.find("Name=标记目标") != std::string::npos);

	// 逐行扫：不允许出现“裸小节名”行（Cycle3 这种既不是注释也不是 key=value）
	bool bare = false;
	std::size_t pos = 0;
	while (pos <= text.size())
	{
		const std::size_t eol = text.find("\r\n", pos);
		const std::string line = text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
		if (!line.empty() && line[0] != ';' && line[0] != '[' && line.find('=') == std::string::npos)
		{
			if (line.compare(0, 5, "Cycle") == 0)
			{
				bare = true;
			}
		}
		if (eol == std::string::npos)
		{
			break;
		}
		pos = eol + 2;
	}
	CHECK(!bare);

	// 8 个小节都在
	for (int slot = 1; slot <= kMaxCycles; ++slot)
	{
		CHECK(text.find("\r\n[Cycle" + std::to_string(slot) + "]\r\n") != std::string::npos);
	}
}

static void TestHoldRepeat()
{
	// 范围钳制
	CHECK(ClampRepeatDelay(-5) == kMinRepeatDelayMs);
	CHECK(ClampRepeatDelay(123) == 123);
	CHECK(ClampRepeatDelay(99999) == kMaxRepeatDelayMs);
	CHECK(ClampRepeatInterval(0) == kMinRepeatIntervalMs);
	CHECK(ClampRepeatInterval(150) == 150);
	CHECK(ClampRepeatInterval(5000) == kMaxRepeatIntervalMs);

	// 这一刻要不要再执行一项
	CHECK(!ShouldRepeat(false, true, 1000, 500));  // 键已经松开
	CHECK(!ShouldRepeat(true, false, 1000, 500));  // 连发关掉了
	CHECK(!ShouldRepeat(true, true, 400, 500));    // 还没到时间
	CHECK(ShouldRepeat(true, true, 500, 500));     // 正好到点
	CHECK(ShouldRepeat(true, true, 900, 500));     // 已经过点

	// 下一次时间点 = 现在 + 间隔
	CHECK(NextRepeatTime(1000, 150) == 1150);
	CHECK(NextRepeatTime(1000, 0) == 1000);

	// 序列化里带上了三个新键
	std::vector<Cycle> cycles;
	cycles.push_back(MakeDefaultCycle(0));
	EnsureCycleSlots(cycles, kMaxCycles);

	const std::string on = SerializeConfig(cycles, 50, 0, false, true, 250, 80);
	CHECK(on.find("HoldRepeat=1") != std::string::npos);
	CHECK(on.find("RepeatDelayMs=250") != std::string::npos);
	CHECK(on.find("RepeatIntervalMs=80") != std::string::npos);

	const std::string off = SerializeConfig(cycles, 50, 0, false);
	CHECK(off.find("HoldRepeat=0") != std::string::npos);
	CHECK(off.find("RepeatDelayMs=300") != std::string::npos);
	CHECK(off.find("RepeatIntervalMs=150") != std::string::npos);
}

int main()
{
	std::printf("MarkerCycle cycle-logic tests\n");
	std::printf("-----------------------------\n");

	TestParseSequence();
	TestParseTargetMode();
	TestResolveTargetMode();
	TestCycle();
	TestActionParse();
	TestActionCycle();
	TestMultiCycle();
	TestCycleSlots();
	TestConfigText();
	TestHoldRepeat();

	std::printf("-----------------------------\n");
	if (g_failures == 0)
	{
		std::printf("All tests passed.\n");
		return 0;
	}

	std::printf("%d check(s) FAILED.\n", g_failures);
	return 1;
}
