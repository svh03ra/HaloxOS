// Copyright Svh03ra (C) 2026, All rights reserved
// Source File: loader/efi/efi.h, the slice of the UEFI interfaces this loader uses.
//
// This repository is licensed under the GNU General Public License.

/*
 * Only the parts of the UEFI specification the loader actually calls are
 * declared here.  Every firmware entry point is declared with the calling
 * convention UEFI uses for the architecture being built: the Microsoft x64
 * convention (ms_abi) on x86_64, and plain cdecl on IA32.  GCC supports both
 * directly, so no assembly thunks and no gnu-efi are needed to talk to the
 * firmware - and these declarations describe both firmwares, because the only
 * thing that differs between them is the width of the natural word.
 *
 * The structures below are declared in specification order, because UEFI
 * dispatches through the *offset* of each member.  Do not reorder them; a
 * field out of place calls the wrong firmware function.  The same applies to
 * UINTN: narrowing it for IA32 moves every member after the first one, which
 * is exactly what the 32-bit firmware expects and what a 64-bit-only UINTN
 * gets wrong.
 *
 * Two of the structures carry a member written out only for x86_64: the four
 * bytes of space a firmware with eight-byte pointers leaves between a UINT32
 * and the pointer after it.  IA32 firmware aligns pointers to four bytes and
 * has no such hole, so declaring it unconditionally would push every member
 * after it four bytes along and have this loader read the console, and its own
 * device handle, out of the wrong words - a hang with a perfectly ordinary-
 * looking trace rather than a compile error.
 */

#ifndef HALOXOS_EFI_H
#define HALOXOS_EFI_H

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
typedef long long INT64;

/*
 * UINTN is the firmware's natural word, so it is 64 bits on x86_64 firmware and
 * 32 on IA32 firmware.  It sizes EFI_STATUS and every pointer in every protocol
 * structure below; everything else in UEFI is a fixed width.  Building for the
 * wrong one does not fail to compile - it silently reads the firmware's tables
 * at the wrong offsets - so it is chosen from the target, never assumed.
 */
#if defined(__x86_64__)
typedef unsigned long long UINTN;
typedef long long INTN;
#define EFIAPI __attribute__((ms_abi))
#elif defined(__i386__)
typedef unsigned int UINTN;
typedef int INTN;
#define EFIAPI __attribute__((cdecl))
#else
#error "HaloxOS UEFI loader: no calling convention defined for this target"
#endif

typedef UINTN EFI_STATUS;
typedef UINTN EFI_TPL;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;
typedef void *EFI_HANDLE;
typedef unsigned short CHAR16;

/*
 * A status is an error when its top bit is set, and the top bit is the top bit
 * of UINTN - 64 on x86_64 firmware, 32 on IA32.  Spelling the codes as 64-bit
 * constants would drop that bit on IA32 and turn EFI_NOT_READY into the success
 * code 6, so a machine whose firmware has no such call would look like one that
 * answered instead of like one that failed.  The width is part of the value.
 */
#define EFI_ERROR(status) ((INTN)(status) < 0)

#if defined(__x86_64__)
#define EFI_MAX_BIT 0x8000000000000000ull
#else
#define EFI_MAX_BIT 0x80000000u
#endif

#define EFI_SUCCESS 0u
#define EFI_NOT_READY (EFI_MAX_BIT | 6u)
#define EFI_BUFFER_TOO_SMALL (EFI_MAX_BIT | 5u)
#define EFI_ABORTED (EFI_MAX_BIT | 21u)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *,
                                            CHAR16 *);
