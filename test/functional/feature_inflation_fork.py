#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the constant-inflation hard fork on regtest.

The fork is activated at a low height with -inflationforkheight, and the node
mines across it. From the fork height on:

  - the block subsidy follows the perpetual ~1.98 %/year schedule instead of
    the halving schedule, and
  - the proof of work is a truncated double SHA-512 of the header, which the
    node has to grind itself (the block *hash* stays double SHA-256).

See doc/hardfork-inflation.md.
"""
import hashlib

from test_framework.address import ADDRESS_BCRT1_UNSPENDABLE
from test_framework.messages import COIN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

FORK_HEIGHT = 200
# Regtest values from src/kernel/chainparams.cpp.
HALVING_INTERVAL = 150
BLOCKS_PER_YEAR = 150
RATE_NUM = 99
RATE_DEN = 5000


def legacy_subsidy(height):
    """Bitcoin's original subsidy at `height`, in satoshis."""
    halvings = height // HALVING_INTERVAL
    return 0 if halvings >= 64 else (50 * COIN) >> halvings


def scheduled_supply(height):
    """Supply in satoshis scheduled by the halving schedule after `height` blocks."""
    supply = 0
    h = 0
    while h < height:
        era_end = min(height, (h // HALVING_INTERVAL + 1) * HALVING_INTERVAL)
        supply += legacy_subsidy(h) * (era_end - h)
        h = era_end
    return supply


def inflation_subsidy(height):
    """The fork's subsidy at `height`, in satoshis."""
    assert height >= FORK_HEIGHT
    den = RATE_DEN * BLOCKS_PER_YEAR
    supply = scheduled_supply(FORK_HEIGHT)
    for _ in range((height - FORK_HEIGHT) // BLOCKS_PER_YEAR):
        supply += (supply * RATE_NUM // den) * BLOCKS_PER_YEAR
    return supply * RATE_NUM // den


class InflationForkTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[f"-inflationforkheight={FORK_HEIGHT}"]]

    def subsidy_at(self, height):
        """The subsidy the node actually paid at `height`, in satoshis."""
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(height), 2)
        coinbase = block["tx"][0]
        assert_equal(len(coinbase["vin"]), 1)
        # No fees on this chain, so the whole coinbase output is the subsidy.
        return sum(round(out["value"] * COIN) for out in coinbase["vout"])

    def pow_hash(self, height):
        """SHA-512(SHA-512(header))[:32], little-endian, as the node sees it."""
        header = bytes.fromhex(self.nodes[0].getblockheader(
            self.nodes[0].getblockhash(height), False))
        assert_equal(len(header), 80)
        digest = hashlib.sha512(hashlib.sha512(header).digest()).digest()
        return int.from_bytes(digest[:32], "little")

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mine up to the fork height on the halving schedule")
        self.generatetoaddress(node, FORK_HEIGHT - 1, ADDRESS_BCRT1_UNSPENDABLE)
        assert_equal(node.getblockcount(), FORK_HEIGHT - 1)

        assert_equal(self.subsidy_at(1), 50 * COIN)
        assert_equal(self.subsidy_at(HALVING_INTERVAL - 1), 50 * COIN)
        assert_equal(self.subsidy_at(HALVING_INTERVAL), 25 * COIN)
        assert_equal(self.subsidy_at(FORK_HEIGHT - 1), 25 * COIN)

        self.log.info("The fork block switches to the perpetual schedule")
        self.generatetoaddress(node, 1, ADDRESS_BCRT1_UNSPENDABLE)
        assert_equal(node.getblockcount(), FORK_HEIGHT)

        expected = inflation_subsidy(FORK_HEIGHT)
        assert_equal(expected, 115_500_000)  # 1.155 BTC, see doc/hardfork-inflation.md
        assert_equal(self.subsidy_at(FORK_HEIGHT), expected)
        # ... which is not what the halving schedule would have paid here.
        assert expected != legacy_subsidy(FORK_HEIGHT)

        self.log.info("getblockstats reports the same subsidy")
        assert_equal(node.getblockstats(FORK_HEIGHT)["subsidy"], expected)

        self.log.info("The fork block's proof of work is SHA-512d, not the block hash")
        target = int(node.getblockheader(node.getblockhash(FORK_HEIGHT))["bits"], 16)
        # Decode the compact target the same way DeriveTarget() does.
        target = (target & 0x007fffff) << (8 * ((target >> 24) - 3))
        assert_greater_than(target, self.pow_hash(FORK_HEIGHT))
        # The block hash itself is still double SHA-256 and unrelated to it.
        assert node.getblockhash(FORK_HEIGHT) != f"{self.pow_hash(FORK_HEIGHT):064x}"
        # Blocks below the fork height are *not* required to solve SHA-512d, and
        # with a ~2**255 target the odds of one doing so by chance are 1 in 2, so
        # over 50 blocks at least one is virtually certain not to.
        assert any(self.pow_hash(h) > target for h in range(FORK_HEIGHT - 50, FORK_HEIGHT))

        self.log.info("The subsidy is constant within an emission year")
        self.generatetoaddress(node, BLOCKS_PER_YEAR - 1, ADDRESS_BCRT1_UNSPENDABLE)
        assert_equal(node.getblockcount(), FORK_HEIGHT + BLOCKS_PER_YEAR - 1)
        assert_equal(self.subsidy_at(FORK_HEIGHT + BLOCKS_PER_YEAR - 1), expected)

        self.log.info("... and steps up at the year boundary")
        self.generatetoaddress(node, 1, ADDRESS_BCRT1_UNSPENDABLE)
        next_expected = inflation_subsidy(FORK_HEIGHT + BLOCKS_PER_YEAR)
        assert_equal(next_expected, 117_786_900)
        assert_equal(self.subsidy_at(FORK_HEIGHT + BLOCKS_PER_YEAR), next_expected)

        self.log.info("The realised growth is just under the nominal 1.98 %")
        supply_at_fork = scheduled_supply(FORK_HEIGHT)
        emitted = expected * BLOCKS_PER_YEAR
        assert emitted * RATE_DEN <= supply_at_fork * RATE_NUM
        assert (emitted + BLOCKS_PER_YEAR) * RATE_DEN > supply_at_fork * RATE_NUM

        self.log.info("A restarted node re-validates the post-fork chain")
        tip = node.getbestblockhash()
        self.restart_node(0, extra_args=[f"-inflationforkheight={FORK_HEIGHT}",
                                         "-checkblocks=0", "-checklevel=4"])
        assert_equal(node.getbestblockhash(), tip)


if __name__ == '__main__':
    InflationForkTest(__file__).main()
