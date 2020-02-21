//
//  UiMenu.c
//
//
//  Created by N-D-K on 1/24/20.
//

#include <Guid/AppleVariable.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/UgaDraw.h>
#include <Protocol/HiiFont.h>
#include <Protocol/OcInterface.h>


#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/OcDebugLogLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/OcAppleKeyMapLib.h>
#include <Library/OcBootManagementLib.h>
#include <Library/OcConsoleLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/OcPngLib.h>
#include <Library/OcFileLib.h>
#include <Library/OcStorageLib.h>
#include <Library/OcMiscLib.h>

#define NDK_BOOTPICKER_VERSION   "0.0.6"

STATIC
BOOLEAN
mAllowSetDefault;

STATIC
BOOLEAN
mHideAuxiliary;

STATIC
UINTN
mDefaultEntry;

/*========== Graphic UI Begin ==========*/

STATIC
EFI_GRAPHICS_OUTPUT_PROTOCOL *
mGraphicsOutput;

STATIC
EFI_HII_FONT_PROTOCOL *
mHiiFont;

STATIC
EFI_UGA_DRAW_PROTOCOL *
mUgaDraw;

STATIC
INTN
mScreenWidth;

STATIC
INTN
mScreenHeight;

STATIC
INTN
mFontWidth;

STATIC
INTN
mFontHeight;

STATIC
INTN
mTextHeight;

STATIC
INTN
mTextScale = 0;  // not actual scale, will be set after getting screen resolution. (16 will be no scaling, 28 will be for 4k screen)

STATIC
INTN
mUiScale = 0;

STATIC
UINTN
mIconSpaceSize;  // Default 136 pixels space to contain icons with size 128x128.

STATIC
UINTN
mIconPaddingSize;

STATIC
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *
mFileSystem = NULL;

STATIC
EFI_IMAGE_OUTPUT *
mBackgroundImage = NULL;

STATIC
EFI_IMAGE_OUTPUT *
mMenuImage = NULL;

BOOLEAN
mSelectorUsed = TRUE;

STATIC
INTN
mMenuFadeIntensity = 150;     // ranging from 0 to 255 0 = completely disappear, 255 = no fading.

/* Colors are now customized by the optional small 16x16 pixels png color example files in Icons folder (Can be anysize only 1st pixels will be used for color setting).
   font_color.png (Entry discription color and selection color)
   font_color_alt.png (All other text on screen)
   background_color.png (Background color)
   Background.png (Wallpaper bacground instead, preferly matching with screen resolution setting.)
 
   Background.png will be checked first, will use it if found in Icons foler, if not found, then background_color.png will be checked,
   if not found then 1st pixel color (top/left pixel) of icon.
 */

/*=========== Default colors settings ==============*/

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
mTransparentPixel  = {0x00, 0x00, 0x00, 0x00};

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
mBlackPixel  = {0x00, 0x00, 0x00, 0xff};

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
mDarkGray = {0x76, 0x81, 0x85, 0xff};

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
mLowWhitePixel  = {0xb8, 0xbd, 0xbf, 0xff};


// Selection and Entry's description font color
STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL *
mFontColorPixel = &mLowWhitePixel;

// Date time, Version, and other color
STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL *
mFontColorPixelAlt = &mDarkGray;

// Background color
STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL *
mBackgroundPixel = &mBlackPixel;

STATIC
VOID
FreeImage (
  IN EFI_IMAGE_OUTPUT    *Image
  )
{
  if (Image != NULL) {
    if (Image->Image.Bitmap != NULL) {
      FreePool (Image->Image.Bitmap);
      Image->Image.Bitmap = NULL;
    }
    FreePool (Image);
  }
}

STATIC
BOOLEAN
FileExist (
  IN CHAR16                        *FilePath
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;
  VOID                             *Buffer;
  UINT32                           BufferSize;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            Index;

  BufferSize = 0;
  HandleCount = 0;
  FileSystem = NULL;
  Buffer = NULL;
  
  if (mFileSystem == NULL) {
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPartTypeSystemPartGuid, NULL, &HandleCount, &Handles);
    if (!EFI_ERROR (Status) && HandleCount > 0) {
      for (Index = 0; Index < HandleCount; ++Index) {
        Status = gBS->HandleProtocol (
                        Handles[Index],
                        &gEfiSimpleFileSystemProtocolGuid,
                        (VOID **) &FileSystem
                        );
        if (EFI_ERROR (Status)) {
          FileSystem = NULL;
          continue;
        }
        
        Buffer = ReadFile (FileSystem, FilePath, &BufferSize, BASE_16MB);
        if (Buffer != NULL) {
          mFileSystem = FileSystem;
          DEBUG ((DEBUG_INFO, "OCUI: FileSystem found!  Handle(%d) \n", Index));
          break;
        }
        FileSystem = NULL;
      }
      
      if (Handles != NULL) {
        FreePool (Handles);
      }
    }
    
  } else {
    Buffer = ReadFile (mFileSystem, FilePath, &BufferSize, BASE_16MB);
  }
  
  if (Buffer != NULL) {
    FreePool (Buffer);
    return TRUE;
  }
  return FALSE;
}

STATIC
EFI_IMAGE_OUTPUT *
CreateImage (
  IN UINT16       Width,
  IN UINT16       Height
  )
{
  EFI_IMAGE_OUTPUT  *NewImage;
  
  NewImage = (EFI_IMAGE_OUTPUT *) AllocateZeroPool (sizeof (EFI_IMAGE_OUTPUT));
  
  if (NewImage == NULL) {
    return NULL;
  }
  
  if (Width * Height == 0) {
    FreeImage (NewImage);
    return NULL;
  }
  
  NewImage->Image.Bitmap = AllocateZeroPool (Width * Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (NewImage->Image.Bitmap == NULL) {
    FreePool (NewImage);
    return NULL;
  }
  
  NewImage->Width = Width;
  NewImage->Height = Height;
  
  return NewImage;
}

STATIC
VOID
RestrictImageArea (
  IN     EFI_IMAGE_OUTPUT   *Image,
  IN     INTN               AreaXpos,
  IN     INTN               AreaYpos,
  IN OUT INTN               *AreaWidth,
  IN OUT INTN               *AreaHeight
  )
{
  if (Image == NULL || AreaWidth == NULL || AreaHeight == NULL) {
    DEBUG ((DEBUG_INFO, "OCUI: invalid argument\n"));
    return;
  }

  if (AreaXpos >= Image->Width || AreaYpos >= Image->Height) {
    *AreaWidth  = 0;
    *AreaHeight = 0;
  } else {
    if (*AreaWidth > Image->Width - AreaXpos) {
      *AreaWidth = Image->Width - AreaXpos;
    }
    if (*AreaHeight > Image->Height - AreaYpos) {
      *AreaHeight = Image->Height - AreaYpos;
    }
  }
}

STATIC
VOID
DrawImageArea (
  IN EFI_IMAGE_OUTPUT  *Image,
  IN INTN              AreaXpos,
  IN INTN              AreaYpos,
  IN INTN              AreaWidth,
  IN INTN              AreaHeight,
  IN INTN              ScreenXpos,
  IN INTN              ScreenYpos
  )
{
  EFI_STATUS           Status;
  
  if (Image == NULL) {
    return;
  }
  
  if (ScreenXpos < 0 || ScreenXpos >= mScreenWidth || ScreenYpos < 0 || ScreenYpos >= mScreenHeight) {
    DEBUG ((DEBUG_INFO, "OCUI: Invalid Screen coordinate requested...x:%d - y:%d \n", ScreenXpos, ScreenYpos));
    return;
  }
  
  if (AreaWidth == 0) {
    AreaWidth = Image->Width;
  }
  
  if (AreaHeight == 0) {
    AreaHeight = Image->Height;
  }
  
  if ((AreaXpos != 0) || (AreaYpos != 0)) {
    RestrictImageArea (Image, AreaXpos, AreaYpos, &AreaWidth, &AreaHeight);
    if (AreaWidth == 0) {
      DEBUG ((DEBUG_INFO, "OCUI: invalid area position requested\n"));
      return;
    }
  }
  
  if (ScreenXpos + AreaWidth > mScreenWidth) {
    AreaWidth = mScreenWidth - ScreenXpos;
  }
  
  if (ScreenYpos + AreaHeight > mScreenHeight) {
    AreaHeight = mScreenHeight - ScreenYpos;
  }
  
  if (mGraphicsOutput != NULL) {
    Status = mGraphicsOutput->Blt(mGraphicsOutput,
                                  Image->Image.Bitmap,
                                  EfiBltBufferToVideo,
                                  (UINTN) AreaXpos,
                                  (UINTN) AreaYpos,
                                  (UINTN) ScreenXpos,
                                  (UINTN) ScreenYpos,
                                  (UINTN) AreaWidth,
                                  (UINTN) AreaHeight,
                                  (UINTN) Image->Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                                  );
  } else {
    ASSERT (mUgaDraw != NULL);
    Status = mUgaDraw->Blt(mUgaDraw,
                            (EFI_UGA_PIXEL *) Image->Image.Bitmap,
                            EfiUgaBltBufferToVideo,
                            (UINTN) AreaXpos,
                            (UINTN) AreaYpos,
                            (UINTN) ScreenXpos,
                            (UINTN) ScreenYpos,
                            (UINTN) AreaWidth,
                            (UINTN) AreaHeight,
                            (UINTN) Image->Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                            );
  }
  
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCUI: Draw Image Area...%r\n", Status));
  }
}

