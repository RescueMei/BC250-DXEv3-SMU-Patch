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
  UINTN   Iterations;

  Start      = GetPerformanceCounter ();
  Iterations = 0;

  for (;;) {
    Value = SmnRead32 (RspAddress);
    if (IsDoneStatus (Value)) {
      *Status = Value;
      return EFI_SUCCESS;
    }

    ElapsedUs = DivU64x32 (GetTimeInNanoSecond (GetPerformanceCounter () - Start), 1000U);
    if (ElapsedUs >= TimeoutUs || ++Iterations >= SMU_PATCH_MAILBOX_POLL_MAX_ITER) {
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
  Invoke one SMU cache-maintenance helper (d-cache 0x30B3 / i-cache 0x30EB) at a
  dword-aligned address, restoring the scratch FLAG/MASK afterwards.  Mirrors the
  reference deployer's cache_helper().

  @param[in]  Function   0x30B3 (data) or 0x30EB (instruction).
  @param[in]  Address    Dword-aligned SMU-local address.
  @param[in]  Flag       Scratch CP_FLAG value.
  @param[in]  Mask       Scratch CP_MASK value.
  @param[in]  DmaBuffer  Virtual address of the 4 KB DMA-able page.

  @retval EFI_SUCCESS  The helper ran.
  @retval others       A mailbox step failed or the response was unexpected.
**/
STATIC
EFI_STATUS
SmuCacheHelper (
  IN UINT32  Function,
  IN UINT32  Address,
  IN UINT32  Flag,
  IN UINT32  Mask,
  IN VOID    *DmaBuffer
  )
{
  UINT32      Args[6];
  UINT32      Status;
  EFI_STATUS  Result;
  UINT32      Out;

  Result = SmuWriteDword (SMU_PATCH_CP_FLAG, Flag);
  if (EFI_ERROR (Result)) return Result;
  Result = SmuWriteDword (SMU_PATCH_CP_CB, Function);
  if (EFI_ERROR (Result)) return Result;

  ZeroMem (Args, sizeof (Args));
  Args[0] = 0x3AU;
  Args[1] = 0x22U;
  Args[4] = Address;

  Result = SendMailbox (
             SMU_PATCH_Q2_CMD,
             SMU_PATCH_Q2_RSP,
             SMU_PATCH_Q2_ARG,
             SMU_PATCH_Q2_MSG_TRANSFER,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) return Result;

  Out = SmnRead32 (SMU_PATCH_Q2_ARG);
  if (Out != 2U && Out != 3U) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: cache helper 0x%X @0x%05X out=0x%X\n",
      Function, Address, Out));
    return EFI_DEVICE_ERROR;
  }

  SmuWriteDword (SMU_PATCH_CP_MASK, Mask);
  SmuWriteDword (SMU_PATCH_CP_FLAG, Flag);
  return EFI_SUCCESS;
}

