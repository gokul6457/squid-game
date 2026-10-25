@echo off
title Campus Quest server
cd /d "%~dp0"
node -e "if (Number(process.versions.node.split('.')[0]) < 22 || (Number(process.versions.node.split('.')[0]) === 22 && Number(process.versions.node.split('.')[1]) < 13)) { console.error('Campus Quest requires Node.js 22.13 or newer.'); process.exit(1); }"
if errorlevel 1 (
  pause
  exit /b 1
)
echo Starting Campus Quest... (keep this window open while students play)
start "" http://localhost:4000
node server.js
pause
