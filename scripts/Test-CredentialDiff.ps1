<#
.SYNOPSIS
检测 diff 中新引入的凭据形状赋值；同文件原样保留的赋值不因注释或改名重复报警。
.DESCRIPTION
按文件分别对比删除行和新增行的赋值多重集。新增值、增加副本或移到另一文件仍会阻断。
只返回布尔值，不将匹配内容写入诊断；工作流运行时表达式保持原有豁免。
#>
function Test-CredentialDiff {
    param([string[]]$Diff)
    $pattern = '(?i)(?:password|passwd|secret|token|verification[-_ ]?code|email)\s*[:=]\s*[^\s<]{3,}'
    $removed = [System.Collections.Generic.List[string]]::new()
    $added = [System.Collections.Generic.List[string]]::new()
    foreach ($line in @($Diff) + @('diff --git END END')) {
        if ($line.StartsWith('diff --git ')) {
            foreach ($value in $added) {
                if (-not $removed.Remove($value)) { return $true }
            }
            $removed.Clear()
            $added.Clear()
        } elseif ($line -match '^[+-](?!\+\+|--)') {
            $normalized = [regex]::Replace($line.Substring(1), '\$\{\{[^}]+\}\}', '<runtime>')
            foreach ($match in [regex]::Matches($normalized, $pattern)) {
                if ($line[0] -eq '-') { $removed.Add($match.Value) }
                else { $added.Add($match.Value) }
            }
        }
    }
    return $false
}