STATIC
VOID
RawComposeOnFlat (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL *CompBasePtr,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopBasePtr,
  IN     INTN                          Width,
  IN     INTN                          Height,
  IN     INTN                          CompLineOffset,
  IN     INTN                          TopLineOffset
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *TopPtr;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *CompPtr;
  UINT32                               TopAlpha;
  UINT32                               RevAlpha;
  UINTN                                Temp;
  INT64                                X;
  INT64                                Y;

  if (CompBasePtr == NULL || TopBasePtr == NULL) {
    return;
  }

  for (Y = 0; Y < Height; ++Y) {
    TopPtr = TopBasePtr;
    CompPtr = CompBasePtr;
    for (X = 0; X < Width; ++X) {
      TopAlpha = TopPtr->Reserved;
      RevAlpha = 255 - TopAlpha;

      Temp = ((UINT8) CompPtr->Blue * RevAlpha) + ((UINT8) TopPtr->Blue * TopAlpha);
      CompPtr->Blue = (UINT8) (Temp / 255);

      Temp = ((UINT8) CompPtr->Green * RevAlpha) + ((UINT8) TopPtr->Green * TopAlpha);
      CompPtr->Green = (UINT8) (Temp / 255);

      Temp = ((UINT8) CompPtr->Red * RevAlpha) + ((UINT8) TopPtr->Red * TopAlpha);
      CompPtr->Red = (UINT8) (Temp / 255);

      CompPtr->Reserved = (UINT8)(255);

      TopPtr++;
      CompPtr++;
    }
    TopBasePtr += TopLineOffset;
    CompBasePtr += CompLineOffset;
  }
}

STATIC
VOID
RawCopy (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL *CompBasePtr,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopBasePtr,
  IN     INTN                          Width,
  IN     INTN                          Height,
  IN     INTN                          CompLineOffset,
  IN     INTN                          TopLineOffset
  )
{
  INTN       X;
  INTN       Y;

  if (CompBasePtr == NULL || TopBasePtr == NULL) {
    return;
  }

  for (Y = 0; Y < Height; ++Y) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopPtr = TopBasePtr;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *CompPtr = CompBasePtr;
    for (X = 0; X < Width; ++X) {
      *CompPtr = *TopPtr;
      TopPtr++;
      CompPtr++;
    }
    TopBasePtr += TopLineOffset;
    CompBasePtr += CompLineOffset;
  }
}

STATIC
VOID
RawCopyAlpha (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL *CompBasePtr,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopBasePtr,
  IN     INTN                          Width,
  IN     INTN                          Height,
  IN     INTN                          CompLineOffset,
  IN     INTN                          TopLineOffset,
  IN     BOOLEAN                       Faded
  )
{
  INTN       X;
  INTN       Y;
  INTN       Alpha;
  INTN       InvAlpha;

  if (CompBasePtr == NULL || TopBasePtr == NULL) {
    return;
  }
  
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *FirstTopPtr = TopBasePtr;
  for (Y = 0; Y < Height; ++Y) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopPtr = TopBasePtr;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *CompPtr = CompBasePtr;
    for (X = 0; X < Width; ++X) {
      if ((TopPtr->Red != FirstTopPtr->Red
          && TopPtr->Blue != FirstTopPtr->Blue
          && TopPtr->Green != FirstTopPtr->Green
          && TopPtr->Reserved != FirstTopPtr->Reserved)
          || (TopPtr->Red > 5
          && TopPtr->Blue > 5
          && TopPtr->Green > 5)
          ) {
        Alpha =  Faded ? mMenuFadeIntensity + 1 : TopPtr->Reserved + 1;
        InvAlpha = Faded ? 256 - mMenuFadeIntensity : 256 - TopPtr->Reserved;
        CompPtr->Blue = (UINT8) ((TopPtr->Blue * Alpha + CompPtr->Blue * InvAlpha) >> 8);
        CompPtr->Green = (UINT8) ((TopPtr->Green * Alpha + CompPtr->Green * InvAlpha) >> 8);
        CompPtr->Red = (UINT8) ((TopPtr->Red * Alpha + CompPtr->Red * InvAlpha) >> 8);
        CompPtr->Reserved = (UINT8) (255);
      }
      TopPtr++;
      CompPtr++;
    }
    TopBasePtr += TopLineOffset;
    CompBasePtr += CompLineOffset;
  }
}

STATIC
VOID
RawCompose (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL *CompBasePtr,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL *TopBasePtr,
  IN     INTN                          Width,
  IN     INTN                          Height,
  IN     INTN                          CompLineOffset,
  IN     INTN                          TopLineOffset
  )
{
  INT64                                X;
  INT64                                Y;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *TopPtr;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *CompPtr;
  INTN                                 TopAlpha;
  INTN                                 Alpha;
  INTN                                 CompAlpha;
  INTN                                 RevAlpha;
  INTN                                 TempAlpha;

  if (CompBasePtr == NULL || TopBasePtr == NULL) {
    return;
  }

  for (Y = 0; Y < Height; ++Y) {
    TopPtr = TopBasePtr;
    CompPtr = CompBasePtr;
    for (X = 0; X < Width; ++X) {
      TopAlpha = TopPtr->Reserved & 0xFF;
      
      if (TopAlpha == 255) {
        CompPtr->Blue  = TopPtr->Blue;
        CompPtr->Green = TopPtr->Green;
        CompPtr->Red   = TopPtr->Red;
        CompPtr->Reserved = (UINT8) TopAlpha;
      } else if (TopAlpha != 0) {
        CompAlpha = CompPtr->Reserved & 0xFF;
        RevAlpha = 255 - TopAlpha;
        TempAlpha = CompAlpha * RevAlpha;
        TopAlpha *= 255;
        Alpha = TopAlpha + TempAlpha;

        CompPtr->Blue = (UINT8) ((TopPtr->Blue * TopAlpha + CompPtr->Blue * TempAlpha) / Alpha);
        CompPtr->Green = (UINT8) ((TopPtr->Green * TopAlpha + CompPtr->Green * TempAlpha) / Alpha);
        CompPtr->Red = (UINT8) ((TopPtr->Red * TopAlpha + CompPtr->Red * TempAlpha) / Alpha);
        CompPtr->Reserved = (UINT8) (Alpha / 255);
      }
      TopPtr++;
      CompPtr++;
    }
    TopBasePtr += TopLineOffset;
    CompBasePtr += CompLineOffset;
  }
}

STATIC
VOID
ComposeImage (
  IN OUT EFI_IMAGE_OUTPUT    *Image,
  IN     EFI_IMAGE_OUTPUT    *TopImage,
  IN     INTN                Xpos,
  IN     INTN                Ypos,
  IN     BOOLEAN             ImageIsAlpha,
  IN     BOOLEAN             TopImageIsAlpha
  )
{
  INTN                       CompWidth;
  INTN                       CompHeight;
  
  if (TopImage == NULL || Image == NULL) {
    return;
  }

  CompWidth  = TopImage->Width;
  CompHeight = TopImage->Height;
  RestrictImageArea (Image, Xpos, Ypos, &CompWidth, &CompHeight);

  if (CompWidth > 0) {
    if (ImageIsAlpha && mBackgroundImage == NULL) {
      ImageIsAlpha = FALSE;
    }
    if (TopImageIsAlpha) {
      if (ImageIsAlpha) {
        RawCompose (Image->Image.Bitmap + Ypos * Image->Width + Xpos,
                    TopImage->Image.Bitmap,
                    CompWidth,
                    CompHeight,
                    Image->Width,
                    TopImage->Width
                    );
      } else {
        RawComposeOnFlat (Image->Image.Bitmap + Ypos * Image->Width + Xpos,
                          TopImage->Image.Bitmap,
                          CompWidth,
                          CompHeight,
                          Image->Width,
                          TopImage->Width
                          );
      }
    } else {
      RawCopy (Image->Image.Bitmap + Ypos * Image->Width + Xpos,
               TopImage->Image.Bitmap,
               CompWidth,
               CompHeight,
               Image->Width,
               TopImage->Width
               );
    }
  }
}

STATIC
VOID
FillImage (
  IN OUT EFI_IMAGE_OUTPUT              *Image,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Color,
  IN     BOOLEAN                       IsAlpha
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        FillColor;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL        *PixelPtr;
  INTN                                 Index;
  
  if (Image == NULL || Color == NULL) {
    return;
  }
  

  if (!IsAlpha) {
    FillColor.Reserved = 0;
  }

  FillColor = *Color;

  PixelPtr = Image->Image.Bitmap;
  for (Index = 0; Index < Image->Width * Image->Height; ++Index) {
    *PixelPtr++ = FillColor;
  }
}

