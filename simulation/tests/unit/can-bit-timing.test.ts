/**
 * can-bit-timing.test.ts — Validate MCP2515 CNF register timing for 500 kbit/s.
 *
 * The MCP2515 CAN controller at rt-esp32/src/can_driver_mcp2515.cpp uses
 * CNF registers to configure the CAN bit rate. This test verifies that
 * the register values produce exactly 500 kbit/s at the specified crystal
 * frequency, and that total time quanta >= 8 (CAN minimum).
 *
 * Catches: Bug #2 — CNF3=0x01 giving PS2=1 TQ instead of 2 TQ (7 TQ total)
 *          at the PHSEG2 bit position (CNF3 bits 5-3, not bits 2-0).
 */

import { describe, it, expect } from 'vitest';

// ── CNF register decode helpers (mirrors MCP2515 datasheet §12.0) ──────

/** TQ period in nanoseconds */
function tqNs(brp: number, foscMhz: number): number {
  return (2 * (brp + 1)) / foscMhz * 1000;
}

/** Decode total time quanta from CNF register values */
function bitTq(cnf1: number, cnf2: number, cnf3: number): {
  brp: number;
  propSeg: number;   // Propagation Segment (TQ)
  ps1: number;       // Phase Segment 1 (TQ)
  ps2: number;       // Phase Segment 2 (TQ)
  sjw: number;       // Synchronization Jump Width (TQ)
  total: number;     // Total TQ
  samplePoint: number; // Sample point as fraction [0, 1]
} {
  const brp    = cnf1 & 0x3F;
  const sjw    = ((cnf1 >> 6) & 0x03) + 1;
  const propSeg = (cnf2 & 0x07) + 1;
  const ps1    = ((cnf2 >> 3) & 0x07) + 1;
  // KEY: PHSEG2 is at CNF3 bits 5-3, NOT bits 2-0
  const ps2    = ((cnf3 >> 3) & 0x07) + 1;
  const total  = 1 + propSeg + ps1 + ps2; // SyncSeg(1) + PropSeg + PS1 + PS2
  const samplePoint = (1 + propSeg + ps1) / total;
  return { brp, propSeg, ps1, ps2, sjw, total, samplePoint };
}

/** Computed bit rate in bit/s */
function bitRate(cnf1: number, cnf2: number, cnf3: number, foscMhz: number): number {
  const { brp, total } = bitTq(cnf1, cnf2, cnf3);
  const tq = tqNs(brp, foscMhz); // nanoseconds
  return 1e9 / (total * tq);
}

// ── Constants from the driver source ──────────────────────────────────
// rt-esp32/src/can_driver_mcp2515.cpp lines 22-24
const CNF1_500k = 0x00;  // SJW=1, BRP=0
const CNF2_500k = 0x91;  // BTLMODE=1, PHSEG1=010→3TQ, PRSEG=001→2TQ
const CNF3_500k = 0x08;  // PHSEG2=001→2TQ (was 0x01 — BUG)

const CNF3_OLD_BUGGY = 0x01; // The old value that gave PS2=1 TQ

describe('MCP2515 CNF bit timing', () => {
  describe('register field decode', () => {
    it('decodes CNF1 fields correctly', () => {
      const { brp, sjw } = bitTq(CNF1_500k, CNF2_500k, CNF3_500k);
      expect(brp).toBe(0);
      expect(sjw).toBe(1);
    });

    it('decodes CNF2 fields correctly', () => {
      const { propSeg, ps1 } = bitTq(CNF1_500k, CNF2_500k, CNF3_500k);
      expect(propSeg).toBe(2);
      expect(ps1).toBe(3);
    });

    it('decodes CNF3 PHSEG2 at bits 5-3 (not bits 2-0)', () => {
      const { ps2 } = bitTq(CNF1_500k, CNF2_500k, CNF3_500k);
      expect(ps2).toBe(2); // PHSEG2=1 → 1+1=2 TQ
    });

    it('detects old buggy CNF3=0x01 giving PS2=1 TQ', () => {
      const { ps2 } = bitTq(CNF1_500k, CNF2_500k, CNF3_OLD_BUGGY);
      expect(ps2).toBe(1); // PHSEG2=0 → 0+1=1 TQ — BUG
    });
  });

  describe('total time quanta', () => {
    it('has exactly 8 TQ with corrected CNF3', () => {
      const { total } = bitTq(CNF1_500k, CNF2_500k, CNF3_500k);
      expect(total).toBe(8);
    });

    it('has only 7 TQ with old buggy CNF3', () => {
      const { total } = bitTq(CNF1_500k, CNF2_500k, CNF3_OLD_BUGGY);
      expect(total).toBe(7);
    });

    it('meets CAN minimum of 8 TQ', () => {
      const { total } = bitTq(CNF1_500k, CNF2_500k, CNF3_500k);
      expect(total).toBeGreaterThanOrEqual(8);
    });
  });

  describe('bit rate at 8 MHz crystal', () => {
    const FOSC = 8.0;

    it('computes 500 kbit/s within 1% tolerance', () => {
      const rate = bitRate(CNF1_500k, CNF2_500k, CNF3_500k, FOSC);
      expect(rate).toBeGreaterThan(495_000);
      expect(rate).toBeLessThan(505_000);
    });

    it('computes exactly 500,000 bit/s', () => {
      const rate = bitRate(CNF1_500k, CNF2_500k, CNF3_500k, FOSC);
      expect(rate).toBe(500_000);
    });

    it('old buggy CNF3 gives ~571 kbit/s (14% too fast)', () => {
      const rate = bitRate(CNF1_500k, CNF2_500k, CNF3_OLD_BUGGY, FOSC);
      expect(rate).toBeCloseTo(571_428, -3); // ~571.4 kbit/s
    });

    it('sample point is at 75% (6/8)', () => {
      const { samplePoint } = bitTq(CNF1_500k, CNF2_500k, CNF3_500k);
      expect(samplePoint).toBeCloseTo(0.75, 2);
    });
  });

  describe('bit rate at 16 MHz crystal (common on Chinese modules)', () => {
    const FOSC = 16.0;

    it('with BRP=0 gives 1 Mbit/s — TOO FAST for 500k bus', () => {
      const rate = bitRate(CNF1_500k, CNF2_500k, CNF3_500k, FOSC);
      expect(rate).toBe(1_000_000);
    });

    it('16 MHz requires BRP=1 to get 500 kbit/s', () => {
      // BRP=1: TQ = 2*2/16MHz = 0.25 µs, same as BRP=0 @ 8 MHz
      const cnf1_16mhz = 0x01; // BRP=1
      const { brp } = bitTq(cnf1_16mhz, CNF2_500k, CNF3_500k);
      expect(brp).toBe(1);

      const tq = tqNs(brp, FOSC);
      expect(tq).toBeCloseTo(250, 0);  // 250 ns = 0.25 µs

      const rate = bitRate(cnf1_16mhz, CNF2_500k, CNF3_500k, FOSC);
      expect(rate).toBe(500_000);
    });
  });

  describe('TQ computation', () => {
    it('TQ = 250 ns (0.25 µs) at 8 MHz with BRP=0', () => {
      expect(tqNs(0, 8.0)).toBeCloseTo(250, 0);
    });

    it('TQ = 250 ns (0.25 µs) at 16 MHz with BRP=1', () => {
      expect(tqNs(1, 16.0)).toBeCloseTo(250, 0);
    });

    it('TQ = 125 ns (0.125 µs) at 16 MHz with BRP=0', () => {
      expect(tqNs(0, 16.0)).toBeCloseTo(125, 0);
    });
  });
});
