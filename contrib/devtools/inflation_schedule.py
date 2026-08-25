#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Derive and verify the constant-inflation emission schedule of the hard fork.

The fork replaces Bitcoin's halving schedule with a perpetual emission that
grows the *money supply* by a constant, slightly-below-2% rate per year.

The schedule is defined in yearly epochs of BLOCKS_PER_YEAR blocks.  For epoch
n, every block pays the same subsidy, derived from the scheduled supply S_n at
the start of that epoch:

    subsidy_n = floor(S_n * RATE_NUM / (RATE_DEN * BLOCKS_PER_YEAR))
    S_{n+1}   = S_n + BLOCKS_PER_YEAR * subsidy_n

All arithmetic is integer arithmetic on satoshis, so the schedule is exactly
reproducible by every consensus implementation.  Because of the flooring, the
realised growth rate is always a hair *below* the nominal RATE_NUM/RATE_DEN.

This script is the reference for src/validation.cpp:GetBlockSubsidy() and for
the values asserted in src/test/inflation_tests.cpp.

Usage:
    ./inflation_schedule.py            # summary
    ./inflation_schedule.py --years 30 # per-year table
"""

import argparse

COIN = 100_000_000

# --- Legacy Bitcoin parameters (pre-fork) -----------------------------------
HALVING_INTERVAL = 210_000
INITIAL_SUBSIDY = 50 * COIN

# --- Fork parameters (must match src/kernel/chainparams.cpp) ----------------
FORK_HEIGHT = 1_000_000
BLOCKS_PER_YEAR = 52_560  # 365 days * 144 blocks/day at 10 minute spacing
RATE_NUM = 99             # nominal annual money supply growth: 99/5000
RATE_DEN = 5_000          #                                  = 1.98 %
MAX_MONEY = 2_100_000_000 * COIN


def legacy_subsidy(height: int) -> int:
    """Bitcoin's original subsidy at `height`, in satoshis."""
    halvings = height // HALVING_INTERVAL
    if halvings >= 64:
        return 0
    return INITIAL_SUBSIDY >> halvings


def legacy_supply(height: int) -> int:
    """Scheduled supply in satoshis after `height` blocks (heights 0..height-1)."""
    supply = 0
    h = 0
    while h < height:
        # Blocks [h, era_end) all pay the same subsidy.
        era_end = min(height, (h // HALVING_INTERVAL + 1) * HALVING_INTERVAL)
        supply += legacy_subsidy(h) * (era_end - h)
        h = era_end
    return supply


def per_block_subsidy(supply: int) -> int:
    """subsidy = floor(supply * RATE_NUM / (RATE_DEN * BLOCKS_PER_YEAR)).

    Written the way the C++ implementation writes it: split into quotient and
    remainder so the intermediate product can never overflow an int64.
    """
    den = RATE_DEN * BLOCKS_PER_YEAR
    return (supply // den) * RATE_NUM + ((supply % den) * RATE_NUM) // den


def schedule(max_epochs: int):
    """Yield (epoch, first_height, subsidy, supply_at_epoch_start, realised_rate)."""
    supply = legacy_supply(FORK_HEIGHT)
    for epoch in range(max_epochs):
        subsidy = per_block_subsidy(supply)
        emitted = subsidy * BLOCKS_PER_YEAR
        # Never mint past MAX_MONEY; the 64-bit money type has to hold the total.
        if supply + emitted > MAX_MONEY:
            subsidy = 0
            emitted = 0
        rate = emitted / supply if supply else 0.0
        yield epoch, FORK_HEIGHT + epoch * BLOCKS_PER_YEAR, subsidy, supply, rate
        supply += emitted


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--years", type=int, default=0,
                        help="print a per-year table for this many years")
    args = parser.parse_args()

    s0 = legacy_supply(FORK_HEIGHT)
    first = per_block_subsidy(s0)

    print(f"Fork height              : {FORK_HEIGHT:,}")
    print(f"Blocks per year          : {BLOCKS_PER_YEAR:,} (10 min spacing)")
    print(f"Nominal annual growth    : {RATE_NUM}/{RATE_DEN} = {100 * RATE_NUM / RATE_DEN:.4f} %")
    print(f"Scheduled supply at fork : {s0 / COIN:,.8f} BTC")
    print(f"Legacy subsidy at fork   : {legacy_subsidy(FORK_HEIGHT) / COIN:,.8f} BTC/block")
    print()
    print(f"First-year subsidy       : {first} sat = {first / COIN:.8f} BTC per block")
    print(f"                           {first * 144 / COIN:,.8f} BTC per day")
    print(f"                           {first * BLOCKS_PER_YEAR / COIN:,.8f} BTC per year")
    print(f"Realised first-year rate : {100 * first * BLOCKS_PER_YEAR / s0:.6f} %")
    print()

    # Where does the schedule end?  It stops once MAX_MONEY would be exceeded.
    last_paying, terminal_supply = None, s0
    for epoch, _height, subsidy, supply, _rate in schedule(10_000):
        if subsidy == 0:
            terminal_supply = supply
            break
        last_paying = epoch
    print(f"Last paying epoch        : year {last_paying} after the fork")
    print(f"Terminal supply          : {terminal_supply / COIN:,.2f} BTC "
          f"(MAX_MONEY = {MAX_MONEY / COIN:,.0f} BTC)")

    if args.years:
        print()
        print(f"{'year':>5} {'first height':>13} {'subsidy (BTC)':>16} "
              f"{'supply (BTC)':>20} {'growth':>9}")
        for epoch, height, subsidy, supply, rate in schedule(args.years):
            print(f"{epoch:>5} {height:>13,} {subsidy / COIN:>16.8f} "
                  f"{supply / COIN:>20,.2f} {100 * rate:>8.4f}%")


if __name__ == "__main__":
    main()
