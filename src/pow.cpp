// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <hash.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

#include <algorithm>
#include <limits>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    const int nHeight{pindexLast->nHeight + 1};
    if (params.IsHardForkActive(nHeight)) {
        // The proof-of-work hash function changes at the fork height, so all
        // difficulty accumulated by SHA-256 hardware becomes meaningless in one
        // block. Reset to the minimum difficulty and let the per-block
        // adjustment in CalculateNextWorkRequiredFork() find the new
        // equilibrium.
        if (nHeight == params.nHardForkHeight) return nProofOfWorkLimit;
        return CalculateNextWorkRequiredFork(pindexLast, params);
    }

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then it MUST be a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int CalculateNextWorkRequiredFork(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const arith_uint256 bnPowLimit{UintToArith256(params.powLimit)};

    if (params.fPowNoRetargeting) return pindexLast->nBits;

    // Until a full window of post-fork blocks exists, stay at the minimum
    // difficulty the fork block reset to. Nothing better can be measured yet,
    // and the point of the reset is to let the new hash function's (initially
    // tiny) hash rate produce blocks at all.
    const int64_t window{params.nPowForkAveragingWindow};
    if (pindexLast->nHeight < params.nHardForkHeight + window) {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex* pindexFirst{pindexLast->GetAncestor(pindexLast->nHeight - window)};
    assert(pindexFirst);

    // Work that the `window` blocks in (pindexFirst, pindexLast] actually
    // produced, scaled to one target spacing.
    arith_uint256 work{pindexLast->nChainWork - pindexFirst->nChainWork};
    work *= params.nPowTargetSpacing;

    // Time they took, clamped to [1/2, 2] of the expected timespan so that a
    // single manipulated timestamp cannot move the difficulty arbitrarily far.
    const int64_t expected_timespan{window * params.nPowTargetSpacing};
    const int64_t actual_timespan{std::clamp<int64_t>(
        pindexLast->GetBlockTime() - pindexFirst->GetBlockTime(),
        expected_timespan / 2, expected_timespan * 2)};
    work /= actual_timespan;

    // `work` is now the work one block is expected to cost at the observed hash
    // rate. Invert it back into a target: target = 2^256 / work - 1, which is
    // the inverse of GetBlockProof().
    if (work == 0) return bnPowLimit.GetCompact();
    const arith_uint256 bnNew{(-work) / work};

    if (bnNew > bnPowLimit) return bnPowLimit.GetCompact();
    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    // From the fork height on, the difficulty retargets on every block from a
    // moving window of block times and chain work. That cannot be bounded from
    // a single pair of nBits values, so this pre-sync heuristic has nothing to
    // say about post-fork heights. Header spam is still bounded by
    // nMinimumChainWork, and GetNextWorkRequired() re-derives the exact value
    // in ContextualCheckBlockHeader().
    if (params.IsHardForkActive(height)) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

uint256 GetBlockProofOfWorkHash(const CBlockHeader& block, int nHeight, const Consensus::Params& params)
{
    if (!params.IsHardForkActive(nHeight)) return block.GetHash();
    return (PoWHashWriter{} << block).GetHash();
}

bool CheckProofOfWork(const CBlockHeader& block, int nHeight, const Consensus::Params& params)
{
    return CheckProofOfWork(GetBlockProofOfWorkHash(block, nHeight, params), block.nBits, params);
}

bool CheckProofOfWorkAnyAlgo(const CBlockHeader& block, const Consensus::Params& params)
{
    if (CheckProofOfWork(block.GetHash(), block.nBits, params)) return true;
    // A header is only ever mined under one of the two hash functions, but here
    // we do not know yet which height it will claim, so try the other one too.
    if (params.nHardForkHeight == std::numeric_limits<int>::max()) return false;
    return CheckProofOfWork((PoWHashWriter{} << block).GetHash(), block.nBits, params);
}
