param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
if ($env:W64DEVKIT) { $env:PATH = (Join-Path $env:W64DEVKIT 'bin') + ';' + $env:PATH }
if (!(Get-Command $Compiler -ErrorAction SilentlyContinue)) {
    if (Test-Path -LiteralPath 'C:\w64devkit\bin\g++.exe') { $Compiler = 'C:\w64devkit\bin\g++.exe' }
    else { throw 'g++ not found. Add w64devkit/bin to PATH or pass -Compiler.' }
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cases = @(
    @{ Name='humanizer_test'; Sources=@('tools/humanizer_test.cpp') },
    @{ Name='offscreen_test'; Sources=@('tools/offscreen_test.cpp') },
    @{ Name='aim_time_test'; Sources=@('tools/aim_time_test.cpp','src/core/bind.cpp','src/func/aim/fov.cpp') },
    @{ Name='mouse_settings_test'; Sources=@('tools/mouse_settings_test.cpp','src/core/memory.cpp') },
    @{ Name='bone_budget_test'; Sources=@('tools/bone_budget_test.cpp') },
    @{ Name='bone_perf_test'; Sources=@('tools/bone_perf_test.cpp','src/core/game.cpp','src/core/memory.cpp') },
    @{ Name='geometry_test'; Sources=@('tools/geometry_test.cpp') },
    @{ Name='aimsim'; Sources=@('tools/aimsim.cpp','src/core/bind.cpp','src/func/aim/fov.cpp') },
    @{ Name='trigger_test'; Sources=@('tools/trigger_test.cpp','src/core/bind.cpp') },
    @{ Name='config_test'; Sources=@('tools/config_test.cpp','src/core/config.cpp') },
    @{ Name='fov_test'; Sources=@('tools/fov_test.cpp','src/func/aim/fov.cpp') },
    @{ Name='override_test'; Sources=@('tools/override_test.cpp') },
    @{ Name='quickstop_test'; Sources=@('tools/quickstop_test.cpp','src/func/misc/quickstop.cpp','src/func/misc/input.cpp') },
    @{ Name='quickstop_state_test'; Sources=@('tools/quickstop_state_test.cpp','src/func/misc/input.cpp') },
    @{ Name='bhop_state_test'; Sources=@('tools/bhop_state_test.cpp','src/func/misc/input.cpp') },
    @{ Name='esp_test'; Sources=@('tools/esp_test.cpp','src/func/esp/esp.cpp','dependencies/imgui/imgui.cpp','dependencies/imgui/imgui_draw.cpp','dependencies/imgui/imgui_tables.cpp','dependencies/imgui/imgui_widgets.cpp') }
)
Push-Location -LiteralPath $root
try {
    $testBuild = Join-Path $root '.local\test-build'
    New-Item -ItemType Directory -Path $testBuild -Force | Out-Null
    foreach ($case in $cases) {
        $output = Join-Path $testBuild ($case.Name + '.exe')
        $arguments = @('-std=c++20','-O0','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-I','src','-I','dependencies','-I','dependencies/imgui') +
                     $case.Sources + @('-o',$output,'-luser32','-lgdi32')
        & $compiler @arguments
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $($case.Name)" }
        & $output
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $($case.Name)" }
    }
    Write-Output "All $($cases.Count) offline suites passed."
} finally { Pop-Location }
