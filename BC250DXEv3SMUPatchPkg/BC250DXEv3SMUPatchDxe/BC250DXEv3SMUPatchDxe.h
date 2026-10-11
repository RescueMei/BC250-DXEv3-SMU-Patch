#ifndef BC250DXEV3SMUPATCHDXE_H_
#define BC250DXEV3SMUPATCHDXE_H_

#include <Uefi.h>

//
// Host bridge PCI configuration-space access window (SMN indirect access).
// The SMU mailbox registers are addressed through the SMN window.
//
#define SMU_PATCH_PCI_SEGMENT       0
#define SMU_PATCH_PCI_BUS           0
#define SMU_PATCH_PCI_DEVICE        0
#define SMU_PATCH_PCI_FUNCTION      0
#define SMU_PATCH_SMN_INDEX_OFFSET  0xB8
#define SMU_PATCH_SMN_DATA_OFFSET   0xBC

//
// SMU Queue 2 mailbox registers (transfer-engine interface).
//
#define SMU_PATCH_Q2_CMD            0x03B10528U
#define SMU_PATCH_Q2_RSP            0x03B10564U
#define SMU_PATCH_Q2_ARG            0x03B10998U

//
// SMU Queue 3 mailbox registers (gated write-through / SMN window).
//
#define SMU_PATCH_Q3_CMD            0x03B10A20U
#define SMU_PATCH_Q3_RSP            0x03B10A80U
#define SMU_PATCH_Q3_ARG            0x03B10A88U

//
// SMU Queue 2 message 0x0A sub-operations used for SMU-local SRAM reads.
//
#define SMU_PATCH_Q2_MSG_TRANSFER   0x0AU
#define SMU_PATCH_SUB_SRAM_LOAD     0x1FU   // copy SMU SRAM[src] to the key-3 entry
#define SMU_PATCH_SUB_SMU2DRAM      0x14U   // DMA the key-3 entry to DRAM[hi:lo]

//
// SMU Queue 3 gated (privileged) messages.
//
#define SMU_PATCH_Q3_MSG_SET_WRITE_PTR  0x28U  // point write-through at an SMU-local address
#define SMU_PATCH_Q3_MSG_WRITE_THROUGH   0x29U  // SMU-CPU 32-bit store through the pointer
#define SMU_PATCH_Q3_MSG_SMN_PROBE       0x2AU  // 32-bit SMN read via mem64 window (gated probe)

//
// Mailbox terminal status values (from the Linux/Python reference implementation).
//
#define SMU_PATCH_STATUS_OK              0x01U
#define SMU_PATCH_STATUS_FAILED          0xFFU
#define SMU_PATCH_STATUS_UNKNOWN_CMD     0xFEU
#define SMU_PATCH_STATUS_REJECTED_PREREQ 0xFDU
#define SMU_PATCH_STATUS_REJECTED_BUSY   0xFCU

//
// Polling parameters.
//
#define SMU_PATCH_MAILBOX_POLL_DELAY_US  2000U
#define SMU_PATCH_MAILBOX_TIMEOUT_US     5000000U

//
// DMA page for transfer-engine reads.
//
#define SMU_PATCH_DMA_PAGE_SIZE          4096U

//
// Enable gate: read the DXEv3 menu driver's SMU patch variable. The patch is
// only applied when SmuReportingPatch (offset 0) is set.
//
#define SMU_PATCH_CONFIG_VAR_NAME        L"MeiMeiDXEv3SmuPatchVar"
#define SMU_PATCH_CONFIG_VAR_OFFSET      0U
#define SMU_PATCH_CONFIG_ENABLED         1U

extern EFI_GUID  gMeiMeiDXEv3ConfigVarGuid;

//
// One patch site. Data is the replacement instruction bytes at the given
// SMU-local address. The reference patcher applies these with a byte-granular
// read-modify-write through the gated write-through pointer.
//
typedef struct {
  UINT32  Address;
  UINT8   Size;
  UINT8   Data[3];
} SMU_PATCH_SITE;

