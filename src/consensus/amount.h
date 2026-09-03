// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cstdint>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

/** The amount of satoshis in one BTC. */
inline constexpr CAmount COIN{100'000'000};

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, but rather a sanity
 * check. As this sanity check is used by consensus-critical validation code,
 * the exact value of the MAX_MONEY constant is consensus critical; in unusual
 * circumstances like a(nother) overflow bug that allowed for the creation of
 * coins out of thin air modification could lead to a fork.
 *
 * The constant-inflation hard fork (see doc/hardfork-inflation.md) replaces
 * the halving schedule with a perpetual emission, so the original 21,000,000
 * BTC bound no longer holds. Raising the bound is a relaxation: every block
 * that satisfied the old bound still satisfies the new one, so the historical
 * chain stays valid.
 *
 * The value is picked so that (a) the emission schedule can run for ~236 years
 * past the fork before it has to stop, and (b) 2 * MAX_MONEY still fits
 * comfortably into an int64_t, which is what keeps the running-sum overflow
 * checks in CheckTransaction()/CheckTxInputs() meaningful.
 * */
inline constexpr CAmount MAX_MONEY{2'100'000'000 * COIN};
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // BITCOIN_CONSENSUS_AMOUNT_H
