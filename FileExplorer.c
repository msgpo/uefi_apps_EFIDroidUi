#include "EFIDroidUi.h"
#include <Guid/FileSystemVolumeLabelInfo.h>
#include <Library/ShellCommandLib.h>

typedef struct {
  MENU_OPTION     *ParentMenu;
  EFI_HANDLE      Handle;
  CHAR16          *ShellFilePath;
  EFI_FILE_HANDLE Root;
  UINT16          *FileName;
  BOOLEAN         IsDir;
  CHAR16          *FileExtension;
} MENU_ITEM_CONTEXT;

STATIC
EFI_STATUS
FindFiles (
  IN MENU_OPTION               *Menu,
  IN EFI_FILE_HANDLE           FileHandle,
  IN UINT16                    *FileName,
  IN EFI_HANDLE                DeviceHandle,
  IN CHAR16                    *ShellFilePath
  );

STATIC
EFI_STATUS
FileExplorerBackCallback (
  MENU_OPTION* This
)
{
  MENU_OPTION *OldMenu = This->Private;

  SetActiveMenu(OldMenu);
  MenuFree(This);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MenuItemCallback (
  IN MENU_ENTRY* This
)
{
  MENU_ITEM_CONTEXT* ItemContext = This->Private;
  MENU_OPTION *OldMenu = GetActiveMenu();
  EFI_STATUS Status;

  if(ItemContext->IsDir) {
    MENU_OPTION *Menu = MenuCreate();
    Menu->Private = OldMenu;
    Menu->BackCallback = FileExplorerBackCallback;

    EFI_FILE_HANDLE File = NULL;
    Status = ItemContext->Root->Open (
                     ItemContext->Root,
                     &File,
                     ItemContext->FileName,
                     EFI_FILE_MODE_READ,
                     0
                     );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    CHAR16* FileName = NULL;
    Status = FileHandleGetFileName(File, &FileName);
    if (EFI_ERROR (Status)) {
      Menu->Title = Unicode2Ascii(ItemContext->FileName);
    }
    else {
      Menu->Title = Unicode2Ascii(FileName);
      FreePool(FileName);
    }

    FindFiles(Menu, File, ItemContext->FileName, ItemContext->Handle, ItemContext->ShellFilePath);
    SetActiveMenu(Menu);
  }

  else if (ItemContext->FileExtension) {
    if(!StrCmp(ItemContext->FileExtension, L"efi")) {
      // open file
      EFI_FILE_HANDLE File = NULL;
      Status = ItemContext->Root->Open (
                       ItemContext->Root,
                       &File,
                       ItemContext->FileName,
                       EFI_FILE_MODE_READ,
                       0
                       );
      if (EFI_ERROR (Status)) {
        CHAR8 Buf[100];
        AsciiSPrint(Buf, 100, "Can't open file: %r", Status);
        MenuShowMessage("Error", Buf);

        return Status;
      }

      // get filename
      CHAR16* FileName = NULL;
      Status = FileHandleGetFileName(File, &FileName);
      if (EFI_ERROR (Status)) {
        UINTN Size = StrSize(ItemContext->FileName)+1*sizeof(CHAR16);
        FileName = AllocateZeroPool(Size);
        if(FileName==NULL) {
          MenuShowMessage("Error", "Can't allocate filename");
          return EFI_OUT_OF_RESOURCES;
        }

        // this is a workaround for the FV filesystem
        UnicodeSPrint(FileName, Size, L"\\%s", ItemContext->FileName);
      }

      // build absolute execution path
      UINTN PathLen = (StrLen(ItemContext->ShellFilePath)+StrLen(FileName)+1)*sizeof(CHAR16);
      CHAR16* Path = AllocateZeroPool(PathLen);
      if(Path==NULL) {
        MenuShowMessage("Error", "Out of memory");
        FreePool(FileName);
        return EFI_OUT_OF_RESOURCES;
      }
      UnicodeSPrint(Path, PathLen, L"%s%s", ItemContext->ShellFilePath, FileName);
      FreePool(FileName);

      // shut down menu
      MenuPreBoot();

      // start efi application
      EFI_STATUS CommandStatus;
      Status = ShellExecute (gImageHandle, Path, FALSE, NULL, &CommandStatus);

      // restart menu
      MenuPostBoot();

      // free path memory
      FreePool(Path);

      // show loader error
      if (EFI_ERROR(Status)) {
        CHAR8 Buf[100];
        AsciiSPrint(Buf, 100, "Error loading: %r", Status);
        MenuShowMessage("Error", Buf);
      }

      // show application error
      else if (EFI_ERROR(CommandStatus)) {
        CHAR8 Buf[100];
        AsciiSPrint(Buf, 100, "Application returned: %r", CommandStatus);
        MenuShowMessage("Error", Buf);
      }
    }
  }

  return EFI_SUCCESS;
}

VOID
MenuItemFreeCallback (
  MENU_ENTRY* This
)
{
  MENU_ITEM_CONTEXT* ItemContext = This->Private;

  if(This==NULL || This->Private==NULL)
    return;

  if(ItemContext->FileName)
    FreePool(ItemContext->FileName);
  if(ItemContext->FileExtension)
    FreePool(ItemContext->FileExtension);

  FreePool(ItemContext);
}

STATIC
EFI_STATUS
FindFiles (
  IN MENU_OPTION               *Menu,
  IN EFI_FILE_HANDLE           FileHandle,
  IN UINT16                    *FileName,
  IN EFI_HANDLE                DeviceHandle,
  IN CHAR16                    *ShellFilePath
  )
{
  EFI_FILE_INFO   *DirInfo;
  UINTN           BufferSize;
  UINTN           DirBufferSize;
  MENU_ENTRY      *Entry;
  MENU_ITEM_CONTEXT    *ItemContext;
  UINTN           Pass;
  EFI_STATUS      Status;

  DirBufferSize = sizeof (EFI_FILE_INFO) + 1024;
  DirInfo       = AllocateZeroPool (DirBufferSize);
  if (DirInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Get all files in current directory
  // Pass 1 to get Directories
  // Pass 2 to get files that are EFI images
  //
  for (Pass = 1; Pass <= 2; Pass++) {
    FileHandle->SetPosition (FileHandle, 0);
    for (;;) {
      BufferSize  = DirBufferSize;
      Status      = FileHandle->Read (FileHandle, &BufferSize, DirInfo);
      if (EFI_ERROR (Status) || BufferSize == 0) {
        break;
      }

      if (((DirInfo->Attribute & EFI_FILE_DIRECTORY) != 0 && Pass == 2) ||
          ((DirInfo->Attribute & EFI_FILE_DIRECTORY) == 0 && Pass == 1)
          ) {
        //
        // Pass 1 is for Directories
        // Pass 2 is for file names
        //
        continue;
      }

      if (!StrCmp(DirInfo->FileName, L".") || !StrCmp(DirInfo->FileName, L"..")) {
        continue;
      }

      ItemContext = AllocateZeroPool(sizeof(MENU_ITEM_CONTEXT));
      if (ItemContext == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      ItemContext->ParentMenu = Menu;
      ItemContext->Handle = DeviceHandle;
      ItemContext->Root = FileHandle;
      ItemContext->IsDir = (BOOLEAN) ((DirInfo->Attribute & EFI_FILE_DIRECTORY) == EFI_FILE_DIRECTORY);
      ItemContext->ShellFilePath = ShellFilePath;

      ItemContext->FileName = UnicodeStrDup(DirInfo->FileName);
      if (ItemContext->FileName == NULL) {
        FreePool(ItemContext);
        return EFI_OUT_OF_RESOURCES;
      }

      Entry = MenuCreateEntry();
      Entry->Name = Unicode2Ascii(DirInfo->FileName);
      Entry->Callback = MenuItemCallback;
      Entry->FreeCallback = MenuItemFreeCallback;
      Entry->Private = ItemContext;
      Entry->HideBootMessage = TRUE;
      if (ItemContext->IsDir)
        Entry->Icon = libaroma_stream_ramdisk("icons/ic_fso_folder.png");
      else {
        CHAR16* Ext = UtilGetExtensionLower(DirInfo->FileName);
        if(Ext) {
          if(StrStr (L"efi", Ext))
            Entry->Icon = libaroma_stream_ramdisk("icons/uefi.png");

          ItemContext->FileExtension = Ext;
        }

        if(!Entry->Icon)
          Entry->Icon = libaroma_stream_ramdisk("icons/ic_fso_default.png");
      }
      MenuAddEntry(Menu, Entry);
    }
  }

  FreePool (DirInfo);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
AddSFS (
  IN EFI_HANDLE  Handle,
  IN VOID        *Instance,
  IN VOID        *Context
  )
{
  MENU_OPTION     *Menu = Context;
  EFI_STATUS      Status;
  EFI_FILE_HANDLE Root;
  MENU_ENTRY      *Entry;
  EFI_PARTITION_NAME_PROTOCOL  *PartitionName;
  EFI_FILE_SYSTEM_VOLUME_LABEL *Info;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;
  CHAR16                       *ShellFilePath;

  Status = EFI_SUCCESS;

  DevicePath = DevicePathFromHandle(Handle);
  if (DevicePath==NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto DONE;
  }

  ShellFilePath = gEfiShellProtocol->GetFilePathFromDevicePath(DevicePath);
  if (ShellFilePath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto DONE;
  }

  Root = UtilOpenRoot(Handle);
  if (Root == NULL) {
    goto DONE;
  }

  MENU_ITEM_CONTEXT* ItemContext = AllocateZeroPool(sizeof(MENU_ITEM_CONTEXT));
  if (ItemContext == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto DONE;
  }
  ItemContext->ParentMenu = Menu;
  ItemContext->Handle = Handle;
  ItemContext->Root = Root;
  ItemContext->FileName = UnicodeStrDup(L"\\");
  ItemContext->IsDir = TRUE;
  ItemContext->ShellFilePath = ShellFilePath;

  Entry = MenuCreateEntry();
  Entry->Callback = MenuItemCallback;
  Entry->FreeCallback = MenuItemFreeCallback;
  Entry->Private = ItemContext;
  Entry->HideBootMessage = TRUE;
  Entry->Icon = libaroma_stream_ramdisk("icons/storage.png");
  MenuAddEntry(Menu, Entry);

  PartitionName = NULL;
  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiPartitionNameProtocolGuid,
                  (VOID **)&PartitionName
                  );
  if (!EFI_ERROR (Status)) {
    Entry->Name = Unicode2Ascii(PartitionName->Name);
  }

  else {
    Info = (EFI_FILE_SYSTEM_VOLUME_LABEL *) UtilFileInfo (Root, &gEfiFileSystemVolumeLabelInfoIdGuid);
    if (Info == NULL) {
      Entry->Name = AsciiStrDup("NO FILE SYSTEM INFO");
    } else {
      if (Info->VolumeLabel == NULL) {
        Entry->Name = AsciiStrDup("NULL VOLUME LABEL");
      } else {
        Entry->Name = AsciiStrDup("NO FILE SYSTEM INFO");
        if (Info->VolumeLabel[0] == 0x0000) {
          Entry->Name = AsciiStrDup("NO VOLUME LABEL");
        }
        else {
          Entry->Name = Unicode2Ascii(Info->VolumeLabel);
        }
      }
    }
  }


DONE:
  return Status;
}

EFI_STATUS
FileExplorerCallback (
  IN MENU_ENTRY* This
)
{
  MENU_OPTION *FileSystemMenu;
  MENU_OPTION *OldMenu = GetActiveMenu();

  FileSystemMenu = MenuCreate();
  FileSystemMenu->Private = OldMenu;
  FileSystemMenu->BackCallback = FileExplorerBackCallback;
  FileSystemMenu->Title = AsciiStrDup("Select Filesystem");

  // find filesystems
  VisitAllInstancesOfProtocol (
    &gEfiSimpleFileSystemProtocolGuid,
    AddSFS,
    FileSystemMenu
    );

  SetActiveMenu(FileSystemMenu);
  return EFI_SUCCESS;
}
