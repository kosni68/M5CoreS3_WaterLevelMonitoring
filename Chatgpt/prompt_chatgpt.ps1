# --- copy_project_files.ps1 ---
# Ce script récupère le contenu de certains fichiers du projet M5CoreS3_WaterLevelMonitoring
# selon les extensions spécifiées, puis les copie dans le presse-papiers.

# Répertoires à inclure
$dirs = @(
    "..\data",
    "..\src"
)

# Fichiers spécifiques à inclure
$files = @(
    ".\Chatgpt_instruction.md",
    "..\platformio.ini"
)

# Extensions à inclure
$ext = ".md",".txt", ".cpp", ".c", ".h", ".hpp", ".html", ".css", ".js", ".ini"

# Combine les répertoires et fichiers en une seule liste
$paths = $dirs + $files

# Vérifie que le script output_Files.ps1 est dans le même dossier
$scriptPath = Join-Path -Path $PSScriptRoot -ChildPath "output_Files.ps1"

if (-Not (Test-Path $scriptPath)) {
    Write-Host "❌ Le fichier output_Files.ps1 est introuvable dans le dossier du script." -ForegroundColor Red
    exit
}

# Exécute le script avec les bons paramètres et copie la sortie dans le presse-papiers
Write-Host "⏳ Exécution du script output_Files.ps1..." -ForegroundColor Yellow
& $scriptPath -Directory $paths -Extensions $ext | Set-Clipboard

Write-Host "✅ Les fichiers ont été copiés dans le presse-papiers." -ForegroundColor Green