STATIC CONST SMU_PATCH_SITE  mSmuPatchSites[] = {
  { 0x29A10, 2, { 0xA0, 0x06 } },
  { 0x29A1D, 3, { 0xC6, 0x7C, 0x45 } },
  { 0x3B000, 3, { 0xD4, 0x6E, 0x01 } },
  { 0x3B004, 3, { 0xD8, 0x6E, 0x01 } },
  { 0x3B008, 2, { 0x90, 0xCC } },
  { 0x3B00C, 2, { 0xC4, 0xFA } },
  { 0x3B010, 2, { 0x54, 0xCA } },
  { 0x3B014, 3, { 0x82, 0xA0, 0x67 } },
  { 0x3B017, 3, { 0x82, 0x54, 0x73 } },
  { 0x3B01A, 3, { 0x0C, 0x63, 0x51 } },
  { 0x3B01D, 3, { 0xFB, 0xFF, 0x5A } },
  { 0x3B020, 3, { 0x53, 0x52, 0x05 } },
  { 0x3B024, 3, { 0x61, 0xFA, 0xFF } },
  { 0x3B027, 3, { 0x72, 0xA1, 0x78 } },
  { 0x3B02A, 3, { 0x70, 0x75, 0xD1 } },
  { 0x3B02D, 3, { 0x7A, 0x66, 0x22 } },
  { 0x3B030, 3, { 0xA0, 0x68, 0x4A } },
  { 0x3B033, 3, { 0x22, 0x20, 0x23 } },
  { 0x3B036, 3, { 0x90, 0x5C, 0xC9 } },
  { 0x3B039, 3, { 0x4A, 0x99, 0x90 } },
  { 0x3B03C, 3, { 0x93, 0xA0, 0xE1 } },
  { 0x3B03F, 3, { 0xF0, 0xFF, 0x03 } },
  { 0x3B042, 1, { 0x0E } },
  { 0x3B044, 3, { 0x13, 0x06, 0x50 } },
  { 0x3B048, 2, { 0x11, 0x2A } },
  { 0x3B04B, 3, { 0xA1, 0xEA, 0xA2 } },
  { 0x3B04E, 3, { 0x52, 0x3A, 0x53 } },
  { 0x3B051, 2, { 0x06, 0x04 } },
  { 0x3B054, 2, { 0x55, 0x2A } },
  { 0x3B057, 3, { 0xA5, 0xEA, 0xA2 } },
  { 0x3B05A, 3, { 0x69, 0x20, 0xE1 } },
  { 0x3B05D, 3, { 0xEA, 0xFF, 0x23 } },
  { 0x3B060, 1, { 0x0E } },
  { 0x3B062, 3, { 0x33, 0x06, 0x07 } },
  { 0x3B065, 3, { 0x20, 0x33, 0x2A } },
  { 0x3B069, 3, { 0xA3, 0xEA, 0xA2 } },
  { 0x3B06C, 3, { 0x52, 0x4C, 0x43 } },
  { 0x3B06F, 3, { 0x06, 0x53, 0x20 } },
  { 0x3B072, 2, { 0x44, 0x2A } },
  { 0x3B075, 3, { 0xA4, 0xEA, 0xA2 } },
  { 0x3B078, 3, { 0x52, 0x56, 0x1B } },
  { 0x3B07B, 3, { 0x33, 0x0C, 0xC6 } },
  { 0x3B07E, 3, { 0x67, 0xBA, 0x9A } },
  { 0x3B081, 3, { 0x71, 0xE3, 0xFF } },
  { 0x3B084, 3, { 0x06, 0x66, 0xBA } },
};

#define SMU_PATCH_TABLE_SIZE  (sizeof (mSmuPatchSites) / sizeof (mSmuPatchSites[0]))

#endif