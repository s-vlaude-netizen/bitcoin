# Constant-inflation hard fork

This tree is a hard fork of Bitcoin. At a fixed block height the chain leaves
Bitcoin's consensus rules behind and adopts a perpetual, constant-percentage
emission together with a different proof-of-work hash function. Everything
below the fork height is unmodified Bitcoin history: the same genesis block,
the same blocks, the same UTXO set.

## What changes at the fork height

| | before the fork | from the fork on |
|---|---|---|
| block subsidy | 50 BTC, halving every 210,000 blocks | perpetual, ~1.98 % money supply growth per year |
| proof-of-work hash | double SHA-256 | double SHA-512, truncated to 256 bits |
| difficulty retarget | every 2016 blocks, ±4x | every block, moving 144-block window, ±2x |
| `MAX_MONEY` | 21,000,000 BTC | 2,100,000,000 BTC |

The **block hash** — `CBlockHeader::GetHash()`, what `hashPrevBlock`, the block
index, inventory messages and every RPC report — stays double SHA-256 at every
height. Only the hash that has to be ground below the target changes.

## Parameters

Set in `src/kernel/chainparams.cpp`, declared in `src/consensus/params.h`:

| parameter | mainnet |
|---|---|
| `nHardForkHeight` | 1,000,000 |
| `nInflationBlocksPerYear` | 52,560 (365 × 144) |
| `nInflationRateNumerator` / `Denominator` | 99 / 5000 = 1.98 % |
| `nPowForkAveragingWindow` | 144 blocks (~1 day) |

Testnet3, testnet4 and signet carry the same rules at placeholder heights
(5,000,000 / 250,000 / 350,000) that have to be pinned before any of those
networks is actually launched. On regtest the fork is inactive unless
`-inflationforkheight=<n>` is given, so the existing test suite keeps running
against the unmodified schedule.

## The emission schedule

"Constant inflation" here means a constant *percentage*, not a constant amount:
each year the money supply grows by the same fraction of itself, so the
absolute subsidy grows along with the supply. That is the difference between
this and a Monero-style fixed tail emission, whose inflation rate decays
towards zero.

The schedule is divided into emission years of `nInflationBlocksPerYear`
blocks. Every block of a year pays the same subsidy, derived from the scheduled
supply `S_n` at the start of that year:

```
subsidy_n = floor(S_n * 99 / (5000 * 52560))
S_(n+1)   = S_n + 52560 * subsidy_n
```

`S_0` is the supply the halving schedule had scheduled by the fork height,
computed in `GetScheduledSupply()` rather than hard-coded, so the same code
works for every network. On mainnet at height 1,000,000 that is exactly
**20,187,500 BTC**.

Everything is integer arithmetic on satoshis, so every implementation
reproduces the schedule bit for bit. The floor is what makes the realised
growth land just *below* the nominal 1.98 %.

Resulting numbers for mainnet:

```
First-year subsidy       : 760488013 sat = 7.60488013 BTC per block
                           1,095.10273872 BTC per day
                           399,712.49963280 BTC per year
Realised first-year rate : 1.980000 %
```

Note that this *raises* the block reward: the fork block pays 7.60 BTC where
the block before it paid 3.125 BTC. That is what 1.98 % of a 20.19 M supply
comes to; a fork that wanted to keep miner revenue flat would have to pick a
much lower rate.

`contrib/devtools/inflation_schedule.py` is the reference implementation of the
recurrence and prints the whole schedule:

```
$ ./contrib/devtools/inflation_schedule.py --years 5
 year  first height    subsidy (BTC)         supply (BTC)    growth
    0     1,000,000       7.60488013        20,187,500.00   1.9800%
    1     1,052,560       7.75545676        20,587,212.50   1.9800%
    2     1,105,120       7.90901480        20,994,839.31   1.9800%
    3     1,157,680       8.06561330        21,410,537.12   1.9800%
    4     1,210,240       8.22531244        21,834,465.76   1.9800%
```

### Why the emission stops after ~236 years

Amounts are `int64_t` satoshis, and `MAX_MONEY` has to stay small enough that
the running-sum overflow checks in `CheckTransaction()` and `CheckTxInputs()`
remain meaningful — `2 * MAX_MONEY` must not overflow. `MAX_MONEY` is therefore
raised to 2,100,000,000 BTC, which a 1.98 % compounding supply would reach 236
emission years after the fork. Emission year 235 is therefore the last one that
pays a subsidy; from year 236 on `GetBlockSubsidy()` returns 0 rather than
minting past the bound, and once stopped it never restarts.

