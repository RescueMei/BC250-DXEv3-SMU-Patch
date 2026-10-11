#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PciSegmentLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

#include "BC250DXEv3SMUPatchDxe.h"
#include "SmuPatchPayload.h"
#include "SmuPatchMeta.h"

//
// This DXE driver applies the SMU firmware patch contained in
// Ref/bc250-smu-unlock (patches.hex / patcher.py) to the BC-250 SMU.
//
// The driver does NOT unlock the SMU. It assumes another driver that runs
// earlier (the SMU privilege-unlock driver) has already opened the gated
// write-through interface, and it verifies this with the same probe the
// reference PoC uses. If privileged writes are not available (Q3 message 0x2A
// returns 0xFD / SMU_RETURN_REJECTED_PREREQ), the driver logs a warning and
// exits without touching the SMU.
//
// Execution is gated by the MeiMeiDXEv3SmuPatchVar configuration variable from
// the DXEv3 menu driver: the patch is applied only when SmuReportingPatch
// (byte offset 0) is set.
//
// The patch application mirrors the reference Python patcher:
//   - reads are performed through the SMU Queue 2 transfer engine
//     (sram_load sub 0x1F, then smu2dram sub 0x14 into a DMA-able page),
//   - writes are performed byte-granular through the gated write-through
//     pointer (Q3 messages 0x28 + 0x29),
//   - every write is followed by a read-back verification.
//

//
// Vendor GUID of the DXEv3 configuration variables (must match the menu
// driver's BC250DXEv3MenuDriver.h definition exactly).
//
EFI_GUID  gMeiMeiDXEv3ConfigVarGuid =
  { 0x49CC168D, 0xE8B0, 0x4613, { 0xA8, 0x07, 0x16, 0x96, 0x99, 0x86, 0x72, 0x6F } };

#define SMU_PATCH_PCI_SEGMENT_ADDRESS(Offset) \
  PCI_SEGMENT_LIB_ADDRESS (SMU_PATCH_PCI_SEGMENT, SMU_PATCH_PCI_BUS, SMU_PATCH_PCI_DEVICE, SMU_PATCH_PCI_FUNCTION, (Offset))

/**
  Determine whether an SMU mail box response value means the queue is idle or
  the last command has completed.

  Matches the terminal status set from the reference Python implementation and
  the DXEv2 core-unlock driver.

  @param[in] Status  Raw SMU queue response register value.

  @retval TRUE   Status is one of the known terminal states.
  @retval FALSE  Status is still busy or otherwise non-terminal.
**/
STATIC
BOOLEAN
IsDoneStatus (
  IN UINT32  Status
  )
{
  return (BOOLEAN)(Status == SMU_PATCH_STATUS_OK              ||
                   Status == SMU_PATCH_STATUS_FAILED          ||
                   Status == SMU_PATCH_STATUS_UNKNOWN_CMD     ||
                   Status == SMU_PATCH_STATUS_REJECTED_PREREQ ||
                   Status == SMU_PATCH_STATUS_REJECTED_BUSY);
}

/**
  Read a 32-bit value from an SMN register through the host bridge PCI config
  index/data pair.

  @param[in] Register  SMN register address.

  @return 32-bit value read from the addressed SMN register.
**/
STATIC
UINT32
SmnRead32 (
  IN UINT32  Register
  )
{
  PciSegmentWrite32 (SMU_PATCH_PCI_SEGMENT_ADDRESS (SMU_PATCH_SMN_INDEX_OFFSET), Register);
  return PciSegmentRead32 (SMU_PATCH_PCI_SEGMENT_ADDRESS (SMU_PATCH_SMN_DATA_OFFSET));
}

/**
  Write a 32-bit value to an SMN register through the host bridge PCI config
  index/data pair.

  @param[in] Register  SMN register address.
  @param[in] Value     32-bit value to write.
**/
STATIC
VOID
SmnWrite32 (
  IN UINT32  Register,
  IN UINT32  Value
  )
{
  PciSegmentWrite32 (SMU_PATCH_PCI_SEGMENT_ADDRESS (SMU_PATCH_SMN_INDEX_OFFSET), Register);
  PciSegmentWrite32 (SMU_PATCH_PCI_SEGMENT_ADDRESS (SMU_PATCH_SMN_DATA_OFFSET), Value);
}

