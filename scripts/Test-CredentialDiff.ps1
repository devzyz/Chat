<#
.SYNOPSIS
检测 diff 中新引入的凭据形状赋值；同文件原样保留的赋值不因注释或改名重复报警。
.DESCRIPTION
按文件分别对比删除行和新增行的赋值多重集。新增值、增加副本或移到另一文件仍会阻断。
只返回布尔值，不将匹配内容写入诊断；工作流运行时表达式保持原有豁免。
#>
function Test-CredentialDiff {
    param([string[]]$Diff)
    $assignment = '(?i)(password|passwd|secret|token|verification[-_ ]?code|email)\s*([:=])'
    $pattern = $assignment + '\s*[^\s<]{3,}'
    $continuation = ''
    $previousSide = ''
    $removed = [System.Collections.Generic.List[string]]::new()
    $added = [System.Collections.Generic.List[string]]::new()
    foreach ($line in @($Diff) + @('diff --git END END')) {
        if ($line.StartsWith('diff --git ')) {
            $continuation = ''
            foreach ($value in $added) {
                if (-not $removed.Remove($value)) { return $true }
            }
            $removed.Clear()
            $added.Clear()
        } elseif ($line -match '^[+-](?!\+\+|--)') {
            $normalized = [regex]::Replace($line.Substring(1), '\$\{\{[^}]+\}\}', '<runtime>')
            # 仅在同一 diff 块、同侧的相邻行间补全赋值，保留值内的每个字符。
            if ($continuation -and $previousSide -eq $line[0]) {
                $normalized = $continuation + $normalized.TrimStart()
            }
            $pending = [regex]::Match($normalized, $assignment + '\s*$')
            $continuation = $pending.Value.TrimEnd()
            $previousSide = $line[0]
            foreach ($match in [regex]::Matches($normalized, $pattern)) {
                $value = [regex]::Replace($match.Value, '^' + $assignment + '\s*', '$1$2')
                if ($line[0] -eq '-') { $removed.Add($value) }
                else { $added.Add($value) }
            }
        } else {
            $continuation = ''
        }
    }
    return $false
}