Truly unbounded inflation would require widening the money type throughout the
codebase, which is a much larger change than this one.

Raising `MAX_MONEY` is a relaxation of an existing rule: every historical block
that satisfied the 21 M bound also satisfies the new one, so the pre-fork chain
stays valid without any height gating.

## The proof-of-work hash

`GetBlockProofOfWorkHash()` in `src/pow.cpp` returns the block hash below the
fork height and `SHA-512(SHA-512(header))[0..31]` from it on. SHA-512 shares
SHA-256's overall structure but works on 64-bit words with different constants,
a different message schedule and 80 instead of 64 rounds, so SHA-256 mining
hardware cannot compute it. No other cryptocurrency currently uses it as its
proof of work.

(The original request named "SHA-2048". No such function exists — the SHA-2
family is SHA-224/256/384/512, SHA-3 is SHA3-224/256/384/512 plus the SHAKE
XOFs, and nothing standard has a 2048-bit digest. SHA-512 is the largest
standard SHA-2 digest and is the nearest thing to what was asked for. SHA3-512
would work equally well and is a one-line change in `PoWHashWriter`.)

The digest is truncated to its leading 256 bits, the same construction NIST
uses for SHA-512/256. This keeps the result comparable against the 256-bit
compact target in `nBits`, so nothing in the difficulty machinery has to grow a
wider integer type, and it does not reduce preimage resistance below 256 bits.

### How the height gets into the check

The hash function depends on the height, but two code paths see a header
before it is connected to the chain: the header spam filter in
`net_processing` and re-reading a block from disk. Both use
`CheckProofOfWorkAnyAlgo()`, which accepts a header valid under *either*
function. That is all a denial-of-service pre-filter needs — the work that has
to be spent is the same either way — and the authoritative, height-aware
`CheckProofOfWork(block, nHeight, params)` runs in `AcceptBlockHeader()`, right
after `pindexPrev` has been looked up. Every block reaches it: a block is only
ever accepted once its header has passed through there.

## Difficulty after the fork

Changing the hash function destroys the meaning of the accumulated difficulty
in one block: at the fork height there is no SHA-512 hardware and the honest
hash rate drops by many orders of magnitude. Two things handle that:

1. The block at `nHardForkHeight` resets `nBits` to `powLimit`.
2. From `nHardForkHeight + 1` on, `CalculateNextWorkRequiredFork()` retargets on
   every block. It takes the chain work produced by the last 144 blocks and the
   time they took (clamped to between half and twice the expected 24 hours) and
   inverts it into a target — the same shape as Bitcoin Cash's `cw-144`. Until
   a full window of post-fork blocks exists, difficulty stays at `powLimit`.

Because the difficulty can now change on every block, the pre-sync heuristic
`PermittedDifficultyTransition()` cannot bound a transition from a single pair
of `nBits` values and returns true for post-fork heights. Header spam is still
bounded by `nMinimumChainWork`, and the exact value is re-derived in
`ContextualCheckBlockHeader()`.

## Network separation

Mainnet gets its own message-start magic (`c91f024e`, versus Bitcoin's
`f9beb4d9`) and default P2P port (8433, versus 8333), so nodes on the two
chains do not waste connection slots on each other and get each other banned
after the split.

## Deliberately not done

These belong to a launch, not to the consensus change, and are listed so they
are not mistaken for oversights:

- **Transaction replay protection.** Bitcoin Cash added a `SIGHASH_FORKID`
  variant so a transaction signed for one chain is invalid on the other. This
  fork has none: a transaction with pre-fork inputs is valid on both chains
  until the two UTXO sets diverge. Adding it touches the script interpreter,
  signing, the wallet and the RPC surface, and is a separate change.
- **Address format.** Base58 prefixes and the `bc` bech32 HRP are unchanged, so
  addresses look identical on both chains. A distinct HRP would make the
  divergence visible to users.
- **BIP44 coin type**, DNS seeds, fixed seeds and the checkpoint/assumevalid
  data still point at Bitcoin's.

## Testing

Unit tests:

```
build/bin/test_bitcoin --run_test=validation_tests/inflation_subsidy_test
build/bin/test_bitcoin --run_test=pow_tests/fork_pow_hash
build/bin/test_bitcoin --run_test=pow_tests/fork_pow_check_by_height
build/bin/test_bitcoin --run_test=pow_tests/fork_difficulty_adjustment
```

End-to-end on regtest, which activates the fork at a low height and mines
across it:

```
build/test/functional/feature_inflation_fork.py
```

The schedule the C++ asserts against is produced independently by
`contrib/devtools/inflation_schedule.py`.
