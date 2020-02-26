# NdkBootPicker for  Official OpenCore  [![Build Status](https://travis-ci.org/n-d-k/NdkBootPicker.svg?branch=master)](https://travis-ci.org/n-d-k/NdkBootPicker)


Initial release external NdkBootPicker.efi for Official OpenCore.

- Bios Date/time, OC version can be displayed with Spacebar key  .
- Screen snapshot can be taken with F10.


Instruction:

  * Binary release are  compiled by Travis Ci server and posted in the Releases tab.
  * NdkBootPicker.efi need to be in EFI/OC/Drivers and set to load in config.plist *
  * Icons folder need to be into  EFI/OC/ folder and Background.png need to be resized to match the screen resolution for it to load.
  * Config.plist set Misc->Boot->PickerMode = External
  * To build NdkBootPicker.efi, run "./ndk-macbuild.tool" at Terminal (require Xcode and Xcode Command Line Tool installed, and open xcode to accept license agreement before compiling).
