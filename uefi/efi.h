/*
 * Self-contained UEFI protocol definitions for SakuruBoot.
 * Based on UEFI Specification 2.10.
 * No external dependency (no gnu-efi / EDK2 required).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Calling convention                                                  */
/* ------------------------------------------------------------------ */
#if defined(__x86_64__)
#  define EFIAPI __attribute__((ms_abi))
#elif defined(__aarch64__)
#  define EFIAPI   /* AAPCS64 is the default on arm64 */
#else
#  define EFIAPI
#endif

/* ------------------------------------------------------------------ */
/* Primitive types                                                     */
/* ------------------------------------------------------------------ */
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int64_t   INT64;
typedef uintptr_t UINTN;
typedef intptr_t  INTN;
typedef uint16_t  CHAR16;   /* UCS-2 */
typedef void     *EFI_HANDLE;
typedef void     *EFI_EVENT;
typedef uint64_t  EFI_LBA;
typedef uint64_t  EFI_PHYSICAL_ADDRESS;
typedef uint64_t  EFI_VIRTUAL_ADDRESS;
typedef UINTN     EFI_STATUS;
typedef uint8_t   BOOLEAN;

#define TRUE  1
#define FALSE 0

/* ------------------------------------------------------------------ */
/* Status codes                                                        */
/* ------------------------------------------------------------------ */
#define EFI_SUCCESS               0ULL
#define EFI_ERR(x)               ((EFI_STATUS)(0x8000000000000000ULL | (x)))
#define EFI_LOAD_ERROR           EFI_ERR(1)
#define EFI_INVALID_PARAMETER    EFI_ERR(2)
#define EFI_UNSUPPORTED          EFI_ERR(3)
#define EFI_BUFFER_TOO_SMALL     EFI_ERR(5)
#define EFI_NOT_FOUND            EFI_ERR(14)
#define EFI_OUT_OF_RESOURCES     EFI_ERR(9)
#define EFI_SECURITY_VIOLATION   EFI_ERR(26)
#define EFI_ERROR(s)             ((INT64)(s) < 0)

/* ------------------------------------------------------------------ */
/* GUID                                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

#define EFI_GUID_INIT(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { (a), (b), (c), { (d0),(d1),(d2),(d3),(d4),(d5),(d6),(d7) } }

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    EFI_GUID_INIT(0x5b1b31a1,0x9562,0x11d2,0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b)

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    EFI_GUID_INIT(0x964e5b22,0x6459,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b)

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    EFI_GUID_INIT(0x9042a9de,0x23dc,0x4a38,0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a)

/* EFI Firmware Volume 2 — used to load the embedded UEFI shell */
#define EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID \
    EFI_GUID_INIT(0x220e73b6,0x6bdb,0x4413,0x84,0x05,0xb9,0x74,0xb1,0x08,0x61,0x9a)

/* Serial I/O — used to detach the UEFI terminal mirror from ConOut */
#define EFI_SERIAL_IO_PROTOCOL_GUID \
    EFI_GUID_INIT(0xBB25CF6F,0xF1D4,0x11D2,0x9A,0x0C,0x00,0x90,0x27,0x3F,0xC1,0xFD)

/* Well-known GUID of the UEFI Shell 2.0 application in OVMF/EDK2 firmware */
#define EFI_SHELL_APP_GUID \
    EFI_GUID_INIT(0x7C04A583,0x9E3E,0x4F1C,0xAD,0x65,0xE0,0x52,0x68,0xD0,0xB4,0xD1)

#define EFI_SECTION_PE32 0x10U

typedef struct _EFI_FV2 EFI_FIRMWARE_VOLUME2_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FV2_READ_SECTION)(
    EFI_FIRMWARE_VOLUME2_PROTOCOL *This,
    EFI_GUID                      *NameGuid,
    UINT8                          SectionType,
    UINTN                          SectionInstance,
    void                         **Buffer,
    UINTN                         *BufferSize,
    UINT32                        *AuthenticationStatus);

struct _EFI_FV2 {
    void *GetVolumeAttributes;
    void *SetVolumeAttributes;
    void *ReadFile;
    EFI_FV2_READ_SECTION ReadSection;
    /* remaining methods omitted — we only need ReadSection */
};