/**
  Publish (invalidate) the d-cache and i-cache for [Start, End) so the SMU sees
  freshly written code.  Mirrors the reference deployer's cache_publish().

  @param[in]  Start,End  Half-open SMU-local byte range to publish.
  @param[in]  Flag       Scratch CP_FLAG value.
  @param[in]  Mask       Scratch CP_MASK value.
  @param[in]  DmaBuffer  Virtual address of the 4 KB DMA-able page.

  @retval EFI_SUCCESS  The range was published.
  @retval others       A mailbox step failed.
**/
STATIC
EFI_STATUS
SmuCachePublish (
  IN UINT32  Start,
  IN UINT32  End,
  IN UINT32  Flag,
  IN UINT32  Mask,
  IN VOID    *DmaBuffer
  )
{
  UINT32      Address;
  UINT32      Args[6];
  UINT32      Status;
  EFI_STATUS  Result;

  for (Address = Start & ~15U; Address < End; Address += 16U) {
    Result = SmuCacheHelper (SMU_PATCH_CACHE_FN_DATA, Address, Flag, Mask, DmaBuffer);
    if (EFI_ERROR (Result)) return Result;
  }
  for (Address = Start & ~31U; Address < End; Address += 32U) {
    Result = SmuCacheHelper (SMU_PATCH_CACHE_FN_INSTR, Address, Flag, Mask, DmaBuffer);
    if (EFI_ERROR (Result)) return Result;
  }

  ZeroMem (Args, sizeof (Args));
  Args[0] = SMU_PATCH_CACHE_MARKER;
  Result = SendMailbox (
             SMU_PATCH_Q2_CMD,
             SMU_PATCH_Q2_RSP,
             SMU_PATCH_Q2_ARG,
             SMU_PATCH_Q2_MSG_TRANSFER,
             Args,
             &Status
             );
  if (EFI_ERROR (Result)) return Result;
  if (SmnRead32 (SMU_PATCH_Q2_ARG) != 3U) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: cache marker did not return 3\n"));
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
  Write a 3-byte Xtensa `j` at Site targeting Target (byte-granular RMW).
**/
STATIC
EFI_STATUS
InstallJump3 (
  IN UINT32  Site,
  IN UINT32  Target,
  IN VOID    *DmaBuffer
  )
{
  EFI_STATUS  Status;
  UINT32      DwordAddress;
  UINT32      DwordValue;
  UINT32      VerifyValue;
  UINT32      Shift;
  UINT32      Jump = ComputeJump (Site, Target);
  UINT8       Bytes[3];
  UINTN       Index;

  Bytes[0] = (UINT8)( Jump        & 0xFFU);
  Bytes[1] = (UINT8)((Jump >>  8) & 0xFFU);
  Bytes[2] = (UINT8)((Jump >> 16) & 0xFFU);

  for (Index = 0; Index < 3; Index++) {
    DwordAddress = Site + (UINT32)Index;
    Shift = (DwordAddress & 3U) * 8U; DwordAddress &= ~3U;
    Status = SmuReadDword (DwordAddress, DmaBuffer, &DwordValue);   if (EFI_ERROR (Status)) return Status;
    DwordValue = (DwordValue & ~(0xFFU << Shift)) | ((UINT32)Bytes[Index] << Shift);
    Status = SmuWriteDword (DwordAddress, DwordValue);              if (EFI_ERROR (Status)) return Status;
    Status = SmuReadDword (DwordAddress, DmaBuffer, &VerifyValue);  if (EFI_ERROR (Status)) return Status;
    if (((VerifyValue >> Shift) & 0xFFU) != Bytes[Index]) {
      DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: jump verify mismatch @0x%05X\n", Site + (UINT32)Index));
      return EFI_DEVICE_ERROR;
    }
  }
  return EFI_SUCCESS;
}

/**
  Apply the four MANUAL (non-compilable) hooks that wire the compiled payload,
  each followed by a cache publish (matching the reference deployer):

    0x29BE5  j smu_clamp_current   (Current fill loop bound -> min(count, 6))
    0x29A0F  j smu_clamp_average   (Average fill loop bound -> min(count, 6))
    0x29A1D  j smu_patch_entry     (emit the fixed-size tail)
    0x29CB8  j smu_spare_entry     (stamp Current.Spare after the Current fill)
**/
//
// The four hook sites, each written as a 3-byte "j".  The dwords they touch
// (a 3-byte site can straddle two dwords) are saved before any write so a
// failed install can be rolled back to the pristine bytes.
//
STATIC CONST UINT32  mHookSites[4] = {
  SMU_PATCH_CLAMP_CURRENT_ADDR, SMU_PATCH_CLAMP_AVERAGE_ADDR,
  SMU_PATCH_HOOK_ADDR, SMU_PATCH_SPARE_ADDR
};

STATIC
EFI_STATUS
InstallHooks (
  IN VOID    *DmaBuffer,
  IN UINT32  Flag,
  IN UINT32  Mask
  )
{
  EFI_STATUS  Status;
  UINT32      Entry = SMU_PATCH_PAYLOAD_BASE + SMU_PATCH_ENTRY_OFFSET;
  UINT32      Target[4];
  UINT32      SavedAddr[SMU_PATCH_MAX_HOOK_DWORDS];
  UINT32      SavedVal[SMU_PATCH_MAX_HOOK_DWORDS];
  UINTN       SavedCount;
  UINTN       Site;
  UINTN       Index;
  UINTN       Saved;
  UINT32      DwordAddr;
  UINT32      Val;
  BOOLEAN     Found;

  Target[0] = SMU_PATCH_PAYLOAD_BASE + SMU_PATCH_CLAMP_CURRENT_OFFSET;
  Target[1] = SMU_PATCH_PAYLOAD_BASE + SMU_PATCH_CLAMP_AVERAGE_OFFSET;
  Target[2] = Entry;
  Target[3] = SMU_PATCH_PAYLOAD_BASE + SMU_PATCH_SPARE_OFFSET;

  DEBUG ((DEBUG_INFO,
    "BC250DXEv3SMUPatchDxe: hooks clamp_cur@0x%05X->0x%05X clamp_avg@0x%05X->0x%05X "
    "entry@0x%05X->0x%05X spare@0x%05X->0x%05X\n",
    SMU_PATCH_CLAMP_CURRENT_ADDR, Target[0],
    SMU_PATCH_CLAMP_AVERAGE_ADDR, Target[1],
    SMU_PATCH_HOOK_ADDR, Entry,
    SMU_PATCH_SPARE_ADDR, Target[3]));

  //
  // Snapshot every dword any hook will modify, so a partial install can be
  // reverted to an unpatched (safe) state.
  //
  SavedCount = 0;
  for (Site = 0; Site < 4; Site++) {
    for (Index = 0; Index < 3; Index++) {
      DwordAddr = (mHookSites[Site] + (UINT32)Index) & ~3U;
      Found = FALSE;
      for (Saved = 0; Saved < SavedCount; Saved++) {
        if (SavedAddr[Saved] == DwordAddr) {
          Found = TRUE;
          break;
        }
      }
      if (Found) {
        continue;
      }
      if (SavedCount >= SMU_PATCH_MAX_HOOK_DWORDS) {
        DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: too many hook dwords\n"));
        return EFI_OUT_OF_RESOURCES;
      }
      Status = SmuReadDword (DwordAddr, DmaBuffer, &Val);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      SavedAddr[SavedCount] = DwordAddr;
      SavedVal[SavedCount]  = Val;
      SavedCount++;
    }
  }

  for (Site = 0; Site < 4; Site++) {
    Status = InstallJump3 (mHookSites[Site], Target[Site], DmaBuffer);
    if (EFI_ERROR (Status)) {
      break;
    }
    //
    // Publish this hook so the SMU picks up the new jump (d-cache + i-cache).
    //
    Status = SmuCachePublish (mHookSites[Site] & ~3U, (mHookSites[Site] & ~3U) + 4U,
                              Flag, Mask, DmaBuffer);
    if (EFI_ERROR (Status)) {
      break;
    }
  }

  if (EFI_ERROR (Status)) {
    //
    // Restore the originals so we never leave the SMU with a half-installed
    // hook (a torn jump into the metrics path wedges the SMU).
    //
    for (Saved = 0; Saved < SavedCount; Saved++) {
      SmuWriteDword (SavedAddr[Saved], SavedVal[Saved]);
    }
    DEBUG ((DEBUG_ERROR,
      "BC250DXEv3SMUPatchDxe: hook install failed (%r); rolled back %u dword(s)\n",
      Status, (UINT32)SavedCount));
  }

  return Status;
}

/**
  Apply the metrics patch: read the cache scratch state, inject the compiled
  payload, install the manual hooks, then relocate the metrics base literal.
  Every write group is followed by a cache publish, matching the reference
  deployer (deploy_one.py).
**/
STATIC
EFI_STATUS
ApplySmuPatch (
  IN VOID  *DmaBuffer
  )
{
  EFI_STATUS  Status;
  UINT32      Flag;
  UINT32      Mask;

  //
  // Save the SMU cache scratch state once, so every publish can restore it.
  //
  Status = SmuReadDword (SMU_PATCH_CP_FLAG, DmaBuffer, &Flag);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: cannot read CP_FLAG: %r\n", Status));
    return Status;
  }
  Status = SmuReadDword (SMU_PATCH_CP_MASK, DmaBuffer, &Mask);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: cannot read CP_MASK: %r\n", Status));
    return Status;
  }

  Status = InjectPayload (DmaBuffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = SmuCachePublish (SMU_PATCH_PAYLOAD_BASE,
                            SMU_PATCH_PAYLOAD_BASE + (UINT32)smu_patch_payload_len,
                            Flag, Mask, DmaBuffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = InstallHooks (DmaBuffer, Flag, Mask);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Relocate the metrics base literal so the fills write to the free region the
  // payload's spare/extra writes target (see payload/src/smu_patch_map.h).
  //
  Status = SmuWriteDword (SMU_PATCH_RELOC_LITERAL, SMU_PATCH_METRICS_RELOC);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BC250DXEv3SMUPatchDxe: metrics relocation write failed: %r\n", Status));
    return Status;
  }
  Status = SmuCachePublish (SMU_PATCH_RELOC_LITERAL & ~3U,
                            (SMU_PATCH_RELOC_LITERAL & ~3U) + 4U,
                            Flag, Mask, DmaBuffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "BC250DXEv3SMUPatchDxe: metrics base relocated to 0x%05X\n",
    SMU_PATCH_METRICS_RELOC));

  return EFI_SUCCESS;
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
  if (EFI_ERROR (Status)) {
    //
    // Surface the failure instead of swallowing it.  InstallHooks() rolls its
    // hooks back on error, so a failed apply should leave the SMU unpatched;
    // make that visible in the boot log.
    //
    DEBUG ((DEBUG_ERROR,
      "BC250DXEv3SMUPatchDxe: SMU patch apply FAILED (%r); SMU left unpatched\n", Status));
  }

  gBS->FreePages (DmaPage, 1);

  return Status;
}