STATIC
EFI_IMAGE_OUTPUT *
CreateFilledImage (
  IN INTN                          Width,
  IN INTN                          Height,
  IN BOOLEAN                       IsAlpha,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Color
  )
{
  EFI_IMAGE_OUTPUT      *NewImage;
  
  NewImage = CreateImage (Width, Height);
  if (NewImage == NULL) {
    return NULL;
  }
  
  FillImage (NewImage, Color, IsAlpha);
  
  return NewImage;
}

STATIC
VOID
GetGlyph (
  VOID
  )
{
  EFI_STATUS            Status;
  EFI_IMAGE_OUTPUT      *Blt;
  
  Blt = NULL;
  
  Status = mHiiFont->GetGlyph (mHiiFont,
                               L'Z',
                               NULL,
                               &Blt,
                               NULL
                               );
  if (!EFI_ERROR (Status)) {
    mFontWidth = Blt->Width;
    mFontHeight = Blt->Height;
    mTextHeight = mFontHeight + 1;
    DEBUG ((DEBUG_INFO, "OCUI: Got system fontsize - w:%dxh:%d\n", Blt->Width, Blt->Height));
    FreeImage (Blt);
  }
}

STATIC
EFI_IMAGE_OUTPUT *
CopyImage (
  IN EFI_IMAGE_OUTPUT   *Image
  )
{
  EFI_IMAGE_OUTPUT      *NewImage;
  if (Image == NULL || (Image->Width * Image->Height) == 0) {
    return NULL;
  }

  NewImage = CreateImage (Image->Width, Image->Height);
  if (NewImage == NULL) {
    return NULL;
  }
  
  CopyMem (NewImage->Image.Bitmap, Image->Image.Bitmap, (UINTN) (Image->Width * Image->Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)));
  return NewImage;
}

STATIC
EFI_IMAGE_OUTPUT *
CopyScaledImage (
  IN EFI_IMAGE_OUTPUT  *OldImage,
  IN INTN              Ratio
  )
{
  BOOLEAN                             Grey = FALSE;
  EFI_IMAGE_OUTPUT                    *NewImage;
  INTN                                x, x0, x1, x2, y, y0, y1, y2;
  INTN                                NewH, NewW;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL       *Dest;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL       *Src;
  INTN                                OldW;

  if (Ratio < 0) {
    Ratio = -Ratio;
    Grey = TRUE;
  }

  if (OldImage == NULL) {
    return NULL;
  }
  Src =  OldImage->Image.Bitmap;
  OldW = OldImage->Width;

  NewW = (OldImage->Width * Ratio) >> 4;
  NewH = (OldImage->Height * Ratio) >> 4;


  if (Ratio == 16) {
    NewImage = CopyImage (OldImage);
  } else {
    NewImage = CreateImage (NewW, NewH);
    if (NewImage == NULL)
      return NULL;

    Dest = NewImage->Image.Bitmap;
    for (y = 0; y < NewH; y++) {
      y1 = (y << 4) / Ratio;
      y0 = ((y1 > 0) ? (y1-1) : y1) * OldW;
      y2 = ((y1 < (OldImage->Height - 1)) ? (y1+1) : y1) * OldW;
      y1 *= OldW;
      for (x = 0; x < NewW; x++) {
        x1 = (x << 4) / Ratio;
        x0 = (x1 > 0) ? (x1 - 1) : x1;
        x2 = (x1 < (OldW - 1)) ? (x1+1) : x1;
        Dest->Blue = (UINT8)(((INTN)Src[x1+y1].Blue * 2 + Src[x0+y1].Blue +
                           Src[x2+y1].Blue + Src[x1+y0].Blue + Src[x1+y2].Blue) / 6);
        Dest->Green = (UINT8)(((INTN)Src[x1+y1].Green * 2 + Src[x0+y1].Green +
                           Src[x2+y1].Green + Src[x1+y0].Green + Src[x1+y2].Green) / 6);
        Dest->Red = (UINT8)(((INTN)Src[x1+y1].Red * 2 + Src[x0+y1].Red +
                           Src[x2+y1].Red + Src[x1+y0].Red + Src[x1+y2].Red) / 6);
        Dest->Reserved = Src[x1+y1].Reserved;
        Dest++;
      }
    }
  }
  if (Grey) {
    Dest = NewImage->Image.Bitmap;
    for (y = 0; y < NewH; y++) {
      for (x = 0; x < NewW; x++) {
        Dest->Blue = (UINT8)((INTN)((UINTN)Dest->Blue + (UINTN)Dest->Green + (UINTN)Dest->Red) / 3);
        Dest->Green = Dest->Red = Dest->Blue;
        Dest++;
      }
    }
  }

  return NewImage;
}

STATIC
VOID
TakeImage (
  IN EFI_IMAGE_OUTPUT  *Image,
  IN INTN              ScreenXpos,
  IN INTN              ScreenYpos,
  IN INTN              AreaWidth,
  IN INTN              AreaHeight
  )
{
  EFI_STATUS           Status;
  
  if (ScreenXpos + AreaWidth > mScreenWidth) {
    AreaWidth = mScreenWidth - ScreenXpos;
  }
  
  if (ScreenYpos + AreaHeight > mScreenHeight) {
    AreaHeight = mScreenHeight - ScreenYpos;
  }
    
  if (mGraphicsOutput != NULL) {
    Status = mGraphicsOutput->Blt(mGraphicsOutput,
                                  (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) Image->Image.Bitmap,
                                  EfiBltVideoToBltBuffer,
                                  ScreenXpos,
                                  ScreenYpos,
                                  0,
                                  0,
                                  AreaWidth,
                                  AreaHeight,
                                  (UINTN) Image->Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                                  );
  } else {
    ASSERT (mUgaDraw != NULL);
    Status = mUgaDraw->Blt(mUgaDraw,
                           (EFI_UGA_PIXEL *) Image->Image.Bitmap,
                           EfiUgaVideoToBltBuffer,
                           ScreenXpos,
                           ScreenYpos,
                           0,
                           0,
                           AreaWidth,
                           AreaHeight,
                           (UINTN) Image->Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                           );
  }
}

STATIC
VOID
CreateMenuImage (
  IN EFI_IMAGE_OUTPUT    *Icon,
  IN UINTN               IconCount
  )
{
  EFI_IMAGE_OUTPUT       *NewImage;
  UINT16                 Width;
  UINT16                 Height;
  BOOLEAN                IsTwoRow;
  UINTN                  IconsPerRow;
  INTN                   Xpos;
  INTN                   Ypos;
  
  NewImage = NULL;
  Xpos = 0;
  Ypos = 0;
  
  if (mMenuImage != NULL) {
    Width = mMenuImage->Width;
    Height = mMenuImage->Height;
    IsTwoRow = mMenuImage->Height > mIconSpaceSize;
    
    if (IsTwoRow) {
      IconsPerRow = mMenuImage->Width / mIconSpaceSize;
      Xpos = (IconCount - IconsPerRow) * mIconSpaceSize;
      Ypos = mIconSpaceSize;
    } else {
      if (mMenuImage->Width + (mIconSpaceSize * 2) <= mScreenWidth) {
        Width = mMenuImage->Width + mIconSpaceSize;
        Xpos = mMenuImage->Width;
      } else {
        Height = mMenuImage->Height + mIconSpaceSize;
        Ypos = mIconSpaceSize;
      }
    }
  } else {
    Width = mIconSpaceSize;
    Height = Width;
  }
  
  NewImage = CreateFilledImage (Width, Height, TRUE, &mTransparentPixel);
  if (NewImage == NULL) {
    return;
  }
  
  if (mMenuImage != NULL) {
    ComposeImage (NewImage, mMenuImage, 0, 0, TRUE, TRUE);
    if (mMenuImage != NULL) {
      FreeImage (mMenuImage);
    }
  }
  
  ComposeImage (NewImage, Icon, Xpos + mIconPaddingSize, Ypos + mIconPaddingSize, TRUE, TRUE);
  if (Icon != NULL) {
    FreeImage (Icon);
  }
  
  mMenuImage = NewImage;
}

STATIC
VOID
BltImageAlpha (
  IN EFI_IMAGE_OUTPUT              *Image,
  IN INTN                          Xpos,
  IN INTN                          Ypos,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BackgroundPixel,
  IN INTN                          Scale
  )
{
  EFI_IMAGE_OUTPUT    *CompImage;
  EFI_IMAGE_OUTPUT    *NewImage;
  INTN                Width;
  INTN                Height;
  
  NewImage = NULL;
  Width    = Scale << 3;
  Height   = Width;

  if (Image != NULL) {
    NewImage = CopyScaledImage (Image, Scale);
    Width = NewImage->Width;
    Height = NewImage->Height;
  }

  CompImage = CreateFilledImage (Width, Height, (mBackgroundImage != NULL), BackgroundPixel);
  ComposeImage (CompImage, NewImage, 0, 0, (mBackgroundImage != NULL), TRUE);
  if (NewImage != NULL) {
    FreeImage (NewImage);
  }
  if (mBackgroundImage == NULL) {
    DrawImageArea (CompImage, 0, 0, 0, 0, Xpos, Ypos);
    FreeImage (CompImage);
    return;
  }
  
  // Background Image was used.
  NewImage = CreateImage (Width, Height);
  if (NewImage == NULL) {
    return;
  }
  RawCopy (NewImage->Image.Bitmap,
           mBackgroundImage->Image.Bitmap + Ypos * mBackgroundImage->Width + Xpos,
           Width,
           Height,
           Width,
           mBackgroundImage->Width
           );
  // Compose
  ComposeImage (NewImage, CompImage, 0, 0, FALSE, (mBackgroundImage != NULL));
  FreeImage (CompImage);
  // Draw to screen
  DrawImageArea (NewImage, 0, 0, 0, 0, Xpos, Ypos);
  FreeImage (NewImage);
}

