/*
 * Shared SMU-SRAM access macro for the BC-250 payloads.
 *
 * On target, GOV_SRAM_BASE is 0 and REG() is a plain absolute SMU-SRAM access.
 * For an off-board build (GOV_HOST_TEST=1 + a host compiler) a test harness
 * provides `gov_mock_sram` and the same sources run against it.
 */
#ifndef BC250_SRAM_H
#define BC250_SRAM_H

#ifndef GOV_DRY_RUN
#define GOV_DRY_RUN 0
#endif
#ifndef GOV_HOST_TEST
#define GOV_HOST_TEST 0
#endif

#if GOV_HOST_TEST
extern unsigned char gov_mock_sram[];
#define GOV_SRAM_BASE ((unsigned long)gov_mock_sram)
#else
#define GOV_SRAM_BASE 0UL
#endif

#define REG(t, a) (*(volatile t *)(GOV_SRAM_BASE + (unsigned long)(a)))

#endif /* BC250_SRAM_H */