/* ------------------------------------------------------------------ */
/* Table header                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ------------------------------------------------------------------ */
/* Simple Text Output                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT32  MaxMode;
    UINT32  Mode;
    UINT32  Attribute;
    UINT32  CursorColumn;
    UINT32  CursorRow;
    BOOLEAN CursorVisible;
} EFI_SIMPLE_TEXT_OUTPUT_MODE;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void                                          *Reset;
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                      CHAR16 *String);
    void *TestString;
    EFI_STATUS (EFIAPI *QueryMode)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                   UINTN ModeNumber,
                                   UINTN *Columns, UINTN *Rows);
    void *SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                      UINTN Attribute);
    EFI_STATUS (EFIAPI *ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
    EFI_STATUS (EFIAPI *SetCursorPosition)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                           UINTN Column, UINTN Row);
    EFI_STATUS (EFIAPI *EnableCursor)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                      BOOLEAN Visible);
    EFI_SIMPLE_TEXT_OUTPUT_MODE *Mode;
};

/* Console attribute colors */
#define EFI_BLACK         0x00
#define EFI_BLUE          0x01
#define EFI_GREEN         0x02
#define EFI_CYAN          0x03
#define EFI_RED           0x04
#define EFI_MAGENTA       0x05
#define EFI_BROWN         0x06
#define EFI_LIGHTGRAY     0x07
#define EFI_BRIGHT        0x08
#define EFI_LIGHTBLUE     0x09
#define EFI_LIGHTGREEN    0x0A
#define EFI_LIGHTCYAN     0x0B
#define EFI_LIGHTRED      0x0C
#define EFI_LIGHTMAGENTA  0x0D
#define EFI_YELLOW        0x0E
#define EFI_WHITE         0x0F
#define EFI_BACKGROUND_BLACK     0x00
#define EFI_BACKGROUND_BLUE      0x10
#define EFI_BACKGROUND_GREEN     0x20
#define EFI_BACKGROUND_CYAN      0x30
#define EFI_BACKGROUND_RED       0x40
#define EFI_BACKGROUND_MAGENTA   0x50
#define EFI_BACKGROUND_BROWN     0x60
#define EFI_BACKGROUND_LIGHTGRAY 0x70
#define EFI_TEXT_ATTR(fg,bg)     ((fg) | (bg))

/* ------------------------------------------------------------------ */
/* Simple Text Input                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void       *Reset;
    EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
                                        EFI_INPUT_KEY *Key);
    EFI_EVENT   WaitForKey;
};

/* Scan codes for special keys */
#define SCAN_UP    0x01
#define SCAN_DOWN  0x02

/* ------------------------------------------------------------------ */
/* Memory types & descriptors                                          */
/* ------------------------------------------------------------------ */
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32            Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64            NumberOfPages;
    UINT64            Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_MEMORY_WB  0x0000000000000008ULL
#define EFI_PAGE_SIZE  4096
#define EFI_SIZE_TO_PAGES(s) (((s) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE)

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

/* ------------------------------------------------------------------ */
/* File Protocol                                                       */
/* ------------------------------------------------------------------ */
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *This,
                               EFI_FILE_PROTOCOL **NewHandle,
                               CHAR16 *FileName,
                               UINT64 OpenMode,
                               UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *This);
    void       *Delete;
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *This,
                               UINTN *BufferSize,
                               void *Buffer);
    void       *Write;
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL *This, UINT64 Position);
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *This,
                                  EFI_GUID *InformationType,
                                  UINTN *BufferSize,
                                  void *Buffer);
    void       *SetInfo;
    void       *Flush;
};

/* File open modes */
#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

/* EFI_FILE_INFO GUID */
#define EFI_FILE_INFO_GUID \
    EFI_GUID_INIT(0x09576e92,0x6d3f,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b)
#define EFI_FILE_INFO_ID EFI_FILE_INFO_GUID

/* File attributes */
#define EFI_FILE_DIRECTORY 0x0010ULL

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT64 CreateTime[2];
    UINT64 LastAccessTime[2];
    UINT64 ModificationTime[2];
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

/* ------------------------------------------------------------------ */
/* Block IO (raw sector access — used by ext4 reader)                  */
/* ------------------------------------------------------------------ */
#define EFI_BLOCK_IO_PROTOCOL_GUID \
    EFI_GUID_INIT(0x964e5b21,0x6459,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b)

