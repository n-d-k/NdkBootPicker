# NdkBootPicker for  Offical OpenCore  [![Build Status](https://travis-ci.org/n-d-k/NdkBootPicker.svg?branch=master)](https://travis-ci.org/n-d-k/NdkBootPicker)


Initial release external NdkBootPicker.efi for Official OpenCore.

- Bios Date/time, auto boot to the same OS or manual set to always boot one OS mode, and OC version are displayed in boot picker.
- Auto boot to previous booted OS (if Misc->Security->AllowSetDefault is NO/false).
- macOS Recovery/Tools Entries are hidden by default, use Spacebar in Boot Menu as a toggle on/off to show/hide hidden entries.
- Screenshot via F10 function is available if OC will support OC_INPUT_F10.


Instruction:

  * Binary release are  compiled by Travis Ci server and posted in the Releases tab.
  * NdkBootPicker.efi need to be in EFI/OC/Drivers and set to load in config.plist
  * Icons folder need to be into  EFI/OC/ folder
  * Config.plist set Misc->Boot->PickerMode = External
  * To build NdkBootPicker.efi, run "./ndk-macbuild.tool" at Terminal (require Xcode and Xcode Command Line Tool installed, and open xcode to accept license agreement before compiling).
  