STATIC
VOID
BltImage (
  IN EFI_IMAGE_OUTPUT    *Image,
  IN INTN                Xpos,
  IN INTN                Ypos
  )
{
  if (Image == NULL) {
    return;
  }
  
  DrawImageArea (Image, 0, 0, 0, 0, Xpos, Ypos);
}

STATIC
VOID
BltMenuImage (
  IN EFI_IMAGE_OUTPUT    *Image,
  IN INTN                Xpos,
  IN INTN                Ypos
  )
{
  EFI_IMAGE_OUTPUT       *NewImage;
  
  if (Image == NULL) {
    return;
  }
  
  NewImage = CreateImage (Image->Width, Image->Height);
  if (NewImage == NULL) {
    return;
  }
  
  RawCopy (NewImage->Image.Bitmap,
           mBackgroundImage->Image.Bitmap + Ypos * mBackgroundImage->Width + Xpos,
           Image->Width,
           Image->Height,
           Image->Width,
           mBackgroundImage->Width
           );
  
  RawCopyAlpha (NewImage->Image.Bitmap,
                Image->Image.Bitmap,
                NewImage->Width,
                NewImage->Height,
                NewImage->Width,
                Image->Width,
                TRUE
                );
  
  DrawImageArea (NewImage, 0, 0, 0, 0, Xpos, Ypos);
  FreeImage (NewImage);
}

STATIC
EFI_IMAGE_OUTPUT *
CreatTextImage (
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Foreground,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Background,
  IN CHAR16                           *Buffer,
  IN BOOLEAN                           Scale
  )
{
  EFI_STATUS                          Status;
  EFI_IMAGE_OUTPUT                    *Blt;
  EFI_IMAGE_OUTPUT                    *ScaledBlt;
  EFI_FONT_DISPLAY_INFO               FontDisplayInfo;
  EFI_HII_ROW_INFO                    *RowInfoArray;
  UINTN                               RowInfoArraySize;
  
  RowInfoArray  = NULL;
  ScaledBlt = NULL;

  Blt = (EFI_IMAGE_OUTPUT *) AllocateZeroPool (sizeof (EFI_IMAGE_OUTPUT));
  if (Blt == NULL) {
    DEBUG ((DEBUG_INFO, "OCUI: Failed to allocate memory pool.\n"));
    return NULL;
  }
  
  Blt->Width = StrLen (Buffer) * mFontWidth;
  Blt->Height = mFontHeight;
  
  Blt->Image.Bitmap = AllocateZeroPool (Blt->Width * Blt->Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  
  if (Blt->Image.Bitmap == NULL) {
    DEBUG ((DEBUG_INFO, "OCUI: Failed to allocate memory pool for Bitmap.\n"));
    FreeImage (Blt);
    return NULL;
  }
  
  ZeroMem (&FontDisplayInfo, sizeof (EFI_FONT_DISPLAY_INFO));
  CopyMem (&FontDisplayInfo.ForegroundColor, Foreground, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  CopyMem (&FontDisplayInfo.BackgroundColor, Background, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  
  Status = mHiiFont->StringToImage (
                          mHiiFont,
                          EFI_HII_IGNORE_IF_NO_GLYPH | EFI_HII_OUT_FLAG_CLIP |
                          EFI_HII_OUT_FLAG_CLIP_CLEAN_X | EFI_HII_OUT_FLAG_CLIP_CLEAN_Y |
                          EFI_HII_IGNORE_LINE_BREAK,
                          Buffer,
                          &FontDisplayInfo,
                          &Blt,
                          0,
                          0,
                          &RowInfoArray,
                          &RowInfoArraySize,
                          NULL
                          );
  
  if (!EFI_ERROR (Status) && !Scale) {
    if (RowInfoArray != NULL) {
      FreePool (RowInfoArray);
    }
    return Blt;
  }
  
  if (!EFI_ERROR (Status)) {
    ScaledBlt = CopyScaledImage (Blt, mTextScale);
    if (ScaledBlt == NULL) {
      DEBUG ((DEBUG_INFO, "OCUI: Failed to scale image!\n"));
      if (RowInfoArray != NULL) {
        FreePool (RowInfoArray);
      }
      FreeImage (Blt);
    }
  }
  
  if (RowInfoArray != NULL) {
    FreePool (RowInfoArray);
  }
    
  FreeImage (Blt);
    
  return ScaledBlt;
}

STATIC
VOID
PrintTextGraphicXY (
  IN CHAR16                           *String,
  IN INTN                             Xpos,
  IN INTN                             Ypos,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *FontColor
  )
{
  EFI_IMAGE_OUTPUT                    *TextImage;

  TextImage = CreatTextImage (FontColor, &mTransparentPixel, String, TRUE);
  if (TextImage == NULL) {
    return;
  }
  
  if ((Xpos + TextImage->Width + 8) > mScreenWidth) {
    Xpos = mScreenWidth - (TextImage->Width + 8);
  }
  
  if ((Ypos + TextImage->Height + 5) > mScreenHeight) {
    Ypos = mScreenHeight - (TextImage->Height + 5);
  }
  
  BltImageAlpha (TextImage, Xpos, Ypos, &mTransparentPixel, 16);
}

STATIC
EFI_IMAGE_OUTPUT *
DecodePNGFile (
  IN CHAR16                        *FilePath
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;
  VOID                             *Buffer;
  UINT32                           BufferSize;
  EFI_IMAGE_OUTPUT                 *NewImage;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Pixel;
  VOID                             *Data;
  UINT32                           Width;
  UINT32                           Height;
  UINT8                            *DataWalker;
  UINTN                            X;
  UINTN                            Y;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            Index;

  BufferSize = 0;
  HandleCount = 0;
  FileSystem = NULL;
  Buffer = NULL;
  
  if (mFileSystem == NULL) {
    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPartTypeSystemPartGuid, NULL, &HandleCount, &Handles);
    if (!EFI_ERROR (Status) && HandleCount > 0) {
      for (Index = 0; Index < HandleCount; ++Index) {
        Status = gBS->HandleProtocol (
                        Handles[Index],
                        &gEfiSimpleFileSystemProtocolGuid,
                        (VOID **) &FileSystem
                        );
        if (EFI_ERROR (Status)) {
          FileSystem = NULL;
          continue;
        }
        
        Buffer = ReadFile (FileSystem, FilePath, &BufferSize, BASE_16MB);
        if (Buffer != NULL) {
          mFileSystem = FileSystem;
          DEBUG ((DEBUG_INFO, "OCUI: FileSystem found!  Handle(%d) \n", Index));
          break;
        }
        FileSystem = NULL;
      }
      
      if (Handles != NULL) {
        FreePool (Handles);
      }
    }
    
  } else {
    Buffer = ReadFile (mFileSystem, FilePath, &BufferSize, BASE_16MB);
  }
  
  if (Buffer == NULL) {
    DEBUG ((DEBUG_ERROR, "OCUI: Failed to locate %s file\n", FilePath));
    return Buffer;
  }
  
  Status = DecodePng (
               Buffer,
               BufferSize,
               &Data,
               &Width,
               &Height,
               NULL
              );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCUI: DecodePNG...%r\n", Status));
    if (Buffer != NULL) {
      FreePool (Buffer);
    }
    return NULL;
  }
    
  NewImage = CreateImage ((INTN) Width, (INTN) Height);
  if (NewImage == NULL) {
    if (Buffer != NULL) {
      FreePool (Buffer);
    }
    return NULL;
  }
  
  Pixel = (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) NewImage->Image.Bitmap;
  DataWalker = (UINT8 *) Data;
  for (Y = 0; Y < NewImage->Height; Y++) {
    for (X = 0; X < NewImage->Width; X++) {
      Pixel->Red = *DataWalker++;
      Pixel->Green = *DataWalker++;
      Pixel->Blue = *DataWalker++;
      Pixel->Reserved = 255 - *DataWalker++;
      Pixel++;
    }
  }
  if (Buffer != NULL) {
    FreePool (Buffer);
  }
  
  if (Data != NULL) {
    FreePool (Data);
  }
  
  return NewImage;
}

VOID
TakeScreenShot (
  IN CHAR16              *FilePath
  )
{
  EFI_STATUS              Status;
  EFI_FILE_PROTOCOL       *Fs;
  EFI_TIME                Date;
  EFI_IMAGE_OUTPUT        *Image;
  EFI_UGA_PIXEL           *ImagePNG;
  VOID                    *Buffer;
  UINTN                   BufferSize;
  UINTN                   Index;
  UINTN                   ImageSize;
  CHAR16                  *Path;
  UINTN                   Size;
  
  Buffer     = NULL;
  BufferSize = 0;
  
  Status = gRT->GetTime (&Date, NULL);
  
  Size = StrSize (FilePath) + L_STR_SIZE (L"-0000-00-00-000000.png");
  Path = AllocatePool (Size);
  UnicodeSPrint (Path,
                 Size,
                 L"%s-%04u-%02u-%02u-%02u%02u%02u.png",
                 FilePath,
                 (UINT32) Date.Year,
                 (UINT32) Date.Month,
                 (UINT32) Date.Day,
                 (UINT32) Date.Hour,
                 (UINT32) Date.Minute,
                 (UINT32) Date.Second
  );
  
  Image = CreateImage (mScreenWidth, mScreenHeight);
  if (Image == NULL) {
    DEBUG ((DEBUG_INFO, "Failed to take screen shot!\n"));
    return;
  }
    
  TakeImage (Image, 0, 0, mScreenWidth, mScreenHeight);
  
  ImagePNG = (EFI_UGA_PIXEL *) Image->Image.Bitmap;
  ImageSize = Image->Width * Image->Height;
  
  // Convert BGR to RGBA with Alpha set to 0xFF
  for (Index = 0; Index < ImageSize; ++Index) {
      UINT8 Temp = ImagePNG[Index].Blue;
      ImagePNG[Index].Blue = ImagePNG[Index].Red;
      ImagePNG[Index].Red = Temp;
      ImagePNG[Index].Reserved = 0xFF;
  }

  // Encode raw RGB image to PNG format
  Status = EncodePng (ImagePNG,
                      (UINTN) Image->Width,
                      (UINTN) Image->Height,
                      8,
                      &Buffer,
                      &BufferSize
                      );
  FreeImage (Image);
  if (Buffer == NULL) {
    DEBUG ((DEBUG_INFO, "OCUI: Fail Encoding!\n"));
    return;
  }
  
  Status = mFileSystem->OpenVolume (mFileSystem, &Fs);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCUI: Locating Writeable file system - %r\n", Status));
    return;
  }
  
  Status = SetFileData (Fs, Path, Buffer, (UINT32) BufferSize);
  DEBUG ((DEBUG_INFO, "OCUI: Screenshot was taken - %r\n", Status));
  if (Buffer != NULL) {
    FreePool (Buffer);
  }
  if (Path != NULL) {
    FreePool (Path);
  }
}

