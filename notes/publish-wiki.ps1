# publish-wiki.ps1
# Run this script to publish the contents of this folder to the GitHub Wiki.
# NOTE: Ensure you have visited the "Wiki" tab on GitHub and created at least one page first, 
# so that the wiki repository exists.

$wikiUrl = "https://github.com/shakilapr/etrike.wiki.git"
$tempDir = "wiki_temp"

Write-Host "Cloning the wiki repository..."
git clone $wikiUrl $tempDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to clone wiki repo. Make sure you've initialized the Wiki on GitHub by creating the first page." -ForegroundColor Red
    exit
}

Write-Host "Copying files to the wiki repository..."
Get-ChildItem -Path . | Where-Object { $_.Name -ne $tempDir -and $_.Name -ne "publish-wiki.ps1" } | Copy-Item -Destination $tempDir -Recurse -Force

Set-Location -Path $tempDir

Write-Host "Committing and pushing to GitHub Wiki..."
git add .
git commit -m "Update wiki from notes directory"
git push

Set-Location -Path ..
Write-Host "Cleaning up temporary directory..."
Remove-Item -Path $tempDir -Recurse -Force

Write-Host "Wiki published successfully!" -ForegroundColor Green
