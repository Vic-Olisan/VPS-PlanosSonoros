Get-ChildItem -Path $PSScriptRoot -Filter "*.vcxproj" -Recurse | ForEach-Object {
    (Get-Content $_.FullName) -replace '<PlatformToolset>v143</PlatformToolset>', '<PlatformToolset>v145</PlatformToolset>' | Set-Content $_.FullName -Encoding UTF8
    Write-Host "Atualizado para v145:" $_.FullName
}
Write-Host "Pronto! Agora abra as solucoes no Visual Studio e compile os projetos _VST3."