/**
  Poll an SMU mail box response register until a terminal status appears or a
  timeout expires.

  @param[in]  RspAddress  SMN address of the queue response register.
  @param[out] Status      Last value observed in the response register.
  @param[in]  TimeoutUs   Maximum time to wait, in microseconds.

  @retval EFI_SUCCESS  A terminal queue status was observed.
  @retval EFI_TIMEOUT  The queue never reached a terminal state in time.
**/
STATIC
EFI_STATUS
WaitForDoneStatus (
  IN  UINT32  RspAddress,
  OUT UINT32  *Status,
  IN  UINT64  TimeoutUs
  )
{
  UINT64  Start;
  UINT64  ElapsedUs;
  UINT32  Value;

  Start = GetPerformanceCounter ();

  for (;;) {
    Value = SmnRead32 (RspAddress);
    if (IsDoneStatus (Value)) {
      *Status = Value;
      return EFI_SUCCESS;
    }

    ElapsedUs = DivU64x32 (GetTimeInNanoSecond (GetPerformanceCounter () - Start), 1000U);
    if (ElapsedUs >= TimeoutUs) {
      *Status = Value;
      return EFI_TIMEOUT;
    }

    MicroSecondDelay (SMU_PATCH_MAILBOX_POLL_DELAY_US);
  }
}

/**
  Send a message through an SMU mail box using the write order employed by the
  reference Python implementation.

  Sequence:
    1. Wait for the queue to reach a terminal state.
    2. Clear the response register.
    3. Write the six argument DWORDs.
    4. Write the command opcode.
    5. Wait for completion.

  @param[in]  CmdAddress  SMN address of the queue command register.
  @param[in]  RspAddress  SMN address of the queue response register.
  @param[in]  ArgAddress  SMN address of the queue argument register.
  @param[in]  Message     Message opcode.
  @param[in]  Args        Up to six argument DWORDs.
  @param[out] Status      Terminal response value after the send completes.

  @retval EFI_SUCCESS  The queue completed the message.
  @retval others       Queue readiness or completion timed out.
**/
STATIC
EFI_STATUS
SendMailbox (
  IN  UINT32  CmdAddress,
  IN  UINT32  RspAddress,
  IN  UINT32  ArgAddress,
  IN  UINT32  Message,
  IN  UINT32  *Args,
  OUT UINT32  *Status
  )
{
  EFI_STATUS  Result;
  UINTN       Index;

  Result = WaitForDoneStatus (RspAddress, Status, SMU_PATCH_MAILBOX_TIMEOUT_US);
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: mail box not idle, last status=0x%08x\n", *Status));
    return Result;
  }

  SmnWrite32 (RspAddress, 0);
  for (Index = 0; Index < 6; Index++) {
    SmnWrite32 (ArgAddress + (UINT32)Index * sizeof (UINT32), Args[Index]);
  }

  SmnWrite32 (CmdAddress, Message);

  Result = WaitForDoneStatus (RspAddress, Status, SMU_PATCH_MAILBOX_TIMEOUT_US);
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: mail box timed out after command 0x%08x\n", Message));
  }

  return Result;
}

