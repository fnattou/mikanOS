#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  <Library/UefiBootServicesTableLib.h>
#include  <Library/PrintLib.h>
#include  <Library/MemoryAllocationLib.h>
#include  <Library/BaseMemoryLib.h>
#include  <Protocol/LoadedImage.h>
#include  <Protocol/SimpleFileSystem.h>
#include  <Protocol/DiskIo2.h>
#include  <Protocol/BlockIo.h>
#include  <Guid/FileInfo.h>
#include  "frame_buffer_config.hpp"
#include  "elf.hpp"

struct MemoryMap {
    UINTN buffer_size;
    VOID* buffer;
    UINTN map_size;
    UINTN map_key;
    UINTN descriptor_size;
    UINT32 descroptor_version;
};

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
    if (map->buffer == NULL) {
        return EFI_BUFFER_TOO_SMALL;
    }

    map->map_size = map->buffer_size;
    return gBS->GetMemoryMap(
        &map->map_size,
        (EFI_MEMORY_DESCRIPTOR*)map->buffer,
        &map->map_key,
        &map->descriptor_size,
        &map->descroptor_version);
}

const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
  switch (type) {
    case EfiReservedMemoryType: return L"EfiReservedMemoryType";
    case EfiLoaderCode: return L"EfiLoaderCode";
    case EfiLoaderData: return L"EfiLoaderData";
    case EfiBootServicesCode: return L"EfiBootServicesCode";
    case EfiBootServicesData: return L"EfiBootServicesData";
    case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
    case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
    case EfiConventionalMemory: return L"EfiConventionalMemory";
    case EfiUnusableMemory: return L"EfiUnusableMemory";
    case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
    case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
    case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
    case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
    case EfiPalCode: return L"EfiPalCode";
    case EfiPersistentMemory: return L"EfiPersistentMemory";
    case EfiMaxMemoryType: return L"EfiMaxMemoryType";
    default: return L"InvalidMemoryType";
  }
}

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

  gBS->OpenProtocol(
      image_handle,
      &gEfiLoadedImageProtocolGuid,
      (VOID**)&loaded_image,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  gBS->OpenProtocol(
      loaded_image->DeviceHandle,
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&fs,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  fs->OpenVolume(fs, root);

  return EFI_SUCCESS;
}

EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
    CHAR8 buf[256];
    UINTN len;

    CHAR8* header = 
      "Index, type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
    len = AsciiStrLen(header);
    file->Write(file, &len, header);

    Print(L"map->buffer = %08lx, map->map_size = %08lx\n",
    map->buffer, map->map_size);

    EFI_PHYSICAL_ADDRESS iter;
    int i;
    for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
         iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
         iter += map->descriptor_size, i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
        len = AsciiSPrint(
          buf, sizeof(buf),
          "%u, %x, %-ls, %08lx, %lx, %lx\n",
          i, desc->Type, GetMemoryTypeUnicode(desc->Type),
          desc->PhysicalStart, desc->NumberOfPages,
          desc->Attribute & 0xffffflu
        );
        file->Write(file, &len, buf);
    }

    return EFI_SUCCESS;
}

EFI_STATUS OpenGOP(EFI_HANDLE image_handle,
                   EFI_GRAPHICS_OUTPUT_PROTOCOL** gop) {
  UINTN num_gop_handles = 0;
  EFI_HANDLE* gop_handles = NULL;
  gBS->LocateHandleBuffer(
      ByProtocol,
      &gEfiGraphicsOutputProtocolGuid,
      NULL,
      &num_gop_handles,
      &gop_handles);

  gBS->OpenProtocol(
      gop_handles[0],
      &gEfiGraphicsOutputProtocolGuid,
      (VOID**)gop,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  FreePool(gop_handles);

  return EFI_SUCCESS;
}

const CHAR16* GetPixelFormatUnicode(EFI_GRAPHICS_PIXEL_FORMAT fmt) {
  switch (fmt) {
    case PixelRedGreenBlueReserved8BitPerColor:
      return L"PixelRedGreenBlueReserved8BitPerColor";
    case PixelBlueGreenRedReserved8BitPerColor:
      return L"PixelBlueGreenRedReserved8BitPerColor";
    case PixelBitMask:
      return L"PixelBitMask";
    case PixelBltOnly:
      return L"PixelBltOnly";
    case PixelFormatMax:
      return L"PixelFormatMax";
    default:
      return L"InvalidPixelFormat";
  }
}

void Halt() {
    while(1) __asm__("hlt");
}

void PrintAndHaltIfError(EFI_STATUS st, CHAR16* funcName) {
  if (EFI_ERROR(st)) {
    Print(L"failed to %S: %r\n", funcName, st);
    Halt();
  }
}

void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
  Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
  *first = MAX_UINT64;
  *last = 0;
  for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD) continue;
    *first = MIN(*first, phdr[i].p_vaddr);
    *last =MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
  }
}

void CopyLoadSegments(Elf64_Ehdr* ehdr) {
  Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
  for (Elf64_Half i = 0; i< ehdr->e_phnum; ++i) {
    if (phdr[i].p_type != PT_LOAD) continue;

    UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
    CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

    UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
    SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
  }
}


