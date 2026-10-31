///----------------------------------------------------------------------------------------------------
/// MarkerCycle / 一键循环标记 - 导出检查
/// 加载编译好的 MarkerCycle.dll，调用 GetAddonDef()，校验 Nexus 需要的元数据。
/// 不接触游戏进程，可以在游戏关闭时安全运行。
/// 用法: test_export.exe <MarkerCycle.dll 路径>
///----------------------------------------------------------------------------------------------------

#include "../src/nexus/Nexus.h"

#include <cstdio>
#include <cstring>
#include <string>

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

typedef AddonDefinition_t* (*GETADDONDEF)(void);

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: test_export.exe <path to MarkerCycle.dll>\n");
		return 2;
	}

	std::printf("MarkerCycle export test\n");
	std::printf("-----------------------\n");

	HMODULE module = LoadLibraryA(argv[1]);
	if (module == nullptr)
	{
		std::printf("  FAIL  LoadLibraryA failed, error %lu\n", GetLastError());
		return 1;
	}

	GETADDONDEF getAddonDef = reinterpret_cast<GETADDONDEF>(GetProcAddress(module, "GetAddonDef"));
	CHECK(getAddonDef != nullptr);

	if (getAddonDef != nullptr)
	{
		AddonDefinition_t* definition = getAddonDef();
		CHECK(definition != nullptr);

		if (definition != nullptr)
		{
			std::printf("  Name        : %s\n", definition->Name != nullptr ? definition->Name : "(null)");
			std::printf("  Signature   : %u\n", definition->Signature);
			std::printf("  APIVersion  : %u\n", definition->APIVersion);
			std::printf("  Version     : %u.%u.%u.%u\n", definition->Version.Major, definition->Version.Minor,
				definition->Version.Build, definition->Version.Revision);
			std::printf("  Author      : %s\n", definition->Author != nullptr ? definition->Author : "(null)");
			std::printf("  Description : %s\n", definition->Description != nullptr ? definition->Description : "(null)");

			CHECK(definition->Signature != 0);
			CHECK(definition->Signature == 1296236545u);
			CHECK(definition->APIVersion == NEXUS_API_VERSION);
			CHECK(definition->Load != nullptr);
			CHECK(definition->Unload != nullptr);
			CHECK(definition->Name != nullptr);
			CHECK(definition->Name != nullptr && std::strcmp(definition->Name, u8"一键循环标记") == 0);
		}
	}

	FreeLibrary(module);

	std::printf("-----------------------\n");
	if (g_failures == 0)
	{
		std::printf("Export OK.\n");
		return 0;
	}

	std::printf("%d check(s) FAILED.\n", g_failures);
	return 1;
}
