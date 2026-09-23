[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$SourceText = [Console]::In.ReadToEnd()
$ParseTokens = $null
$ParseErrors = $null
$Ast = [System.Management.Automation.Language.Parser]::ParseInput($SourceText, [ref]$ParseTokens, [ref]$ParseErrors)
if ($ParseErrors.Count -gt 0) {
    throw ($ParseErrors | ForEach-Object <# 格式化解析错误的行号与原因。 #> { "$($_.Extent.StartLineNumber): $($_.Message)" } | Out-String)
}
# 只遍历语法树，不执行被检查的脚本或其中的脚本块。
$Nodes = @($Ast.FindAll({ param($Node)
    $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -or
    $Node -is [System.Management.Automation.Language.ScriptBlockExpressionAst]
}, $true) | ForEach-Object <# 将每个 AST 对象转换为带位置和 token 的记录。 #> {
    $IsFunction = $_ -is [System.Management.Automation.Language.FunctionDefinitionAst]
    $NodeStart = $_.Extent.StartOffset
    $NodeEnd = $_.Extent.EndOffset
    $Help = ''
    if ($IsFunction) {
        $HelpInfo = $_.GetHelpContent()
        if ($null -ne $HelpInfo) { $Help = $HelpInfo.Synopsis }
    }
    [pscustomobject]@{
        kind = $(if ($IsFunction) { 'function' } else { 'lambda' })
        name = $(if ($IsFunction) { $_.Name } else { '<scriptblock>' })
        start = $_.Extent.StartOffset
        end = $_.Extent.EndOffset
        line = $_.Extent.StartLineNumber
        text = $_.Extent.Text
        code = (@($ParseTokens | Where-Object <# 选取对象范围内的非注释 token。 #> {
            $_.Kind -notin @('Comment', 'NewLine', 'EndOfInput') -and
            $_.Extent.StartOffset -ge $NodeStart -and $_.Extent.EndOffset -le $NodeEnd
        } | ForEach-Object <# 保留 token 种类及原始内容供增量比较。 #> { "$($_.Kind):$($_.Text)" }) | ConvertTo-Json -Compress)
        help = $Help
        parameters = @($(if ($IsFunction -and $null -ne $_.Body.ParamBlock) { $_.Body.ParamBlock.Parameters }) | ForEach-Object <# 提取命名参数的变量名称。 #> {
            if ($null -ne $_) { $_.Name.VariablePath.UserPath }
        })
    }
})
[pscustomobject]@{ nodes = $Nodes; comments = @($ParseTokens | Where-Object <# 筛选原生解析器识别的注释 token。 #> { $_.Kind -eq 'Comment' } |
    ForEach-Object <# 输出注释的起止坐标和原文。 #> { [pscustomobject]@{ start = $_.Extent.StartOffset; end = $_.Extent.EndOffset; text = $_.Text } }) } |
    ConvertTo-Json -Depth 15 -Compress
