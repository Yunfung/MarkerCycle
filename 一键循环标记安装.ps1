#requires -version 5.1
<#
=====================================================================================================
 一键循环标记 (MarkerCycle) - Nexus 插件安装脚本
 说明：把 addons\MarkerCycle.dll 复制到 GW2 的 addons 目录，并做备份/回退/状态检查。
 用法：
   powershell -NoProfile -ExecutionPolicy Bypass -File "一键循环标记安装.ps1"
   powershell ... -File "一键循环标记安装.ps1" -Action install  -GameDir "G:\...\Guild_Wars_2..."
   powershell ... -File "一键循环标记安装.ps1" -Action rollback -PurgeConfig
=====================================================================================================
#>
[CmdletBinding()]
param(
    [string]$Action = '',
    [string]$GameDir = '',
    [switch]$Force,
    [switch]$DryRun,
    [switch]$PurgeConfig
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------------------------------
# 基本信息
# ---------------------------------------------------------------------------------------------------
$script:AddonFolder  = 'MarkerCycle'
$script:AddonDisplay = '一键循环标记'
$script:DllName      = 'MarkerCycle.dll'
$script:ConfigDir    = 'MarkerCycle'
$script:ConfigFile   = 'MarkerCycle.ini'
$script:PackageDll   = Join-Path $PSScriptRoot "addons\$script:DllName"
$script:BackupDir    = Join-Path $PSScriptRoot 'backups'

# ---------------------------------------------------------------------------------------------------
# 输出助手
# ---------------------------------------------------------------------------------------------------
function Step([string]$Text) { Write-Host ''; Write-Host "==> $Text" -ForegroundColor Cyan }
function Ok([string]$Text)   { Write-Host "  [ OK ] $Text" -ForegroundColor Green }
function Note([string]$Text) { Write-Host "  [  i  ] $Text" -ForegroundColor Gray }
function Warn([string]$Text) { Write-Host "  [ !  ] $Text" -ForegroundColor Yellow }
function Fail([string]$Text) {
    Write-Host ''
    Write-Host "  [FAIL] $Text" -ForegroundColor Red
    Write-Host ''
    exit 1
}

try {
    if ([Console]::IsOutputRedirected) {
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    }
} catch { }

# ---------------------------------------------------------------------------------------------------
# 路径与检查
# ---------------------------------------------------------------------------------------------------
function Normalize-Dir([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    $value = $Path.Trim().Trim('"').Trim("'")
    return $value.TrimEnd('\', '/')
}

function Ask-GameDir {
    Write-Host ''
    Write-Host '  请输入游戏目录（就是放着 Gw2-64.exe 的那一层文件夹）' -ForegroundColor Cyan
    Note '例：D:\Games\Guild_Wars_2'
    Note '也可以直接把文件夹从资源管理器拖进这个窗口，路径会自动填进来。'
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $manual = Read-Host '  游戏目录'
        if ([string]::IsNullOrWhiteSpace($manual)) {
            Fail '没有填写游戏目录，已停止。'
        }
        $candidate = Normalize-Dir $manual
        if (Test-Path -LiteralPath (Join-Path $candidate 'Gw2-64.exe')) {
            return $candidate
        }
        Warn "这个目录里没有 Gw2-64.exe：$candidate"
        if ($attempt -lt 3) {
            Note '请重新填写。注意要选游戏根目录（不是 addons、也不是 bin64）。'
        }
    }
    Fail '连续 3 次都没找到 Gw2-64.exe，已停止。请确认游戏目录后重新运行。'
}

function Resolve-GameDir {
    if (-not [string]::IsNullOrWhiteSpace($GameDir)) {
        $candidate = Normalize-Dir $GameDir
        if (Test-Path -LiteralPath (Join-Path $candidate 'Gw2-64.exe')) { return $candidate }
        Fail "指定的游戏目录里没有 Gw2-64.exe：$candidate"
    }
    if ([Console]::IsInputRedirected) {
        Fail "缺少 -GameDir。示例：`n  -Action 安装 -GameDir `"D:\Games\Guild_Wars_2`""
    }
    return (Ask-GameDir)
}

function Assert-Game([string]$Dir) {
    if (-not (Test-Path -LiteralPath (Join-Path $Dir 'Gw2-64.exe'))) {
        Fail "这不是有效的 GW2 目录（缺少 Gw2-64.exe）：$Dir"
    }
    Ok "游戏目录：$Dir"
}

function Assert-Nexus([string]$Dir) {
    $loader = Join-Path $Dir 'addonLoader.dll'
    $hasProxy = (Test-Path -LiteralPath (Join-Path $Dir 'd3d11.dll')) -or (Test-Path -LiteralPath (Join-Path $Dir 'dxgi.dll'))

    if ((Test-Path -LiteralPath $loader) -and $hasProxy) {
        Ok '已检测到 Nexus 插件加载器（addonLoader.dll + d3d11/dxgi 代理）'
        return
    }
    if (-not (Test-Path -LiteralPath $loader)) {
        Warn '没有找到 addonLoader.dll —— 说明 Nexus 插件管理工具还没装。'
    }
    if (-not $hasProxy) {
        Warn '没有找到 d3d11.dll / dxgi.dll 代理文件 —— 说明 Nexus 插件管理工具还没装。'
    }
    if (-not $Force) {
        Fail '本插件是 Nexus 插件，必须先安装 Nexus（addonLoader.dll 等）才能加载。确实要继续可加 -Force。'
    }
    Warn '仍然继续（-Force）。插件不会被加载，直到 Nexus 装好。'
}

function Assert-NotRunning {
    $proc = Get-Process -Name 'Gw2-64' -ErrorAction SilentlyContinue
    if ($null -ne $proc) {
        Fail '游戏正在运行。请先完全退出游戏（包括登录器里的“进入游戏”进程）再安装。'
    }
    Ok '游戏未运行'
}

function Assert-DllWritable([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    try {
        $stream = [System.IO.File]::Open($Path, 'Open', 'ReadWrite', 'None')
        $stream.Close()
    } catch {
        Fail "目标插件正被占用（可能游戏或 Nexus 还开着）：$Path"
    }
}

# ---------------------------------------------------------------------------------------------------
# 状态
# ---------------------------------------------------------------------------------------------------
function Show-Status([string]$Dir) {
    $targetDll   = Join-Path (Join-Path $Dir 'addons') $script:DllName
    $configDir   = Join-Path (Join-Path $Dir 'addons') $script:ConfigDir
    $configFile  = Join-Path $configDir $script:ConfigFile

    Step "状态 - $script:AddonDisplay ($script:AddonFolder)"

    if (Test-Path -LiteralPath $targetDll) {
        $item = Get-Item -LiteralPath $targetDll
        Ok "已安装：$targetDll"
        Note ("大小 {0} 字节，时间 {1}" -f $item.Length, $item.LastWriteTime)
        if (Test-Path -LiteralPath $script:PackageDll) {
            $package = Get-Item -LiteralPath $script:PackageDll
            if ($package.Length -ne $item.Length) {
                Warn ("已安装的 DLL 与安装包内的版本大小不同（{0} vs {1} 字节），可能需要重新安装。" -f $item.Length, $package.Length)
            } else {
                Ok '已安装版本与安装包内版本一致'
            }
        }
    } else {
        Warn "未安装：$targetDll"
    }

    if (Test-Path -LiteralPath $configFile) {
        Ok "配置文件：$configFile"
    } else {
        Note "配置文件将在第一次进游戏加载插件后自动生成：$configFile"
    }

    $backupPattern = Join-Path $script:BackupDir "$script:AddonFolder-*.dll"
    $backups = Get-ChildItem -Path $backupPattern -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
    if ($null -ne $backups -and @($backups).Count -gt 0) {
        Note ("可用备份 {0} 个，最近一个：{1}" -f @($backups).Count, @($backups)[0].Name)
    }
}

# ---------------------------------------------------------------------------------------------------
# 安装 / 回退
# ---------------------------------------------------------------------------------------------------
# ---------------------------------------------------------------------------------------------------
# 绑定标识迁移（v1.3 的 "MarkerCycle-循环放置" -> v1.4 的 "MarkerCycle-循环1"）
# ---------------------------------------------------------------------------------------------------
function Migrate-BindIdentifiers([string]$Dir) {
    $nexusDir = Join-Path (Join-Path $Dir 'addons') 'Nexus'
    $json = Join-Path $nexusDir 'InputBinds.json'
    if (-not (Test-Path -LiteralPath $json)) {
        Note '没有找到 Nexus\InputBinds.json（还没在 Nexus 里设过键？），跳过键位迁移'
        return
    }

    $oldId = 'MarkerCycle-循环放置'
    $newId = 'MarkerCycle-循环1'
    $text = [System.IO.File]::ReadAllText($json, [System.Text.Encoding]::UTF8)
    if ($text.IndexOf($oldId) -lt 0) {
        Ok '键位不需要迁移（没有旧标识）'
        return
    }

    if (-not (Test-Path -LiteralPath $script:BackupDir)) {
        New-Item -ItemType Directory -Path $script:BackupDir -Force | Out-Null
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = Join-Path $script:BackupDir ("InputBinds-{0}.json" -f $stamp)
    Copy-Item -LiteralPath $json -Destination $backup -Force
    Note "Nexus 键位文件已备份到 backups\$(Split-Path $backup -Leaf)"

    if ($text.IndexOf('"' + $newId + '"') -lt 0) {
        # 新标识还没出现：直接把旧条目改名，键位原样保留
        $text = $text.Replace('"' + $oldId + '"', '"' + $newId + '"')
        [System.IO.File]::WriteAllText($json, $text, (New-Object System.Text.UTF8Encoding($false)))
        Ok ("键位已迁移：{0} -> {1}（原有快捷键保留）" -f $oldId, $newId)
        return
    }

    # 新标识已经存在：删掉旧条目，避免同一个标识出现两条
    $pattern = '(?s)\{\s*"Alt":\s*(?:true|false),\s*"Code":\s*\d+,[^{}]*?"Identifier":\s*"' + [regex]::Escape($oldId) + '",[^{}]*?\}\s*,?\s*'
    $old = $text
    $text = [regex]::Replace($text, $pattern, '')
    $text = [regex]::Replace($text, ',\s*\]', ']')
    if ($text -eq $old) {
        Warn '没能自动删除旧的键位条目，请在 Nexus「输入绑定」里手动清掉 MarkerCycle-循环放置'
        return
    }
    [System.IO.File]::WriteAllText($json, $text, (New-Object System.Text.UTF8Encoding($false)))
    Ok ("已清理旧标识 {0} 的残留条目（新标识 {1} 已在用）" -f $oldId, $newId)
}

function Do-Install([string]$Dir) {
    if (-not (Test-Path -LiteralPath $script:PackageDll)) {
        Fail "安装包里缺少 DLL：$script:PackageDll"
    }

    $addonsDir = Join-Path $Dir 'addons'
    if (-not (Test-Path -LiteralPath $addonsDir)) {
        Fail "游戏目录里没有 addons 文件夹：$addonsDir"
    }

    $targetDll = Join-Path $addonsDir $script:DllName
    Assert-DllWritable $targetDll

    if ($DryRun) {
        Step '预演（-DryRun，不写任何文件）'
        Note "将安装：$($script:PackageDll) -> $targetDll"
        return
    }

    Step '备份旧版本'
    if (Test-Path -LiteralPath $targetDll) {
        if (-not (Test-Path -LiteralPath $script:BackupDir)) {
            New-Item -ItemType Directory -Path $script:BackupDir -Force | Out-Null
        }
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $backupFile = Join-Path $script:BackupDir ("{0}-{1}.dll" -f $script:AddonFolder, $stamp)
        Copy-Item -LiteralPath $targetDll -Destination $backupFile -Force
        Ok "旧版本已备份到 backups\$(Split-Path $backupFile -Leaf)"
    } else {
        Note '没有旧版本，跳过备份'
    }

    Step '复制插件'
    Copy-Item -LiteralPath $script:PackageDll -Destination $targetDll -Force
    $sourceSize = (Get-Item -LiteralPath $script:PackageDll).Length
    $targetSize = (Get-Item -LiteralPath $targetDll).Length
    if ($sourceSize -ne $targetSize) {
        Fail "复制后大小不一致（源 $sourceSize 字节，目标 $targetSize 字节），请检查目标路径是否被占用。"
    }
    Ok "已安装：$targetDll（$targetSize 字节）"

    Step '迁移旧版绑定标识（如果有）'
    Migrate-BindIdentifiers $Dir

    Step '完成'
    Note '1) 启动游戏，Nexus 会自动加载本插件（插件库里能看到“一键循环标记”）'
    Note '2) 在 Nexus 里打开「输入绑定 / Keybinds」，找到“MarkerCycle-循环1”，设置你想要的快捷键'
    Note '3) 循环1 的默认快捷键是 CTRL+E，其它循环默认没绑键（想用哪个就给它点一个键）'
    Note '4) 在 Nexus 的「插件」页找到“一键循环标记”，点「设置」即可编辑循环列表（可增删、数量不限，改完立刻生效）'
    Note '   8 个循环常驻、互不影响：到「输入绑定」里给 MarkerCycle-循环2/3/… 设键就多一个循环，'
    Note '   不用新建；面板最上面的下拉框用来切换“编辑哪一个循环”。'
    Note "5) 配置文件（可选）：$(Join-Path (Join-Path $addonsDir $script:ConfigDir) $script:ConfigFile)"
    Note '   面板里的改动会自动写回该文件；也可以直接手改，格式见 说明.txt 第七节'
    Note '   想存多套/切换/导出导入：面板最下面有「配置管理」折叠段，说明见 说明.txt 第八节'
    Show-Status $Dir
}

function Do-Rollback([string]$Dir) {
    $addonsDir = Join-Path $Dir 'addons'
    $targetDll = Join-Path $addonsDir $script:DllName
    $configDir = Join-Path $addonsDir $script:ConfigDir

    Step '回退（卸载）'
    if ($DryRun) {
        Note "将删除：$targetDll"
        if ($PurgeConfig) { Note "将删除配置目录：$configDir" }
        return
    }

    if (Test-Path -LiteralPath $targetDll) {
        Assert-DllWritable $targetDll
        Remove-Item -LiteralPath $targetDll -Force
        Ok "已删除：$targetDll"
    } else {
        Note '没有已安装的 DLL，跳过'
    }

    if ($PurgeConfig) {
        if (Test-Path -LiteralPath $configDir) {
            Remove-Item -LiteralPath $configDir -Recurse -Force
            Ok "已删除配置目录：$configDir"
        } else {
            Note '没有配置目录，跳过'
        }
    } else {
        if (Test-Path -LiteralPath $configDir) {
            Note "配置目录保留（如需一并删除，加 -PurgeConfig）：$configDir"
        }
    }
}

# ---------------------------------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($Action)) {
    if ([Console]::IsInputRedirected) {
        $Action = 'install'
    } else {
        Write-Host ''
        Write-Host "  $script:AddonDisplay ($script:AddonFolder) 安装工具" -ForegroundColor Cyan
        Write-Host '  ------------------------------------------------'
        Write-Host '  前提：游戏必须先装好 Nexus 插件管理工具，否则 GW2 不会加载插件。' -ForegroundColor Yellow
        Write-Host '   1) 安装 / 更新'
        Write-Host '   2) 回退 / 卸载'
        Write-Host '   3) 查看状态'
        Write-Host '   0) 退出'
        $choice = Read-Host '  请输入序号后回车'
        switch ($choice.Trim()) {
            '1' { $Action = 'install' }
            '2' { $Action = 'rollback' }
            '3' { $Action = 'status' }
            default { Write-Host '  已取消。' -ForegroundColor Gray; exit 0 }
        }
    }
}

switch ($Action.Trim().ToLower()) {
    'install'  { $Action = 'install' }
    '安装'     { $Action = 'install' }
    'rollback' { $Action = 'rollback' }
    '回退'     { $Action = 'rollback' }
    '卸载'     { $Action = 'rollback' }
    'status'   { $Action = 'status' }
    '状态'     { $Action = 'status' }
    default    { Fail "未知操作：$Action（可用：install / rollback / status，或 安装 / 回退 / 状态）" }
}

$dir = Resolve-GameDir
Assert-Game $dir
Assert-Nexus $dir

switch ($Action) {
    'install' {
        Assert-NotRunning
        Do-Install $dir
    }
    'rollback' { Do-Rollback $dir }
    'status'   { Show-Status $dir }
}

Write-Host ''
Write-Host '  完成。' -ForegroundColor Green
exit 0