STATIC
VOID
CreateIcon (
  IN CHAR16               *Name,
  IN OC_BOOT_ENTRY_TYPE   Type,
  IN UINTN                IconCount,
  IN BOOLEAN              IsDefault,
  IN BOOLEAN              Ext,
  IN BOOLEAN              Dmg
  )
{
  CHAR16                 *FilePath;
  EFI_IMAGE_OUTPUT       *Icon;
  EFI_IMAGE_OUTPUT       *ScaledImage;
  EFI_IMAGE_OUTPUT       *TmpImage;
  
  Icon = NULL;
  ScaledImage = NULL;
  TmpImage = NULL;
  
  switch (Type) {
    case OcBootWindows:
      if (StrStr (Name, L"10") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_win10.icns";
      } else {
        FilePath = L"EFI\\OC\\Icons\\os_win.icns";
      }
      break;
    case OcBootApple:
      if (StrStr (Name, L"Cata") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_cata.icns";
      } else if (StrStr (Name, L"Moja") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_moja.icns";
      } else {
        FilePath = L"EFI\\OC\\Icons\\os_mac.icns";
      }
      break;
    case OcBootAppleRecovery:
      FilePath = L"EFI\\OC\\Icons\\os_recovery.icns";
      break;
    case OcBootCustom:
      if (StrStr (Name, L"Free") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_freebsd.icns";
      } else if (StrStr (Name, L"Linux") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_linux.icns";
      } else if (StrStr (Name, L"Redhat") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_redhat.icns";
      } else if (StrStr (Name, L"Ubuntu") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_ubuntu.icns";
      } else if (StrStr (Name, L"Fedora") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_fedora.icns";
      } else if (StrStr (Name, L"Shell") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\tool_shell.icns";
      } else if (StrStr (Name, L"Win") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_win.icns";
      } else if (StrStr (Name, L"10") != NULL) {
        FilePath = L"EFI\\OC\\Icons\\os_win10.icns";
      } else {
        FilePath = L"EFI\\OC\\Icons\\os_custom.icns";
      }
      break;
    case OcBootSystem:
      FilePath = L"EFI\\OC\\Icons\\func_resetnvram.icns";
      break;
    case OcBootUnknown:
      FilePath = L"EFI\\OC\\Icons\\os_unknown.icns";
      break;
      
    default:
      FilePath = L"EFI\\OC\\Icons\\os_unknown.icns";
      break;
  }
  
  Icon = DecodePNGFile (FilePath);
  
  if (Icon != NULL) {
    TmpImage = CreateFilledImage (128, 128, TRUE, &mTransparentPixel);
    ComposeImage (Icon, TmpImage, 0, 0, FALSE, TRUE);
    FreeImage (TmpImage);
  }
  
  ScaledImage = CopyScaledImage (Icon, mUiScale);
  FreeImage (Icon);
  CreateMenuImage (ScaledImage, IconCount);
}

STATIC
VOID
SwitchIconSelection (
  IN UINTN               IconCount,
  IN UINTN               IconIndex,
  IN BOOLEAN             Selected
  )
{
  EFI_IMAGE_OUTPUT       *NewImage;
  EFI_IMAGE_OUTPUT       *Icon;
  BOOLEAN                IsTwoRow;
  INTN                   Xpos;
  INTN                   Ypos;
  UINT16                 Width;
  UINT16                 Height;
  UINTN                  IconsPerRow;
  
  /* Begin Calculating Xpos and Ypos of current selected icon on screen*/
  NewImage = NULL;
  Icon = NULL;
  IsTwoRow = FALSE;
  Xpos = 0;
  Ypos = 0;
  Width = mIconSpaceSize;
  Height = Width;
  IconsPerRow = 1;
  
  for (IconsPerRow = 1; IconsPerRow < IconCount; ++IconsPerRow) {
    Width = Width + mIconSpaceSize;
    if ((Width + (mIconSpaceSize * 2)) >= mScreenWidth) {
      break;
    }
  }
  
  if (IconsPerRow < IconCount) {
    IsTwoRow = TRUE;
    Height = mIconSpaceSize * 2;
    if (IconIndex <= IconsPerRow) {
      Xpos = (mScreenWidth - Width) / 2 + (mIconSpaceSize * IconIndex);
      Ypos = (mScreenHeight / 2) - mIconSpaceSize;
    } else {
      Xpos = (mScreenWidth - Width) / 2 + (mIconSpaceSize * (IconIndex - (IconsPerRow + 1)));
      Ypos = mScreenHeight / 2;
    }
  } else {
    Xpos = (mScreenWidth - Width) / 2 + (mIconSpaceSize * IconIndex);
    Ypos = (mScreenHeight / 2) - mIconSpaceSize;
  }
  /* Done Calculating Xpos and Ypos of current selected icon on screen*/
  
  Icon = CreateImage (mIconSpaceSize - (mIconPaddingSize * 2), mIconSpaceSize - (mIconPaddingSize * 2));
  if (Icon == NULL) {
    return;
  }
  
  RawCopy (Icon->Image.Bitmap,
           mMenuImage->Image.Bitmap + (IsTwoRow ? mIconSpaceSize + mIconPaddingSize : mIconPaddingSize) * mMenuImage->Width + ((Xpos + mIconPaddingSize) - ((mScreenWidth - Width) / 2)),
           Icon->Width,
           Icon->Height,
           Icon->Width,
           mMenuImage->Width
           );
  
  if (Selected && mSelectorUsed) {
    NewImage = CreateFilledImage (mIconSpaceSize, mIconSpaceSize, FALSE, mFontColorPixel);
    RawCopy (NewImage->Image.Bitmap + mIconPaddingSize * NewImage->Width + mIconPaddingSize,
             mBackgroundImage->Image.Bitmap + (Ypos + mIconPaddingSize) * mBackgroundImage->Width + (Xpos + mIconPaddingSize),
             mIconSpaceSize - (mIconPaddingSize * 2),
             mIconSpaceSize - (mIconPaddingSize * 2),
             mIconSpaceSize,
             mBackgroundImage->Width
             );
  } else {
    NewImage = CreateImage (mIconSpaceSize, mIconSpaceSize);
    
    RawCopy (NewImage->Image.Bitmap,
             mBackgroundImage->Image.Bitmap + Ypos * mBackgroundImage->Width + Xpos,
             mIconSpaceSize,
             mIconSpaceSize,
             mIconSpaceSize,
             mBackgroundImage->Width
             );
  }
  
  RawCopyAlpha (NewImage->Image.Bitmap + mIconPaddingSize * NewImage->Width + mIconPaddingSize,
                Icon->Image.Bitmap,
                Icon->Width,
                Icon->Height,
                NewImage->Width,
                Icon->Width,
                !Selected
                );
  
  FreeImage (Icon);
  BltImage (NewImage, Xpos, Ypos);
  FreeImage (NewImage);
}