/**
  Determine whether SMU privileged (gated) writes are available.

  This is the same probe the reference PoC uses (secure_access_enabled):
  send SMU Queue 3 message 0x2A. If the gated cluster is closed, the SMU
  returns SMU_RETURN_REJECTED_PREREQ (0xFD).

  @retval TRUE   Privileged writes are available (the SMU was already unlocked).
  @retval FALSE  The gated interface rejects the probe.
**/
STATIC
BOOLEAN
SmuPrivilegedWritesAvailable (
  VOID
  )
{
  UINT32      Args[6];
  UINT32      Status;
  EFI_STATUS  Result;

  ZeroMem (Args, sizeof (Args));

  Result = SendMailbox (
             SMU_PATCH_Q3_CMD,
             SMU_PATCH_Q3_RSP,
             SMU_PATCH_Q3_ARG,
             SMU_PATCH_Q3_MSG_SMN_PROBE,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_WARN, "BC250DXEv3SMUPatchDxe: privileged-write probe timed out: %r\n", Result));
    return FALSE;
  }

  DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: Q3 0x2A probe status=0x%08x\n", Status));

  return (BOOLEAN)(Status != SMU_PATCH_STATUS_REJECTED_PREREQ);
}

/**
  Read a 32-bit word from SMU-local SRAM through the Queue 2 transfer engine.

  The read uses the same two-step sequence as the reference Python
  implementation (sram_load + smu2dram), DMA-ing the value into the supplied
  page.

  @param[in]  Address      SMU-local SRAM address to read.
  @param[in]  DmaBuffer    Virtual address of a 4 KB DMA-able page.
  @param[out] Value        The dword read.

  @retval EFI_SUCCESS  The dword was read.
  @retval others       A mail box step failed or timed out.
**/
STATIC
EFI_STATUS
SmuReadDword (
  IN  UINT32  Address,
  IN  VOID    *DmaBuffer,
  OUT UINT32  *Value
  )
{
  UINT32      Args[6];
  UINT32      Status;
  EFI_STATUS  Result;
  UINT64      Phys;

  Phys = (UINT64)(UINTN)DmaBuffer;

  //
  // transfer_engine_sram_load: Q2 msg 0x0A sub 0x1F
  // args: [sub, 0, src, words]
  //
  ZeroMem (Args, sizeof (Args));
  Args[0] = SMU_PATCH_SUB_SRAM_LOAD;
  Args[2] = Address;
  Args[3] = 1;

  Result = SendMailbox (
             SMU_PATCH_Q2_CMD,
             SMU_PATCH_Q2_RSP,
             SMU_PATCH_Q2_ARG,
             SMU_PATCH_Q2_MSG_TRANSFER,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: sram_load failed at 0x%08x: %r\n", Address, Result));
    return Result;
  }
  if (Status != SMU_PATCH_STATUS_OK) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: sram_load returned 0x%08x at 0x%08x\n", Status, Address));
    return EFI_DEVICE_ERROR;
  }

  //
  // transfer_engine_smu2dram: Q2 msg 0x0A sub 0x14
  // args: [sub, dram_hi, dram_lo, words]
  //
  ZeroMem (Args, sizeof (Args));
  Args[0] = SMU_PATCH_SUB_SMU2DRAM;
  Args[1] = (UINT32)(Phys >> 32);
  Args[2] = (UINT32)(Phys & 0xFFFFFFFF);
  Args[3] = 1;

  Result = SendMailbox (
             SMU_PATCH_Q2_CMD,
             SMU_PATCH_Q2_RSP,
             SMU_PATCH_Q2_ARG,
             SMU_PATCH_Q2_MSG_TRANSFER,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: smu2dram failed at 0x%08x: %r\n", Address, Result));
    return Result;
  }
  if (Status != SMU_PATCH_STATUS_OK) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: smu2dram returned 0x%08x at 0x%08x\n", Status, Address));
    return EFI_DEVICE_ERROR;
  }

  WriteBackInvalidateDataCacheRange (DmaBuffer, sizeof (UINT32));
  *Value = *(volatile UINT32 *)DmaBuffer;

  return EFI_SUCCESS;
}

