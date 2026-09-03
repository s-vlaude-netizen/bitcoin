// Copyright (c) 2015-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <crypto/sha512.h>
#include <pow.h>
#include <primitives/block.h>
#include <span.h>
#include <streams.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;

    // Here (and below): expected_nbits is calculated in
    // CalculateNextWorkRequired(); redoing the calculation here would be just
    // reimplementing the same code that is written in pow.cpp. Rather than
    // copy that code, we just hardcode the expected result.
    unsigned int expected_nbits = 0x1d00d86aU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    unsigned int expected_nbits = 0x1d00ffffU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    unsigned int expected_nbits = 0x1c0168fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that reducing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits-1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    unsigned int expected_nbits = 0x1d00e1fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that increasing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits+1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg);
    BOOST_CHECK(pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max{UintToArith256(uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"})};
        targ_max /= consensus.nPowTargetTimespan*4;
        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
    }
}

/* The constant-inflation hard fork swaps the proof-of-work hash function at
 * nHardForkHeight: double SHA-256 below it, truncated double SHA-512 from it
 * on. The block hash itself stays double SHA-256 at every height. */
BOOST_AUTO_TEST_CASE(fork_pow_hash)
{
    const auto chainParams{CreateChainParams(*m_node.args, ChainType::MAIN)};
    const auto& consensus{chainParams->GetConsensus()};
    const CBlockHeader header{chainParams->GenesisBlock()};

    BOOST_CHECK_EQUAL(GetBlockProofOfWorkHash(header, 0, consensus), header.GetHash());
    BOOST_CHECK_EQUAL(GetBlockProofOfWorkHash(header, consensus.nHardForkHeight - 1, consensus), header.GetHash());

    const uint256 fork_hash{GetBlockProofOfWorkHash(header, consensus.nHardForkHeight, consensus)};
    BOOST_CHECK(fork_hash != header.GetHash());

    // Recompute it independently of PoWHashWriter: SHA-512(SHA-512(header)),
    // keeping the leading 256 bits.
    DataStream ss;
    ss << header;
    BOOST_CHECK_EQUAL(ss.size(), 80U);
    unsigned char buf[CSHA512::OUTPUT_SIZE];
    CSHA512().Write(UCharCast(ss.data()), ss.size()).Finalize(buf);
    CSHA512().Write(buf, CSHA512::OUTPUT_SIZE).Finalize(buf);
    BOOST_CHECK_EQUAL(fork_hash, uint256(std::span<const unsigned char>{buf, uint256::size()}));
}

/* Which hash function a header has to satisfy is decided by its height, while
 * the height-free pre-filter accepts either. */
BOOST_AUTO_TEST_CASE(fork_pow_check_by_height)
{
    Consensus::Params consensus{CreateChainParams(*m_node.args, ChainType::REGTEST)->GetConsensus()};
    consensus.nHardForkHeight = 100;

    CBlockHeader header;
    header.nVersion = 1;
    header.nTime = 1231006505;
    // regtest's pow limit is ~2^255, so roughly half of all nonces solve either
    // hash function and the loops below terminate after a handful of tries.
    header.nBits = UintToArith256(consensus.powLimit).GetCompact();

    const auto solves = [&](const CBlockHeader& h, int height) {
        return CheckProofOfWork(GetBlockProofOfWorkHash(h, height, consensus), h.nBits, consensus);
    };

    // A header that solves only the legacy hash is valid below the fork height
    // and invalid from it on.
    CBlockHeader legacy_only{header};
    while (!solves(legacy_only, 0) || solves(legacy_only, 100)) ++legacy_only.nNonce;
    BOOST_CHECK(CheckProofOfWork(legacy_only, 99, consensus));
    BOOST_CHECK(!CheckProofOfWork(legacy_only, 100, consensus));
    BOOST_CHECK(CheckProofOfWorkAnyAlgo(legacy_only, consensus));

    // ... and the other way round for a header that solves only SHA-512d.
    CBlockHeader fork_only{header};
    while (solves(fork_only, 0) || !solves(fork_only, 100)) ++fork_only.nNonce;
    BOOST_CHECK(!CheckProofOfWork(fork_only, 99, consensus));
    BOOST_CHECK(CheckProofOfWork(fork_only, 100, consensus));
    BOOST_CHECK(CheckProofOfWorkAnyAlgo(fork_only, consensus));

    // A header that solves neither is rejected everywhere.
    CBlockHeader neither{header};
    while (solves(neither, 0) || solves(neither, 100)) ++neither.nNonce;
    BOOST_CHECK(!CheckProofOfWork(neither, 99, consensus));
    BOOST_CHECK(!CheckProofOfWork(neither, 100, consensus));
    BOOST_CHECK(!CheckProofOfWorkAnyAlgo(neither, consensus));
}

/* From the fork height on the difficulty retargets on every block over a
 * moving window, after a one-off reset to the minimum. */
BOOST_AUTO_TEST_CASE(fork_difficulty_adjustment)
{
    const auto consensus{CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus()};
    const int fork{consensus.nHardForkHeight};
    const int64_t window{consensus.nPowForkAveragingWindow};
    const int64_t spacing{consensus.nPowTargetSpacing};
    const uint32_t pow_limit_bits{UintToArith256(consensus.powLimit).GetCompact()};
    const CBlockHeader dummy_header;

    // The fork block itself resets to minimum difficulty: all the work that
    // SHA-256 hardware accumulated says nothing about SHA-512 hash rate.
    {
        CBlockIndex last;
        last.nHeight = fork - 1;
        last.nBits = 0x1703098c; // a real, very high Bitcoin difficulty
        BOOST_CHECK_EQUAL(GetNextWorkRequired(&last, &dummy_header, consensus), pow_limit_bits);
    }

    // Until a full window of post-fork blocks exists, difficulty stays there.
    {
        CBlockIndex last;
        last.nHeight = fork + window - 1;
        last.nBits = pow_limit_bits;
        BOOST_CHECK_EQUAL(GetNextWorkRequired(&last, &dummy_header, consensus), pow_limit_bits);
    }

    // With a full window, the next target is derived from the work and the time
    // the window actually took. Use a target well below the pow limit so the
    // result can move in both directions without being clamped.
    const uint32_t base_bits{0x1c00ffff};
    const auto next_target = [&](int64_t actual_spacing) {
        std::vector<CBlockIndex> chain(window + 1);
        arith_uint256 work{0};
        for (int64_t i = 0; i <= window; ++i) {
            CBlockIndex& index{chain[i]};
            index.nHeight = fork + static_cast<int>(i);
            index.nBits = base_bits;
            index.nTime = static_cast<uint32_t>(1'700'000'000 + i * actual_spacing);
            index.pprev = i > 0 ? &chain[i - 1] : nullptr;
            work += GetBlockProof(index);
            index.nChainWork = work;
        }
        arith_uint256 target;
        target.SetCompact(GetNextWorkRequired(&chain[window], &dummy_header, consensus));
        return target;
    };

    arith_uint256 base_target;
    base_target.SetCompact(base_bits);

    // Within a tenth of a percent -- the compact encoding loses the low bits.
    const auto close_to = [](const arith_uint256& actual, const arith_uint256& expected) {
        return actual * 1000 <= expected * 1001 && actual * 1001 >= expected * 1000;
    };

    BOOST_CHECK(close_to(next_target(spacing), base_target));
    // Blocks arriving twice as fast halve the target, i.e. double the difficulty.
    BOOST_CHECK(close_to(next_target(spacing / 2), base_target / 2));
    // Blocks arriving half as fast double it.
    BOOST_CHECK(close_to(next_target(spacing * 2), base_target * 2));
    // Beyond that the timespan is clamped, so a wildly wrong timestamp cannot
    // move the difficulty further than a factor of two per block.
    BOOST_CHECK_EQUAL(next_target(spacing / 20).GetCompact(), next_target(spacing / 2).GetCompact());
    BOOST_CHECK_EQUAL(next_target(spacing * 20).GetCompact(), next_target(spacing * 2).GetCompact());

    // The pre-sync heuristic cannot bound a per-block retarget, so it accepts
    // any transition from the fork height on.
    BOOST_CHECK(PermittedDifficultyTransition(consensus, fork, base_bits, pow_limit_bits));
    BOOST_CHECK(!PermittedDifficultyTransition(consensus, fork - 1, base_bits, pow_limit_bits));
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_SUITE_END()