STATIC
VOID
ClearScreen (
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Color
  )
{
  EFI_IMAGE_OUTPUT                  *Image;
  
  if (FileExist (L"EFI\\OC\\Icons\\Background.png")) {
    mBackgroundImage = DecodePNGFile (L"EFI\\OC\\Icons\\Background.png");
  }
  
  if (mBackgroundImage != NULL && (mBackgroundImage->Width != mScreenWidth || mBackgroundImage->Height != mScreenHeight)) {
    FreeImage(mBackgroundImage);
    mBackgroundImage = NULL;
  }
  
  if (mBackgroundImage == NULL) {
    if (FileExist (L"EFI\\OC\\Icons\\background_color.png")) {
      Image = DecodePNGFile (L"EFI\\OC\\Icons\\background_color.png");
      if (Image != NULL) {
        CopyMem (mBackgroundPixel, &Image->Image.Bitmap[0], sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
        mBackgroundPixel->Reserved = 0xff;
      } else {
        Image = DecodePNGFile (L"EFI\\OC\\Icons\\os_mac.icns");
        if (Image != NULL) {
          CopyMem (mBackgroundPixel, &Image->Image.Bitmap[0], sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
          mBackgroundPixel->Reserved = 0xff;
        }
      }
      FreeImage (Image);
    }
    mBackgroundImage = CreateFilledImage (mScreenWidth, mScreenHeight, FALSE, mBackgroundPixel);
  }
  
  if (mBackgroundImage != NULL) {
    BltImage (mBackgroundImage, 0, 0);
  }
  
  if (FileExist (L"EFI\\OC\\Icons\\font_color.png")) {
    Image = DecodePNGFile (L"EFI\\OC\\Icons\\font_color.png");
    if (Image != NULL) {
      CopyMem (mFontColorPixel, &Image->Image.Bitmap[0], sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      mFontColorPixel->Reserved = 0xff;
      FreeImage (Image);
    }
  }
  
  if (FileExist (L"EFI\\OC\\Icons\\font_color_alt.png")) {
    Image = DecodePNGFile (L"EFI\\OC\\Icons\\font_color_alt.png");
    if (Image != NULL) {
      CopyMem (mFontColorPixelAlt, &Image->Image.Bitmap[0], sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      mFontColorPixelAlt->Reserved = 0xff;
      FreeImage (Image);
    }
  }
  
  if (FileExist (L"EFI\\OC\\Icons\\No_selector.png")) {
    mSelectorUsed = FALSE;
  }
}

STATIC
VOID
ClearScreenArea (
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Color,
  IN INTN                           Xpos,
  IN INTN                           Ypos,
  IN INTN                           Width,
  IN INTN                           Height
  )
{
  EFI_IMAGE_OUTPUT                  *Image;
  EFI_IMAGE_OUTPUT                  *NewImage;
  
  Image = CreateFilledImage (Width, Height, (mBackgroundImage != NULL), Color);
  if (mBackgroundImage == NULL) {
    DrawImageArea (Image, 0, 0, 0, 0, Xpos, Ypos);
    FreeImage (Image);
    return;
  }
  
  NewImage = CreateImage (Width, Height);
  if (NewImage == NULL) {
    return;
  }
  RawCopy (NewImage->Image.Bitmap,
           mBackgroundImage->Image.Bitmap + Ypos * mBackgroundImage->Width + Xpos,
           Width,
           Height,
           Width,
           mBackgroundImage->Width
           );

  ComposeImage (NewImage, Image, 0, 0, FALSE, (mBackgroundImage != NULL));
  FreeImage (Image);

  DrawImageArea (NewImage, 0, 0, 0, 0, Xpos, Ypos);
  FreeImage (NewImage);
}

STATIC
VOID
InitScreen (
  VOID
  )
{
  EFI_STATUS        Status;
  EFI_HANDLE        Handle;
  UINT32            ColorDepth;
  UINT32            RefreshRate;
  UINT32            ScreenWidth;
  UINT32            ScreenHeight;
  
  Handle = NULL;
  mUgaDraw = NULL;
  //
  // Try to open GOP first
  //
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **) &mGraphicsOutput);
  if (EFI_ERROR (Status)) {
    mGraphicsOutput = NULL;
    //
    // Open GOP failed, try to open UGA
    //
    Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiUgaDrawProtocolGuid, (VOID **) &mUgaDraw);
    if (EFI_ERROR (Status)) {
      mUgaDraw = NULL;
    }
    
  }
  
  if (mGraphicsOutput != NULL) {
    Status = OcSetConsoleResolution (0, 0, 0);
    mScreenWidth = mGraphicsOutput->Mode->Info->HorizontalResolution;
    mScreenHeight = mGraphicsOutput->Mode->Info->VerticalResolution;
  } else {
    ASSERT (mUgaDraw != NULL);
    Status = mUgaDraw->GetMode (mUgaDraw, &ScreenWidth, &ScreenHeight, &ColorDepth, &RefreshRate);
    mScreenWidth = ScreenWidth;
    mScreenHeight = ScreenHeight;
  }
  DEBUG ((DEBUG_INFO, "OCUI: Initialize Graphic Screen...%r\n", Status));
  
  mTextScale = (mTextScale == 0 && mScreenHeight >= 2160) ? 28 : 16;
  if (mUiScale == 0 && mScreenHeight >= 2160) {
    mUiScale = 28;
    mIconPaddingSize = 5;
    mIconSpaceSize = 234;
  } else if (mUiScale == 0 && mScreenHeight <= 800) {
    mUiScale = 8;
    mIconPaddingSize = 3;
    mIconSpaceSize = 70;
  } else {
    mUiScale = 16;
    mIconPaddingSize = 4;
    mIconSpaceSize = 136;
  }
  
  Status = gBS->LocateProtocol (&gEfiHiiFontProtocolGuid, NULL, (VOID **) &mHiiFont);
  
  if (EFI_ERROR (Status)) {
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Handle,
                    &gEfiHiiFontProtocolGuid,
                    &mHiiFont,
                    NULL
                    );
    DEBUG ((DEBUG_INFO, "OCUI: No HiiFont found, installing...%r\n", Status));
    if (EFI_ERROR (Status)) {
      mHiiFont = NULL;
    }
  }
  
  DEBUG ((DEBUG_INFO, "OCUI: Initialize HiiFont...%r\n", Status));
  
  GetGlyph ();
}

STATIC
VOID
PrintDateTime (
  IN BOOLEAN         ShowAll
  )
{
  EFI_STATUS         Status;
  EFI_TIME           DateTime;
  CHAR16             DateStr[11];
  CHAR16             TimeStr[11];
  UINTN              Hour;
  CHAR16             *Str;
  
  Str = NULL;
  Hour = 0;
  Status = gRT->GetTime (&DateTime, NULL);
  
  if (!EFI_ERROR (Status) && ShowAll) {
    Hour = (UINTN) DateTime.Hour;
    Str = Hour >= 12 ? L"PM" : L"AM";
    if (Hour > 12) {
      Hour = Hour - 12;
    }
    UnicodeSPrint (DateStr, sizeof (DateStr), L"%02u/%02u/%04u", DateTime.Month, DateTime.Day, DateTime.Year);
    UnicodeSPrint (TimeStr, sizeof (TimeStr), L"%02u:%02u:%02u%s", Hour, DateTime.Minute, DateTime.Second, Str);
    PrintTextGraphicXY (DateStr, mScreenWidth - ((StrLen(DateStr) * mFontWidth) + 10), 5, mFontColorPixelAlt);
    PrintTextGraphicXY (TimeStr, mScreenWidth - ((StrLen(DateStr) * mFontWidth) + 10), (mTextScale == 16) ? (mFontHeight + 5 + 2) : ((mFontHeight * 2) + 5 + 2), mFontColorPixelAlt);
  } else {
    ClearScreenArea (&mTransparentPixel, 0, 0, mScreenWidth, mFontHeight * 5);
  }
}

STATIC
VOID
PrintOcVersion (
  IN CONST CHAR8         *String,
  IN BOOLEAN             ShowAll
  )
{
  CHAR16                 *NewString;
  
  if (String == NULL) {
    return;
  }
  
  NewString = AsciiStrCopyToUnicode (String, 0);
  if (String != NULL && ShowAll) {
    PrintTextGraphicXY (NewString, mScreenWidth - ((StrLen(NewString) * mFontWidth) + 10), mScreenHeight - (mFontHeight + 5), mFontColorPixelAlt);
  } else {
    ClearScreenArea (&mTransparentPixel,
                       mScreenWidth - ((StrLen(NewString) * mFontWidth) * 2),
                       mScreenHeight - mFontHeight * 2,
                       (StrLen(NewString) * mFontWidth) * 2,
                       mFontHeight * 2
                       );
  }
}