typedef EFI_STATUS(EFIAPI *EFI_TEXT_CLEAR_SCREEN)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *,
                                                   UINTN);

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_TEXT_STRING OutputString;
    void *TestString;
    void *QueryMode;
    void *SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS(EFIAPI *EFI_INPUT_READ_KEY)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *,
                                               EFI_INPUT_KEY *);

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void *Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    void *WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef enum {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE, EFI_MEMORY_TYPE, UINTN,
                                               EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS(EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *,
                                               UINTN *, UINT32 *);
typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE, UINTN, void **);
typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);
typedef EFI_STATUS(EFIAPI *EFI_STALL)(UINTN);
typedef EFI_STATUS(EFIAPI *EFI_SET_WATCHDOG_TIMER)(UINTN, UINT64, UINTN, CHAR16 *);
typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(UINT32, EFI_GUID *, void *, UINTN *,
                                                     EFI_HANDLE **);
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    void *FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STALL Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *ComputeCrc32;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
#if defined(__x86_64__)
    UINT32 Padding; /* the hole IA32 firmware does not have: see the top comment */
#endif
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* Loaded image: the firmware tells us which volume we were loaded from. */
typedef struct {
    UINT32 Revision;
#if defined(__x86_64__)
    UINT32 Padding; /* the hole IA32 firmware does not have: see the top comment */
#endif
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
#if defined(__x86_64__)
    UINT32 Padding2; /* the hole IA32 firmware does not have: see the top comment */
#endif
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* Files: enough of the protocol to open, size, read and close a file. */
struct EFI_FILE_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(struct EFI_FILE_PROTOCOL *, struct EFI_FILE_PROTOCOL **,
                                          CHAR16 *, UINT64, UINT64);
typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(struct EFI_FILE_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(struct EFI_FILE_PROTOCOL *, UINTN *, void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(struct EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *,
                                              void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_WRITE)(struct EFI_FILE_PROTOCOL *, UINTN *, void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_POSITION)(struct EFI_FILE_PROTOCOL *, UINT64);

typedef struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void *Delete;
    EFI_FILE_READ Read;
    EFI_FILE_WRITE Write;
    void *GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    void *SetInfo;
    void *Flush;
    void *OpenEx;
    void *ReadEx;
    void *WriteEx;
    void *FlushEx;
} EFI_FILE_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN_VOLUME)(void *, EFI_FILE_PROTOCOL **);

typedef struct {
    UINT64 Revision;
    EFI_FILE_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* Graphics output: the source of the linear framebuffer and its colour masks. */
typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_QUERY_MODE)(
    void *, UINT32, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_SET_MODE)(void *, UINT32);

typedef struct {
    EFI_GRAPHICS_OUTPUT_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_SET_MODE SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ACPI tables are handed over through the configuration table, by GUID. */
typedef struct {
    UINT8 Signature[8];
    UINT8 Checksum;
    UINT8 OemId[6];
    UINT8 Revision;
    UINT32 RsdtAddress;
    UINT32 Length;
    UINT64 XsdtAddress;
    UINT8 ExtendedChecksum;
    UINT8 Reserved[3];
} EFI_ACPI_RSDP;

static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5B1B31A1u, 0x9562u, 0x11d2u, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
    0x964E5B22u, 0x6459u, 0x11d2u, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
static const EFI_GUID EFI_FILE_INFO_GUID = {
    0x09576E92u, 0x6D3Fu, 0x11d2u, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
static const EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042A9DEu, 0x23DCu, 0x4A38u, {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}};
static const EFI_GUID EFI_ACPI_20_TABLE_GUID = {
    0x8868E871u, 0xE4F1u, 0x11D3u, {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}};
static const EFI_GUID EFI_ACPI_TABLE_GUID = {
    0xEB9D2D30u, 0x2D88u, 0x11D3u, {0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}};

#define EFI_FILE_MODE_READ  0x0000000000000001ull
#define EFI_FILE_MODE_WRITE 0x0000000000000002ull
#define EFI_FILE_MODE_CREATE 0x8000000000000000ull

/* Enter the kernel from the loader in 32-bit protected mode.  On x86_64 the
 * transition code is first copied to this low physical page: compatibility
 * mode can address only its low 4 GiB, while UEFI may load BOOTX64.EFI above
 * that boundary.  jump32.asm ignores the page because IA32 firmware already
 * executes below 4 GiB.  Neither implementation returns. */
void EFIAPI efi_enter_loader(UINT32 loader_entry, UINT32 multiboot_info,
                             UINT32 handoff_page);

#endif /* HALOXOS_EFI_H */