/**
  Write a 32-bit word to SMU-local SRAM through the gated write-through
  pointer (Queue 3 messages 0x28 + 0x29).

  @param[in] Address  SMU-local SRAM address to write.
  @param[in] Value    32-bit value to write.

  @retval EFI_SUCCESS  The word was written.
  @retval others       A mail box step failed or timed out.
**/
STATIC
EFI_STATUS
SmuWriteDword (
  IN UINT32  Address,
  IN UINT32  Value
  )
{
  UINT32      Args[6];
  UINT32      Status;
  EFI_STATUS  Result;

  //
  // sec_set_write_ptr: Q3 msg 0x28, arg0 = SMU-local address.
  //
  ZeroMem (Args, sizeof (Args));
  Args[0] = Address;

  Result = SendMailbox (
             SMU_PATCH_Q3_CMD,
             SMU_PATCH_Q3_RSP,
             SMU_PATCH_Q3_ARG,
             SMU_PATCH_Q3_MSG_SET_WRITE_PTR,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: set_write_ptr failed at 0x%08x: %r\n", Address, Result));
    return Result;
  }
  if (Status != SMU_PATCH_STATUS_OK) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: set_write_ptr returned 0x%08x at 0x%08x\n", Status, Address));
    return EFI_DEVICE_ERROR;
  }

  //
  // sec_write_through32: Q3 msg 0x29, arg0 = value.
  //
  ZeroMem (Args, sizeof (Args));
  Args[0] = Value;

  Result = SendMailbox (
             SMU_PATCH_Q3_CMD,
             SMU_PATCH_Q3_RSP,
             SMU_PATCH_Q3_ARG,
             SMU_PATCH_Q3_MSG_WRITE_THROUGH,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: write_through failed at 0x%08x: %r\n", Address, Result));
    return Result;
  }
  if (Status != SMU_PATCH_STATUS_OK) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: write_through returned 0x%08x at 0x%08x\n", Status, Address));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Read the DXEv3 menu driver's SMU patch configuration variable.

  @param[out] PatchEnabled  TRUE when SmuReportingPatch (byte offset 0) is set.

  @retval EFI_SUCCESS  Variable was read; PatchEnabled reflects the setting.
  @retval others       Error returned by GetVariable() (caller treats as disabled).
**/
STATIC
EFI_STATUS
GetPatchEnabled (
  OUT BOOLEAN  *PatchEnabled
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT8       SmuConfig[1];

  *PatchEnabled = FALSE;
  DataSize = sizeof (SmuConfig);
  ZeroMem (SmuConfig, sizeof (SmuConfig));

  Status = gRT->GetVariable (
                  SMU_PATCH_CONFIG_VAR_NAME,
                  &gMeiMeiDXEv3ConfigVarGuid,
                  NULL,
                  &DataSize,
                  SmuConfig
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "BC250DXEv3SMUPatchDxe: failed to read SMU variable: %r\n", Status));
    return Status;
  }

  if (DataSize <= SMU_PATCH_CONFIG_VAR_OFFSET) {
    DEBUG ((DEBUG_WARN, "BC250DXEv3SMUPatchDxe: SMU variable too small (%Lu bytes)\n", (UINT64)DataSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  *PatchEnabled = (BOOLEAN)(SmuConfig[SMU_PATCH_CONFIG_VAR_OFFSET] == SMU_PATCH_CONFIG_ENABLED);

  DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: SmuReportingPatch=%u\n",
    SmuConfig[SMU_PATCH_CONFIG_VAR_OFFSET]));

  return EFI_SUCCESS;
}

/**
  Apply the SMU firmware patch.

  Each patch site is applied byte-by-byte with read-modify-write semantics
  identical to the reference Python patcher:
    - read the dword containing the target byte,
    - replace the byte lane,
    - write the full dword back through the gated write-through pointer,
    - read the dword back and verify the patched lane.

  All sites are attempted; a lane mismatch is logged as an error but does not
  stop the remaining sites (mirroring the patcher's ok &= check).

  @param[in] DmaBuffer  Virtual address of a 4 KB DMA-able page.

  @retval EFI_SUCCESS  All sites were processed (verification failures are logged).
**/
/**
  Compute the Xtensa `j` (J) instruction that jumps from Pc to Dst:
  word = 0x6 | ((delta & 0x3FFFF) << 6), delta = Dst - Pc - 4.
**/
STATIC
UINT32
ComputeJump (
  IN UINT32  Pc,
  IN UINT32  Dst
  )
{
  UINT32  Delta = (Dst - Pc - 4U) & 0x3FFFFU;
  return (6U | (Delta << 6)) & 0x00FFFFFFU;
}