STATIC
BOOLEAN
PrintTimeOutMessage (
  IN UINTN           Timeout
  )
{
  EFI_IMAGE_OUTPUT     *TextImage;
  EFI_IMAGE_OUTPUT     *NewImage;
  CHAR16               String[52];
  
  TextImage = NULL;
  NewImage = NULL;
  
  if (Timeout > 0) {
    UnicodeSPrint (String, sizeof (String), L"%s %02u %s.", L"The default boot selection will start in", Timeout, L"seconds"); //52
    TextImage = CreatTextImage (mFontColorPixelAlt, &mTransparentPixel, String, TRUE);
    if (TextImage == NULL) {
      return !(Timeout > 0);
    }
    NewImage = CreateFilledImage (mScreenWidth, TextImage->Height, TRUE, &mTransparentPixel);
    if (NewImage == NULL) {
      FreeImage (TextImage);
      return !(Timeout > 0);
    }
    ComposeImage (NewImage, TextImage, (NewImage->Width - TextImage->Width) / 2, 0, TRUE, TRUE);
    if (TextImage != NULL) {
      FreeImage (TextImage);
    }
    BltImageAlpha (NewImage, (mScreenWidth - NewImage->Width) / 2, (mScreenHeight / 4) * 3, &mTransparentPixel, 16);
  } else {
    ClearScreenArea (&mTransparentPixel, 0, ((mScreenHeight / 4) * 3) - 4, mScreenWidth, mFontHeight * 2);
  }
  return !(Timeout > 0);
}

STATIC
VOID
PrintTextDesrciption (
  IN UINTN        MaxStrWidth,
  IN UINTN        Selected,
  IN CHAR16       *Name,
  IN BOOLEAN      Ext,
  IN BOOLEAN      Dmg
  )
{
  EFI_IMAGE_OUTPUT                   *TextImage;
  EFI_IMAGE_OUTPUT                   *NewImage;
  CHAR16                             Code[3];
  CHAR16                             String[MaxStrWidth + 1];
  
  Code[0] = 0x20;
  Code[1] = OC_INPUT_STR[Selected];
  Code[2] = '\0';
  
  UnicodeSPrint (String, sizeof (String), L" %s%s%s%s%s ",
                 Code,
                 (mAllowSetDefault && mDefaultEntry == Selected) ? L".*" : L". ",
                 Name,
                 Ext ? L" (ext)" : L"",
                 Dmg ? L" (dmg)" : L""
                 );
  
  TextImage = CreatTextImage (mFontColorPixel, &mTransparentPixel, String, TRUE);
  if (TextImage == NULL) {
    return;
  }
  NewImage = CreateFilledImage (mScreenWidth, TextImage->Height, TRUE, &mTransparentPixel);
  if (NewImage == NULL) {
    FreeImage (TextImage);
    return;
  }
  ComposeImage (NewImage, TextImage, (NewImage->Width - TextImage->Width) / 2, 0, TRUE, TRUE);
  if (TextImage != NULL) {
    FreeImage (TextImage);
  }
 
  BltImageAlpha (NewImage,
                 (mScreenWidth - NewImage->Width) / 2,
                 (mScreenHeight / 2) + mIconSpaceSize,
                 &mTransparentPixel,
                 16
                 );
}

STATIC
VOID
RestoreConsoleMode (
  IN OC_PICKER_CONTEXT    *Context
  )
{
  FreeImage (mBackgroundImage);
  FreeImage (mMenuImage);
  mMenuImage = NULL;
  ClearScreenArea (&mBlackPixel, 0, 0, mScreenWidth, mScreenHeight);
  mUiScale = 0;
  mTextScale = 0;
  if (Context->ConsoleAttributes != 0) {
    gST->ConOut->SetAttribute (gST->ConOut, Context->ConsoleAttributes & 0x7FU);
  }
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);
}

EFI_STATUS
UiMenuMain (
  IN OC_PICKER_CONTEXT            *Context,
  IN OC_BOOT_ENTRY                *BootEntries,
  IN UINTN                        Count,
  IN UINTN                        DefaultEntry,
  OUT OC_BOOT_ENTRY               **ChosenBootEntry
  )
{
  EFI_STATUS                         Status;
  UINTN                              Index;
  UINTN                              CustomEntryIndex;
  INTN                               KeyIndex;
  UINT32                             TimeOutSeconds;
  UINTN                              VisibleList[Count];
  UINTN                              VisibleIndex;
  BOOLEAN                            ShowAll;
  UINTN                              Selected;
  UINTN                              MaxStrWidth;
  UINTN                              StrWidth;
  APPLE_KEY_MAP_AGGREGATOR_PROTOCOL  *KeyMap;
  BOOLEAN                            SetDefault;
  BOOLEAN                            TimeoutExpired;
  OC_STORAGE_CONTEXT                 *Storage;
  
  Selected         = 0;
  VisibleIndex     = 0;
  MaxStrWidth      = 0;
  TimeoutExpired   = FALSE;
  ShowAll          = !mHideAuxiliary;
  TimeOutSeconds   = Context->TimeoutSeconds;
  mAllowSetDefault = Context->AllowSetDefault;
  Storage          = Context->CustomEntryContext;
  mDefaultEntry    = DefaultEntry;
  CustomEntryIndex = 0;
  
  if (Storage->FileSystem != NULL && mFileSystem == NULL) {
    mFileSystem = Storage->FileSystem;
    DEBUG ((DEBUG_INFO, "OCUI: FileSystem Found!\n"));
  }
  
  KeyMap = OcAppleKeyMapInstallProtocols (FALSE);
  if (KeyMap == NULL) {
    DEBUG ((DEBUG_ERROR, "OCUI: Missing AppleKeyMapAggregator\n"));
    return EFI_UNSUPPORTED;
  }
  
  for (Index = 0; Index < MIN (Count, OC_INPUT_MAX); ++Index) {
    StrWidth = UnicodeStringDisplayLength (BootEntries[Index].Name) + ((BootEntries[Index].IsFolder || BootEntries[Index].IsExternal) ? 11 : 5);
    MaxStrWidth = MaxStrWidth > StrWidth ? MaxStrWidth : StrWidth;
    if (BootEntries[Index].Type == OcBootCustom) {
      BootEntries[Index].IsAuxiliary = Context->CustomEntries[CustomEntryIndex].Auxiliary;
      ++CustomEntryIndex;
    }
  }
  
  InitScreen ();
  ClearScreen (&mTransparentPixel);
  
  while (TRUE) {
    if (!TimeoutExpired) {
      TimeoutExpired = PrintTimeOutMessage (TimeOutSeconds);
      TimeOutSeconds = TimeoutExpired ? 10000 : TimeOutSeconds;
    }
    PrintOcVersion (Context->TitleSuffix, ShowAll);
    PrintDateTime (ShowAll);
    for (Index = 0, VisibleIndex = 0; Index < MIN (Count, OC_INPUT_MAX); ++Index) {
      if ((BootEntries[Index].Type == OcBootAppleRecovery && !ShowAll)
          || (BootEntries[Index].Type == OcBootUnknown && !ShowAll)
          || (BootEntries[Index].DevicePath == NULL && !ShowAll)
          || (BootEntries[Index].IsAuxiliary && !ShowAll)) {
        continue;
      }
      if (DefaultEntry == Index) {
        Selected = VisibleIndex;
      }
      VisibleList[VisibleIndex] = Index;
      CreateIcon (BootEntries[Index].Name,
                  BootEntries[Index].Type,
                  VisibleIndex,
                  VisibleIndex,
                  BootEntries[Index].IsExternal,
                  BootEntries[Index].IsFolder
                  );
      ++VisibleIndex;
    }
    
    ClearScreenArea (&mTransparentPixel, 0, (mScreenHeight / 2) - mIconSpaceSize, mScreenWidth, mIconSpaceSize * 2);
    BltMenuImage (mMenuImage, (mScreenWidth - mMenuImage->Width) / 2, (mScreenHeight / 2) - mIconSpaceSize);
    SwitchIconSelection (VisibleIndex, Selected, TRUE);
    PrintTextDesrciption (MaxStrWidth,
                          Selected,
                          BootEntries[DefaultEntry].Name,
                          BootEntries[DefaultEntry].IsExternal,
                          BootEntries[DefaultEntry].IsFolder
                          );

    while (TRUE) {
      KeyIndex = OcWaitForAppleKeyIndex (Context, KeyMap, 1, Context->PollAppleHotKeys, &SetDefault);
      --TimeOutSeconds;
      if ((KeyIndex == OC_INPUT_TIMEOUT && TimeOutSeconds == 0) || KeyIndex == OC_INPUT_CONTINUE) {
        *ChosenBootEntry = &BootEntries[DefaultEntry];
        SetDefault = BootEntries[DefaultEntry].DevicePath != NULL
          && !BootEntries[DefaultEntry].IsAuxiliary
          && Context->AllowSetDefault
          && SetDefault;
        if (SetDefault) {
          Status = OcSetDefaultBootEntry (Context, &BootEntries[DefaultEntry]);
          DEBUG ((DEBUG_INFO, "OCUI: Setting default - %r\n", Status));
        }
        RestoreConsoleMode (Context);
        return EFI_SUCCESS;
      } else if (KeyIndex == OC_INPUT_ABORTED) {
        TimeOutSeconds = 0;
        break;
      } else if (KeyIndex == OC_INPUT_FUNCTIONAL(10)) {
        TimeOutSeconds = 0;
        TakeScreenShot (L"ScreenShot");
      } else if (KeyIndex == OC_INPUT_MORE) {
        ShowAll = !ShowAll;
        DefaultEntry = mDefaultEntry;
        TimeOutSeconds = 0;
        FreeImage (mMenuImage);
        mMenuImage = NULL;
        break;
      } else if (KeyIndex == OC_INPUT_UP || KeyIndex == OC_INPUT_LEFT) {
        SwitchIconSelection (VisibleIndex, Selected, FALSE);
        DefaultEntry = Selected > 0 ? VisibleList[Selected - 1] : VisibleList[VisibleIndex - 1];
        Selected = Selected > 0 ? --Selected : VisibleIndex - 1;
        SwitchIconSelection (VisibleIndex, Selected, TRUE);
        PrintTextDesrciption (MaxStrWidth,
                              Selected,
                              BootEntries[DefaultEntry].Name,
                              BootEntries[DefaultEntry].IsExternal,
                              BootEntries[DefaultEntry].IsFolder
                              );
        TimeOutSeconds = 0;
      } else if (KeyIndex == OC_INPUT_DOWN || KeyIndex == OC_INPUT_RIGHT) {
        SwitchIconSelection (VisibleIndex, Selected, FALSE);
        DefaultEntry = Selected < (VisibleIndex - 1) ? VisibleList[Selected + 1] : 0;
        Selected = Selected < (VisibleIndex - 1) ? ++Selected : 0;
        SwitchIconSelection (VisibleIndex, Selected, TRUE);
        PrintTextDesrciption (MaxStrWidth,
                              Selected,
                              BootEntries[DefaultEntry].Name,
                              BootEntries[DefaultEntry].IsExternal,
                              BootEntries[DefaultEntry].IsFolder
                              );
        TimeOutSeconds = 0;
      } else if (KeyIndex != OC_INPUT_INVALID && (UINTN)KeyIndex < VisibleIndex) {
        ASSERT (KeyIndex >= 0);
        *ChosenBootEntry = &BootEntries[VisibleList[KeyIndex]];
        SetDefault = BootEntries[VisibleList[KeyIndex]].DevicePath != NULL
          && !BootEntries[VisibleList[KeyIndex]].IsAuxiliary
          && Context->AllowSetDefault
          && SetDefault;
        if (SetDefault) {
          Status = OcSetDefaultBootEntry (Context, &BootEntries[VisibleList[KeyIndex]]);
          DEBUG ((DEBUG_INFO, "OCUI: Setting default - %r\n", Status));
        }
        RestoreConsoleMode (Context);
        return EFI_SUCCESS;
      } else if (KeyIndex != OC_INPUT_TIMEOUT) {
        TimeOutSeconds = 0;
      }

      if (!TimeoutExpired) {
        PrintDateTime (ShowAll);
        TimeoutExpired = PrintTimeOutMessage (TimeOutSeconds);
        TimeOutSeconds = TimeoutExpired ? 10000 : TimeOutSeconds;
      } else {
        PrintDateTime (ShowAll);
      }
    }
  }

  ASSERT (FALSE);
}

