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
// Hard iteration cap so a null/absent TimerLib (constant GetPerformanceCounter)
// cannot make the time bound unreachable and hang DXE forever.
//
#define SMU_PATCH_MAILBOX_POLL_MAX_ITER  1000000U

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


//
// Manual hook sites (the non-compilable steps applied by the driver, after
// injecting the compiled payload).  Everything else lives in the payload
// (payload/src, built to SmuPatchPayload.h).  Each stock fill loads its
// per-core loop bound with a 3-byte "l8ui a13,<cfg>,26"; the driver replaces
// each with a 3-byte "j <helper>" that sets a13 = min(count, 6) and resumes.
//
//   0x29BE5  j smu_clamp_current   (Current fill FUN_00029aec, resume 0x29BE8)
//   0x29A0F  j smu_clamp_average   (Average fill FUN_00029848, resume 0x29A12)
//   0x29A1D  j smu_patch_entry     (emit tail; replaces "l32r a7, 0xCA54")
//
//
//   0x29CB8  j smu_spare_entry     (stamp Current.Spare after the Current fill)
//
#define SMU_PATCH_CLAMP_CURRENT_ADDR 0x00029BE5U
#define SMU_PATCH_CLAMP_AVERAGE_ADDR 0x00029A0FU
#define SMU_PATCH_HOOK_ADDR          0x00029A1DU
#define SMU_PATCH_SPARE_ADDR         0x00029CB8U

//
// Metrics relocation.  The Current/Average fills load their base from the single
// literal at image 0x17F24; the deploy patches it to the free metrics region so
// the extra cores land where the driver reads them (see payload/src/smu_patch_map.h).
//
#define SMU_PATCH_RELOC_LITERAL      0x00017F24U
#define SMU_PATCH_METRICS_RELOC      0x0003B500U

//
// SMU cache-maintenance registers/helpers (see the firmware map).  Writes into
// live SMU code are only visible to the SMU after a d-cache + i-cache publish;
// without it the SMU may run stale or torn instructions.
//
#define SMU_PATCH_CP_CB              0x000184F8U
#define SMU_PATCH_CP_FLAG            0x0003FFBCU
#define SMU_PATCH_CP_MASK            0x0003FFF0U
#define SMU_PATCH_CACHE_FN_DATA      0x30B3U
#define SMU_PATCH_CACHE_FN_INSTR     0x30EBU
#define SMU_PATCH_CACHE_MARKER       0x0EU

//
// Max distinct dwords the four 3-byte hook sites can touch (a site may straddle
// two dwords); bounds the rollback snapshot in InstallHooks().
//
#define SMU_PATCH_MAX_HOOK_DWORDS    12U

#endif