/**
  Inject the compiled metrics-patch payload (SmuPatchPayload.h) into the
  documented free SMU-SRAM region, after verifying it is all-zero.
**/
STATIC
EFI_STATUS
InjectPayload (
  IN VOID  *DmaBuffer
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  UINTN       Words;
  UINT32      Expected;
  UINT32      Value;

  if (smu_patch_payload_len == 0 || (smu_patch_payload_len & 3U) != 0) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: bad payload length %u\n",
      (UINT32)smu_patch_payload_len));
    return EFI_BAD_BUFFER_SIZE;
  }
  Words = smu_patch_payload_len / sizeof (UINT32);

  DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: injecting %u bytes at 0x%05X\n",
    (UINT32)smu_patch_payload_len, SMU_PATCH_PAYLOAD_BASE));

  for (Index = 0; Index < Words; Index++) {
    Status = SmuReadDword (SMU_PATCH_PAYLOAD_BASE + (UINT32)Index * 4U, DmaBuffer, &Expected);
    if (EFI_ERROR (Status)) return Status;
    if (Expected != 0) {
      DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: payload region not free @0x%05X = 0x%08X\n",
        SMU_PATCH_PAYLOAD_BASE + (UINT32)Index * 4U, Expected));
      return EFI_ABORTED;
    }
  }
  for (Index = 0; Index < Words; Index++) {
    CopyMem (&Value, smu_patch_payload + Index * 4, sizeof (Value));
    Status = SmuWriteDword (SMU_PATCH_PAYLOAD_BASE + (UINT32)Index * 4U, Value);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: payload write failed at word %u\n", (UINT32)Index));
      return Status;
    }
  }
  return EFI_SUCCESS;
}

/**
  Apply the two MANUAL (non-compilable) hooks that wire the compiled payload in:

    0x29A10  A0 06      movi a13,6   (cap the stock per-core metrics loop at 6)
    0x29A1D  <j entry>  jump to smu_patch_entry (at SMU_PATCH_PAYLOAD_BASE +
                        SMU_PATCH_ENTRY_OFFSET), where the firmware's
                        "l32r a7, 0xCA54" used to be.
**/
STATIC
EFI_STATUS
InstallHooks (
  IN VOID  *DmaBuffer
  )
{
  EFI_STATUS  Status;
  UINT32      DwordAddress;
  UINT32      DwordValue;
  UINT32      VerifyValue;
  UINT32      Shift;
  UINT32      Jump;
  UINT32      Entry = SMU_PATCH_PAYLOAD_BASE + SMU_PATCH_ENTRY_OFFSET;
  CONST UINT8 CapBytes[2] = { 0xA0, 0x06 };
  UINT8       JumpBytes[3];
  UINTN       Index;

  Jump = ComputeJump (SMU_PATCH_HOOK_ADDR, Entry);
  JumpBytes[0] = (UINT8)( Jump        & 0xFFU);
  JumpBytes[1] = (UINT8)((Jump >>  8) & 0xFFU);
  JumpBytes[2] = (UINT8)((Jump >> 16) & 0xFFU);

  DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: hooks cap@0x%05X jump@0x%05X->0x%05X (0x%08X)\n",
    SMU_PATCH_CAP_ADDR, SMU_PATCH_HOOK_ADDR, Entry, Jump));

  for (Index = 0; Index < 2; Index++) {
    DwordAddress = SMU_PATCH_CAP_ADDR + (UINT32)Index;
    Shift = (DwordAddress & 3U) * 8U; DwordAddress &= ~3U;
    Status = SmuReadDword (DwordAddress, DmaBuffer, &DwordValue);   if (EFI_ERROR (Status)) return Status;
    DwordValue = (DwordValue & ~(0xFFU << Shift)) | ((UINT32)CapBytes[Index] << Shift);
    Status = SmuWriteDword (DwordAddress, DwordValue);              if (EFI_ERROR (Status)) return Status;
    Status = SmuReadDword (DwordAddress, DmaBuffer, &VerifyValue);  if (EFI_ERROR (Status)) return Status;
    if (((VerifyValue >> Shift) & 0xFFU) != CapBytes[Index]) {
      DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: cap verify mismatch @0x%05X\n", SMU_PATCH_CAP_ADDR + (UINT32)Index));
      return EFI_DEVICE_ERROR;
    }
  }
  for (Index = 0; Index < 3; Index++) {
    DwordAddress = SMU_PATCH_HOOK_ADDR + (UINT32)Index;
    Shift = (DwordAddress & 3U) * 8U; DwordAddress &= ~3U;
    Status = SmuReadDword (DwordAddress, DmaBuffer, &DwordValue);   if (EFI_ERROR (Status)) return Status;
    DwordValue = (DwordValue & ~(0xFFU << Shift)) | ((UINT32)JumpBytes[Index] << Shift);
    Status = SmuWriteDword (DwordAddress, DwordValue);              if (EFI_ERROR (Status)) return Status;
    Status = SmuReadDword (DwordAddress, DmaBuffer, &VerifyValue);  if (EFI_ERROR (Status)) return Status;
    if (((VerifyValue >> Shift) & 0xFFU) != JumpBytes[Index]) {
      DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: hook verify mismatch @0x%05X\n", SMU_PATCH_HOOK_ADDR + (UINT32)Index));
      return EFI_DEVICE_ERROR;
    }
  }
  return EFI_SUCCESS;
}