EFI_STATUS EFIAPI UefiMain(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE* system_table) {
  EFI_STATUS status;
  Print(L"Hello, Mikan World!\n");

  CHAR8 memmap_buf[4096 * 4];
  struct MemoryMap memmap = {sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0};
  PrintAndHaltIfError(GetMemoryMap(&memmap), L"get memory map");

  EFI_FILE_PROTOCOL* root_dir;
  PrintAndHaltIfError(
    OpenRootDir(image_handle, &root_dir), L"open root directory"
  );

  EFI_FILE_PROTOCOL* memmap_file;
  status = root_dir->Open(
    root_dir, &memmap_file, L"\\memmap",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0
  );
  if (EFI_ERROR(status)) {
    Print(L"failed to open file '\\memmap': %r\n", status);
    Print(L"Ignored.\n");
  }
  else {
    PrintAndHaltIfError(
      SaveMemoryMap(&memmap, memmap_file), L"save memory map"
    );
    PrintAndHaltIfError(
      memmap_file->Close(memmap_file), L"failed to close memory map"
    );
  }


  //-------------------------------------
  // draw pixel with gop
  //-------------------------------------
  EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
  PrintAndHaltIfError(
    OpenGOP(image_handle, &gop), L"open GOP"
  );

  {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* m = gop->Mode;
    Print(L"Resolution: %ux%u, Pixel Format: %s, %u pixels/line\n",
      m->Info->HorizontalResolution,
      m->Info->VerticalResolution, 
      GetPixelFormatUnicode(m->Info->PixelFormat),
      m->Info->PixelsPerScanLine
    );
    Print(L"Frame Buffer: 0x%0lx - 0x%0lx, Size: %lu bytes\n",
      m->FrameBufferBase,
      m->FrameBufferBase + m->FrameBufferSize,
      m->FrameBufferSize
    );
    UINT8* frame_buffer = (UINT8*)m->FrameBufferBase;
    for (UINTN i = 0; i < m->FrameBufferSize; ++i) {
      frame_buffer[i] = 255;
    }
  }

  //-------------------------------------
  //read kernel
  //-------------------------------------
  EFI_FILE_PROTOCOL* kernel_file;
  status = root_dir->Open(
    root_dir, &kernel_file, L"\\kernel.elf",
    EFI_FILE_MODE_READ, 0
  );
  PrintAndHaltIfError(status, L"open file '\\kernel.elf'");

  UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
  UINT8 file_info_buffer[file_info_size];
  status = kernel_file->GetInfo(
      kernel_file, &gEfiFileInfoGuid, 
      &file_info_size, file_info_buffer
  );
  PrintAndHaltIfError(status, L"get file infomation");
  EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
  UINTN kernel_file_size = file_info->FileSize;

  VOID* kernel_buffer;
  PrintAndHaltIfError(
    gBS->AllocatePool(EfiLoaderData, kernel_file_size, &kernel_buffer),
    L"allocate pool"
  );
  PrintAndHaltIfError(
    kernel_file->Read(kernel_file, &kernel_file_size, kernel_buffer),
    L"read kernel"
  );

  //-------------------------------------
  // Alloc Pages
  //-------------------------------------
  Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
  UINT64 kernel_first_addr, kernel_last_addr;
  CalcLoadAddressRange(kernel_ehdr, &kernel_first_addr, &kernel_last_addr);

  UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;

  status = gBS->AllocatePages(
    AllocateAddress, EfiLoaderData,
    num_pages, &kernel_first_addr
  );
  PrintAndHaltIfError(status, L"allocate pages");
  
  //-------------------------------------
  // Copy segments
  //-------------------------------------
  CopyLoadSegments(kernel_ehdr);
  Print(L"Kernel: 0x%0lx (%lu bytes)\n", kernel_first_addr, kernel_last_addr);

  //-------------------------------------
  // Exit BootServices
  //-------------------------------------
  status = gBS->ExitBootServices(image_handle, memmap.map_key);
  if (EFI_ERROR(status)) {
    status = GetMemoryMap(&memmap);
    if (EFI_ERROR(status)) {
      Print(L"failed to get memory map: %r\n", status);
      while(1);
    }
    status = gBS->ExitBootServices(image_handle, memmap.map_key);
    if (EFI_ERROR(status)) {
      Print(L"Could not exit boot service: %r\n", status);
      while(1);
    }
  }

  //-------------------------------------
  // Call Kernel
  //-------------------------------------
  {
    UINT64 entry_addr = *(UINT64*)(kernel_first_addr + 24);
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = gop->Mode->Info;
    struct FrameBufferConfig config =  {
      (UINT8*)gop->Mode->FrameBufferBase,
      info->PixelsPerScanLine,
      info->HorizontalResolution,
      info->VerticalResolution,
      0
    };
    switch (info->PixelFormat) {
      case PixelRedGreenBlueReserved8BitPerColor:
        config.pixel_format = kPixelRGBResv8BitPerColor;
        break;
      case PixelBlueGreenRedReserved8BitPerColor:
        config.pixel_format = kPixelBGRResv8BitPerColor;
        break;
      default:
        Print(L"Unimplemented pixel format: %d\n", info->PixelFormat);
        Halt();
    }
    typedef void EntryPointType(const struct FrameBufferConfig*);
    EntryPointType* entry_point = (EntryPointType*)entry_addr;
    entry_point(&config);
  }
  Print(L"All done\n");
  
  while(1);
  return EFI_SUCCESS;
}