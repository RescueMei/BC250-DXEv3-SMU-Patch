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
  { 0x29863, 3, { 0xA2, 0x14, 0x70 } },
  { 0x2987E, 3, { 0xA2, 0x54, 0x70 } },
  { 0x29884, 3, { 0xA2, 0x14, 0x72 } },
  { 0x2989C, 3, { 0xA2, 0x54, 0x72 } },
  { 0x298A2, 3, { 0xA2, 0x14, 0x73 } },
  { 0x298BA, 3, { 0xA2, 0x54, 0x73 } },
  { 0x298BD, 3, { 0xA2, 0x14, 0x74 } },
  { 0x298CB, 3, { 0xB2, 0x54, 0x74 } },
  { 0x298DD, 3, { 0xA2, 0x14, 0x75 } },
  { 0x298E8, 3, { 0xA2, 0x54, 0x75 } },
  { 0x29900, 3, { 0xA2, 0x22, 0x3B } },
  { 0x2991A, 3, { 0xA2, 0x22, 0x3D } },
  { 0x2991D, 3, { 0x82, 0x62, 0x3B } },
  { 0x29937, 3, { 0xA2, 0x22, 0x3F } },
  { 0x2993A, 3, { 0xB2, 0x62, 0x3D } },
  { 0x29946, 3, { 0xA2, 0x62, 0x3F } },
  { 0x29960, 3, { 0xA2, 0x24, 0x41 } },
  { 0x2997A, 3, { 0xA2, 0x6D, 0x81 } },
  { 0x299A2, 3, { 0xA2, 0x12, 0x44 } },
  { 0x299C2, 3, { 0xA2, 0x52, 0x44 } },
  { 0x299C8, 3, { 0xA2, 0x26, 0x26 } },
  { 0x299DD, 3, { 0xA2, 0x66, 0x26 } },
  { 0x299E0, 3, { 0xA2, 0x12, 0x5C } },
  { 0x299FD, 3, { 0xA2, 0x12, 0x68 } },
  { 0x29A00, 3, { 0xB2, 0x52, 0x5C } },
  { 0x29A0C, 3, { 0xA2, 0x52, 0x68 } },
  { 0x29A4B, 3, { 0xA2, 0x13, 0x64 } },
  { 0x29A62, 3, { 0xA2, 0x13, 0x66 } },
  { 0x29A65, 3, { 0x82, 0x53, 0x64 } },
  { 0x29A71, 3, { 0xA2, 0x53, 0x66 } },
  { 0x29A8E, 3, { 0xA2, 0x14, 0x71 } },
  { 0x29AA9, 3, { 0xA2, 0x54, 0x71 } },
  { 0x29AAF, 3, { 0xA2, 0x14, 0x84 } },
  { 0x29AC7, 3, { 0xA2, 0x54, 0x84 } },
  { 0x29ACD, 3, { 0xA2, 0x14, 0x85 } },
  { 0x29AD9, 3, { 0xA2, 0x54, 0x85 } },
  { 0x29B6E, 3, { 0x92, 0x52, 0x2E } },
  { 0x29B71, 3, { 0xF2, 0x52, 0x30 } },
  { 0x29B74, 3, { 0xE2, 0x52, 0x2C } },
  { 0x29B77, 3, { 0x82, 0x52, 0x2F } },
  { 0x29B92, 3, { 0xC2, 0x52, 0x31 } },
  { 0x29BCD, 3, { 0xA2, 0x6F, 0x19 } },
  { 0x29BD0, 3, { 0x82, 0x6F, 0x1B } },
  { 0x29BD3, 3, { 0xE2, 0x6F, 0x1D } },
  { 0x29BEE, 3, { 0x82, 0x62, 0x1F } },
  { 0x29C30, 2, { 0xC9, 0x4D } },
  { 0x29C32, 3, { 0xB2, 0x5A, 0x18 } },
  { 0x29C35, 3, { 0x82, 0x5A, 0x24 } },
  { 0x29C69, 3, { 0xF2, 0x5E, 0x20 } },
  { 0x29C6C, 3, { 0xD2, 0x5E, 0x22 } },
  { 0x29C9F, 3, { 0xE2, 0x52, 0x40 } },
  { 0x29CA2, 3, { 0xF2, 0x52, 0x2D } },
  { 0x29CA5, 3, { 0xD2, 0x52, 0x41 } },
  { 0x29CB2, 3, { 0xC2, 0xA1, 0x1C } },
};

#define SMU_PATCH_TABLE_SIZE  (sizeof (mSmuPatchSites) / sizeof (mSmuPatchSites[0]))

#endif