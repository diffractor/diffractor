
rem set VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Enterprise
set VSDIR=%VSINSTALLDIR%
set TOOLSDIR=tools
set MSBUILDDIR=%VSDIR%\Msbuild\Current\Bin

Call  "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat"


del  diffractor-setup.exe
del  diffractor-setup-test.exe
del  diffractor.zip

"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=Win32
if %errorlevel% neq 0 exit /b %errorlevel%

"%MSBUILDDIR%\msbuild" df.sln /p:Configuration=Release /p:Platform=x64
if %errorlevel% neq 0 exit /b %errorlevel%

TIMEOUT 1

"%TOOLSDIR%\signtool" sign /fd SHA1 /t http://timestamp.sectigo.com /a /d Diffractor /n Zachariah exe\diffractor.exe
if %errorlevel% neq 0 exit /b %errorlevel%
TIMEOUT 1

"%TOOLSDIR%\signtool" sign /fd SHA256 /tr http://timestamp.sectigo.com/?td=sha256 /td sha256 /as /d Diffractor /n Zachariah exe\diffractor.exe
if %errorlevel% neq 0 exit /b %errorlevel%
TIMEOUT 1

"%TOOLSDIR%\signtool" sign /fd SHA1 /t http://timestamp.sectigo.com /a /d Diffractor /n Zachariah exe\diffractor64.exe
if %errorlevel% neq 0 exit /b %errorlevel%
TIMEOUT 1

"%TOOLSDIR%\signtool" sign /fd SHA256 /tr http://timestamp.sectigo.com/?td=sha256 /td sha256 /as /d Diffractor /n Zachariah exe\diffractor64.exe
if %errorlevel% neq 0 exit /b %errorlevel%
TIMEOUT 1

"C:\Program Files (x86)\NSIS\makensis.exe" installer\diff.nsi
if %errorlevel% neq 0 exit /b %errorlevel%

"%TOOLSDIR%\signtool" sign /fd SHA1 /t http://timestamp.sectigo.com /a /d Diffractor /n Zachariah diffractor-setup.exe
if %errorlevel% neq 0 exit /b %errorlevel%
TIMEOUT 1

"%TOOLSDIR%\signtool" sign /fd SHA256 /tr http://timestamp.sectigo.com?td=sha256 /td sha256 /as /d Diffractor /n Zachariah diffractor-setup.exe
if %errorlevel% neq 0 exit /b %errorlevel%
TIMEOUT 1

"%TOOLSDIR%\symstore" add /r /f exe\diffractor.exe /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor.pdb /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor64.exe /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%
"%TOOLSDIR%\symstore" add /r /f exe\diffractor64.pdb /s c:\code\symbols /t Diffractor /compress
if %errorlevel% neq 0 exit /b %errorlevel%

copy diffractor-setup.exe diffractor-setup-test.exe

cd exe
"C:\Program Files\7-Zip\7z.exe" a -tzip -mx9 ../diffractor.zip diffractor.exe diffractor64.exe diffractor-tools.json location-countries.txt location-places.txt location-states.txt languages/de.po languages/it.po dictionaries/en_US.aff dictionaries/en_US.dic
if %errorlevel% neq 0 exit /b %errorlevel%
cd ..