typedef struct {
    UINT32  MediaId;
    BOOLEAN RemovableMedia;
    BOOLEAN MediaPresent;
    BOOLEAN LogicalPartition; /* TRUE = this handle is a partition, not a whole disk */
    BOOLEAN ReadOnly;
    BOOLEAN WriteCaching;
    /* 3 bytes natural padding here to align BlockSize to 4 */
    UINT32  BlockSize;
    UINT32  IoAlign;
    /* 4 bytes natural padding here to align LastBlock to 8 */
    EFI_LBA LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO_PROTOCOL EFI_BLOCK_IO_PROTOCOL;
struct EFI_BLOCK_IO_PROTOCOL {
    UINT64              Revision;
    EFI_BLOCK_IO_MEDIA *Media;
    void               *Reset;
    EFI_STATUS (EFIAPI *ReadBlocks)(EFI_BLOCK_IO_PROTOCOL *This,
                                    UINT32 MediaId, EFI_LBA Lba,
                                    UINTN BufferSize, void *Buffer);
    void *WriteBlocks;
    void *FlushBlocks;
};

/* ------------------------------------------------------------------ */
/* Disk IO (byte-granular access — simpler than Block IO)              */
/* ------------------------------------------------------------------ */
#define EFI_DISK_IO_PROTOCOL_GUID \
    EFI_GUID_INIT(0xce345171,0xba0b,0x11d2,0x8e,0x4f,0x00,0xa0,0xc9,0x69,0x72,0x3b)

typedef struct EFI_DISK_IO_PROTOCOL EFI_DISK_IO_PROTOCOL;
struct EFI_DISK_IO_PROTOCOL {
    UINT64     Revision;
    EFI_STATUS (EFIAPI *ReadDisk)(EFI_DISK_IO_PROTOCOL *This,
                                   UINT32 MediaId, UINT64 Offset,
                                   UINTN  BufferSize, void *Buffer);
    void *WriteDisk;
};

/* ------------------------------------------------------------------ */
/* Simple File System                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(void *This,
                                     EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* ------------------------------------------------------------------ */
/* Device Path                                                         */
/* ------------------------------------------------------------------ */
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
    EFI_GUID_INIT(0x09576e91,0x6d3f,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b)

typedef struct {
    UINT8  Type;
    UINT8  SubType;
    UINT8  Length[2]; /* little-endian total size of this node */
} EFI_DEVICE_PATH_PROTOCOL;

#define EFI_DP_END_TYPE         0x7Fu
#define EFI_DP_END_SUBTYPE      0xFFu
#define EFI_DP_MEDIA_TYPE       0x04u
#define EFI_DP_FILEPATH_SUBTYPE 0x04u

/* ------------------------------------------------------------------ */
/* Loaded Image                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT32       Revision;
    EFI_HANDLE   ParentHandle;
    void        *SystemTable;
    EFI_HANDLE   DeviceHandle;       /* Device the image was loaded from */
    void        *FilePath;
    void        *Reserved;
    UINT32       LoadOptionsSize;
    void        *LoadOptions;
    void        *ImageBase;
    UINT64       ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void        *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ------------------------------------------------------------------ */
/* Graphics Output                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32                    Version;
    UINT32                    HorizontalResolution;
    UINT32                    VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    UINT32                    PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32                             MaxMode;
    UINT32                             Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                              SizeOfInfo;
    EFI_PHYSICAL_ADDRESS               FrameBufferBase;
    UINTN                              FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ------------------------------------------------------------------ */
/* Boot Services (subset we use)                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    EFI_TABLE_HEADER Hdr;

    /* Task Priority */
    void *RaiseTPL;
    void *RestoreTPL;

    /* Memory */
    EFI_STATUS (EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE Type,
                                        EFI_MEMORY_TYPE MemoryType,
                                        UINTN Pages,
                                        EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize,
                                       EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                       UINTN *MapKey,
                                       UINTN *DescriptorSize,
                                       UINT32 *DescriptorVersion);
    EFI_STATUS (EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType,
                                       UINTN Size,
                                       void **Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);

    /* Events & Timers */
    void *CreateEvent;
    void *SetTimer;
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents,
                                       EFI_EVENT *Event,
                                       UINTN *Index);
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;

    /* Protocols */
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle,
                                         EFI_GUID *Protocol,
                                         void **Interface);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    /* Image */
    EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN             BootPolicy,
                                    EFI_HANDLE          ParentImageHandle,
                                    void               *FilePath,
                                    void               *SourceBuffer,
                                    UINTN               SourceSize,
                                    EFI_HANDLE         *ImageHandle);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE          ImageHandle,
                                     UINTN              *ExitDataSize,
                                     CHAR16            **ExitData);
    void *Exit;
    EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE ImageHandle);
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);

    /* Misc */
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
    void *SetWatchdogTimer;

    /* DriverSupport */
    void *ConnectController;
    void *DisconnectController;

    /* Open/Close Protocol */
    EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE Handle,
                                       EFI_GUID *Protocol,
                                       void **Interface,
                                       EFI_HANDLE AgentHandle,
                                       EFI_HANDLE ControllerHandle,
                                       UINT32 Attributes);
    void *CloseProtocol;
    void *OpenProtocolInformation;

    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(UINTN SearchType,
                                             EFI_GUID *Protocol,
                                             void *SearchKey,
                                             UINTN *NoHandles,
                                             EFI_HANDLE **Buffer);
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID *Protocol,
                                         void *Registration,
                                         void **Interface);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;

    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL  0x00000001

/* ------------------------------------------------------------------ */
/* Runtime Services (stub — only what we might need)                   */
/* ------------------------------------------------------------------ */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *GetTime;
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    void *GetVariable;
    void *GetNextVariableName;
    void *SetVariable;
    void *GetNextHighMonotonicCount;
    void *ResetSystem;
    /* ... */
} EFI_RUNTIME_SERVICES;

/* ------------------------------------------------------------------ */
/* Configuration Table                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    EFI_GUID VendorGuid;
    void    *VendorTable;
} EFI_CONFIGURATION_TABLE;

#define ACPI_20_TABLE_GUID \
    EFI_GUID_INIT(0x8868e871,0xe4f1,0x11d3,0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81)

/* ------------------------------------------------------------------ */
/* System Table                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    EFI_TABLE_HEADER               Hdr;
    CHAR16                        *FirmwareVendor;
    UINT32                         FirmwareRevision;
    EFI_HANDLE                     ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE                     ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*ConOut;
    EFI_HANDLE                     StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*StdErr;
    EFI_RUNTIME_SERVICES          *RuntimeServices;
    EFI_BOOT_SERVICES             *BootServices;
    UINTN                          NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE       *ConfigurationTable;
} EFI_SYSTEM_TABLE;