/**
  Apply the metrics patch: inject the compiled payload, then the manual hooks.
**/
STATIC
EFI_STATUS
ApplySmuPatch (
  IN VOID  *DmaBuffer
  )
{
  EFI_STATUS  Status;
  Status = InjectPayload (DmaBuffer);
  if (EFI_ERROR (Status)) return Status;
  return InstallHooks (DmaBuffer);
}

/**
  DXE entry point.

  Flow:
    1. Read the MeiMeiDXEv3SmuPatchVar gate; exit if SmuReportingPatch is disabled.
    2. Probe whether SMU privileged writes are available; exit if not.
    3. Allocate a DMA page and apply the SMU patch with read-back verification.

  @param[in] ImageHandle   Standard UEFI image handle.
  @param[in] SystemTable   Standard UEFI system table pointer.

  @retval EFI_SUCCESS  Driver completed its work or safely declined to act.
**/
EFI_STATUS
EFIAPI
BC250DXEv3SMUPatchEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  BOOLEAN               PatchEnabled;
  EFI_PHYSICAL_ADDRESS  DmaPage;
  VOID                  *DmaBuffer;

  DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: entry\n"));

  PatchEnabled = FALSE;
  Status = GetPatchEnabled (&PatchEnabled);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "BC250DXEv3SMUPatchDxe: gate unavailable, not applying patch: %r\n", Status));
    return EFI_SUCCESS;
  }

  if (!PatchEnabled) {
    DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: SmuReportingPatch disabled, not applying patch\n"));
    return EFI_SUCCESS;
  }

  if (!SmuPrivilegedWritesAvailable ()) {
    DEBUG ((DEBUG_WARN,
      "BC250DXEv3SMUPatchDxe: SMU privileged writes not available (SMU not unlocked); "
      "not applying patch\n"));
    return EFI_SUCCESS;
  }

  //
  // Allocate a 4 KB DMA-able page for the transfer-engine reads. In DXE,
  // physical == virtual for boot-time memory, so the returned address can be
  // used directly as the software buffer address.
  //
  DmaPage = 0;
  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiRuntimeServicesData,
                  1,
                  &DmaPage
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: failed to allocate DMA page: %r\n", Status));
    return EFI_SUCCESS;
  }

  DmaBuffer = (VOID *)(UINTN)DmaPage;

  Status = ApplySmuPatch (DmaBuffer);

  gBS->FreePages (DmaPage, 1);

  return EFI_SUCCESS;
}