EFI_STATUS
SystemActionResetNvram (
  VOID
  )
{
  OcDeleteVariables ();
  DirectRestCold ();
  return EFI_DEVICE_ERROR;
}

EFI_STATUS
RunBootPicker (
  IN OC_PICKER_CONTEXT  *Context
  )
{
  EFI_STATUS                         Status;
  APPLE_BOOT_POLICY_PROTOCOL         *AppleBootPolicy;
  APPLE_KEY_MAP_AGGREGATOR_PROTOCOL  *KeyMap;
  OC_BOOT_ENTRY                      *Chosen;
  OC_BOOT_ENTRY                      *Entries;
  UINTN                              EntryCount;
  INTN                               DefaultEntry;
  BOOLEAN                            ForbidApple;
  
  mHideAuxiliary = Context->HideAuxiliary;
  
  AppleBootPolicy = OcAppleBootPolicyInstallProtocol (FALSE);
  if (AppleBootPolicy == NULL) {
    DEBUG ((DEBUG_ERROR, "OCUI: AppleBootPolicy locate failure\n"));
    return EFI_NOT_FOUND;
  }
  
  KeyMap = OcAppleKeyMapInstallProtocols (FALSE);
  if (KeyMap == NULL) {
    DEBUG ((DEBUG_ERROR, "OCUI: AppleKeyMap locate failure\n"));
    return EFI_NOT_FOUND;
  }

  //
  // This one is handled as is for Apple BootPicker for now.
  //
  if (Context->PickerCommand != OcPickerDefault) {
    Status = Context->RequestPrivilege (
                        Context->PrivilegeContext,
                        OcPrivilegeAuthorized
                        );
    if (EFI_ERROR (Status)) {
      if (Status != EFI_ABORTED) {
        ASSERT (FALSE);
        return Status;
      }

      Context->PickerCommand = OcPickerDefault;
    }
  }

  if (Context->PickerCommand == OcPickerShowPicker && Context->PickerMode == OcPickerModeApple) {
    Status = OcRunAppleBootPicker ();
    DEBUG ((DEBUG_INFO, "OCUI: Apple BootPicker failed - %r, fallback to builtin\n", Status));
    ForbidApple = TRUE;
  } else {
    ForbidApple = FALSE;
  }

  while (TRUE) {
    DEBUG ((DEBUG_INFO, "OCUI: Performing OcScanForBootEntries...\n"));
    Context->HideAuxiliary = FALSE;
    Status = OcScanForBootEntries (
      AppleBootPolicy,
      Context,
      &Entries,
      &EntryCount,
      NULL,
      TRUE
      );

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "OCUI: OcScanForBootEntries failure - %r\n", Status));
      return Status;
    }

    if (EntryCount == 0) {
      DEBUG ((DEBUG_WARN, "OCUI: OcScanForBootEntries has no entries\n"));
      return EFI_NOT_FOUND;
    }

    DEBUG ((
      DEBUG_INFO,
      "OCB: Performing OcShowSimpleBootMenu... %d - %d entries found\n",
      Context->PollAppleHotKeys,
      EntryCount
      ));
    
    DefaultEntry = OcGetDefaultBootEntry (Context, Entries, EntryCount);

    if (Context->PickerCommand == OcPickerShowPicker) {
      if (!ForbidApple && Context->PickerMode == OcPickerModeApple) {
        Status = OcRunAppleBootPicker ();
        DEBUG ((DEBUG_INFO, "OCUI: Apple BootPicker failed on error - %r, fallback to builtin\n", Status));
        ForbidApple = TRUE;
      }

      Status = UiMenuMain (
        Context,
        Entries,
        EntryCount,
        DefaultEntry,
        &Chosen
        );
      
    } else if (Context->PickerCommand == OcPickerResetNvram) {
      return SystemActionResetNvram ();
    } else {
      Chosen = &Entries[DefaultEntry];
      Status = EFI_SUCCESS;
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "OCUI: OcShowSimpleBootMenu failed - %r\n", Status));
      OcFreeBootEntries (Entries, EntryCount);
      return Status;
    }

    Context->TimeoutSeconds = 0;

    if (!EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_INFO,
        "OCUI: Select to boot from %s (T:%d|F:%d)\n",
        Chosen->Name,
        Chosen->Type,
        Chosen->IsFolder
        ));
    }

    if (!EFI_ERROR (Status)) {
      Status = OcLoadBootEntry (
        AppleBootPolicy,
        Context,
        Chosen,
        gImageHandle
        );
      
      //
      // Do not wait on successful return code.
      //
      if (EFI_ERROR (Status)) {
        gBS->Stall (SECONDS_TO_MICROSECONDS (3));
        //
        // Show picker on first failure.
        //
        Context->PickerCommand = OcPickerShowPicker;
      }
      //
      // Ensure that we flush all pressed keys after the application.
      // This resolves the problem of application-pressed keys being used to control the menu.
      //
      OcKeyMapFlush (KeyMap, 0, TRUE);
    }
    
    if (Entries != NULL) {
      OcFreeBootEntries (Entries, EntryCount);
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
ExternalGuiRun (
  IN OC_INTERFACE_PROTOCOL  *This,
  IN OC_STORAGE_CONTEXT     *Storage,
  IN OC_PICKER_CONTEXT      *Picker
  )
{
  return RunBootPicker (Picker);
}

STATIC
OC_INTERFACE_PROTOCOL
mOcInterfaceProtocol = {
  OC_INTERFACE_REVISION,
  ExternalGuiRun
};

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  VOID        *PrevInterface;
  EFI_HANDLE  NewHandle;

  //
  // Check for previous GUI protocols.
  //
  Status = gBS->LocateProtocol (
    &gOcInterfaceProtocolGuid,
    NULL,
    &PrevInterface
    );

  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "OCE: Another GUI is already present\n"));
    return EFI_ALREADY_STARTED;
  }

  //
  // Install new GUI protocol
  //
  NewHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
    &NewHandle,
    &gOcInterfaceProtocolGuid,
    &mOcInterfaceProtocol,
    NULL
    );

  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OCE: Registered custom GUI protocol\n"));
  } else {
    DEBUG ((DEBUG_ERROR, "OCE: Failed to install GUI protocol - %r\n", Status));
  }

  return Status;
}
