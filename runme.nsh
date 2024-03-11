# This UEFI Shell script recurses through all files present on a volume
# and executes the \AmiNtfsBug.efi application for every file found.
@echo -off
if /s %1 == "" then
  set -v DIR %cwd%
else
  set -v DIR %1
endif

if exist("%DIR%") then
  for %a in "%DIR%\*"
    # Available(...) differentiates files from directories
    if Available("%a") then
      \AmiNtfsBug.efi "%a"
    else
      # The UEFI Shell does not remove the . and .. directories
      # and we need to remove other directories as well...
      cd "%DIR%\.."
      set -v PARENTDIR %cwd%
      cd \
      if /s /i "%a" ne "%PARENTDIR%\" then
      if /s /i "%a" ne "%DIR%\" then
      if /s /i "%a" ne "%cwd%\System Volume Information" then
      if /s /i "%a" ne "%cwd%\$Extend" then
        %0 "%a"
      endif
      endif
      endif
      endif
    endif
  endfor
endif
