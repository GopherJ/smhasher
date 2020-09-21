/**
 * @file prvhash42s.h
 *
 * @brief The inclusion file for the "prvhash42s" hash function. Efficient on
 * large data blocks, more secure, streamed. Implements a parallel variant of
 * the "prvhash42" hash function.
 *
 * @mainpage
 *
 * @section intro_sec Introduction
 *
 * Description is available at https://github.com/avaneev/prvhash
 *
 * @section license License
 *
 * Copyright (c) 2020 Aleksey Vaneev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * @version 2.22
 */

//$ nocpp

#ifndef PRVHASH42S_INCLUDED
#define PRVHASH42S_INCLUDED

#include <string.h>
#include "prvhash42ec.h"

#define PRVHASH42S_LEN 32 // Intermediate block's length, in bytes.

/**
 * The context structure of the "prvhash42s_X" functions.
 */

typedef struct {
	uint64_t lcg[ 4 ]; ///< Current parallel "lcg" values.
	uint64_t Seed[ 4 ]; ///< Current parallel "Seed" values.
	uint8_t Block[ PRVHASH42S_LEN ]; ///< Intermediate input data block.
	size_t BlockFill; ///< The number of bytes filled in the Block.
	uint8_t* Hash; ///< Pointer to the hash buffer.
	int HashLen; ///< Hash buffer length, in bytes, >= 4, in increments of 4.
	int HashPos; ///< Hash buffer position.
	int hlm; ///< Hash length multiplied by 2 or 1 (1 for 32-bit hashes).
	uint8_t fb; ///< Final stream byte value, for hashing finalization.
} PRVHASH42S_CTX;

/**
 * PRVHASH streaming hash function initialization (four 64-bit variables with
 * 32-bit hash word). This function should be called before the hashing
 * session.
 *
 * @param ctx Context structure.
 * @param[in,out] Hash The hash buffer. The length of this buffer should be
 * equal to HashLen * 2 to supply a scratch pad for the function (for 32-bit
 * hashes, this buffer can have HashLen length). If InitVec is non-NULL, the
 * hash will not be initially reset to the default values, and it should be
 * pre-initialized with uniformly-random bytes (there are no restrictions on
 * which values to use for initialization: even an all-zero value can be
 * used). The provided hash will be automatically endianness-corrected. On
 * systems where this is relevant, this address should be aligned to 32 bits.
 * This pointer will be stored in the "ctx" structure.
 * @param HashLen The required hash length, in bytes, should be >= 4, in
 * increments of 4.
 * @param SeedXOR Optional values, to XOR the default seeds with. To use the
 * default seeds, set to 0. If InitVec is non-NULL, this SeedXOR is ignored
 * and should be set to 0. Otherwise, the SeedXOR values can have any bit
 * length, up to four 64-bit values can be supplied, and are used only as an
 * additional entropy source. They should be endianness-corrected.
 * @param InitVec If non-NULL, an "initialization vector" for internal "lcg"
 * and "Seed" variables. Full 64-byte uniformly-random value should be
 * supplied in this case. Since it is imperative that the initialization
 * vector is non-zero, the best strategies to generate it are: 1) compose the
 * vector from 16-bit random values that have 4 to 12 random bits set; 2)
 * compose the vector from 64-bit random values that have 28-36 random bits
 * set.
 */

inline void prvhash42s_init( PRVHASH42S_CTX* ctx, uint8_t* const Hash,
	const int HashLen, const uint64_t SeedXOR[ 4 ],
	const uint8_t InitVec[ 64 ])
{
        ctx -> hlm = HashLen;

	int i;

	if( InitVec == 0 )
	{
		memset( Hash, 0, ctx -> hlm );

		ctx -> lcg[ 0 ] = 5094281193848473994ULL;
		ctx -> Seed[ 0 ] = 10422451504945688786ULL;
		ctx -> lcg[ 1 ] = 18418079607549277980ULL;
		ctx -> Seed[ 1 ] = 6112660181958639245ULL;
		ctx -> lcg[ 2 ] = 9775183536304958763ULL;
		ctx -> Seed[ 2 ] = 12847999344382427112ULL;
		ctx -> lcg[ 3 ] = 14921828135776612191ULL;
		ctx -> Seed[ 3 ] = 17763997673696612504ULL;

		if( SeedXOR != 0 )
		{
			ctx -> Seed[ 0 ] ^= SeedXOR[ 0 ];
			ctx -> Seed[ 1 ] ^= SeedXOR[ 1 ];
			ctx -> Seed[ 2 ] ^= SeedXOR[ 2 ];
			ctx -> Seed[ 3 ] ^= SeedXOR[ 3 ];
		}
	}
	else
	{
		prvhash42_ec( Hash, ctx -> hlm );

		for( i = 0; i < 4; i++ )
		{
			ctx -> lcg[ i ] = prvhash42_u64ec( InitVec + i * 16 );
			ctx -> Seed[ i ] = prvhash42_u64ec( InitVec + i * 16 + 8 );
		}
	}

	ctx -> BlockFill = 0;
	ctx -> Hash = Hash;
	ctx -> HashLen = HashLen;
	ctx -> HashPos = 0;
	ctx -> fb = (uint8_t) 0xFF;
}

/**
 * This function updates the hash according to the contents of the message.
 * Before this function can be called, the prvhash42s_init() should be called,
 * to initialize the context structure. When the streamed hashing is finished,
 * the prvhash42s_final() function should be called.
 *
 * @param ctx Context structure.
 * @param Msg The message to produce hash from. The alignment of the message
 * is unimportant.
 * @param MsgLen Message's length, in bytes.
 */

inline void prvhash42s_update( PRVHASH42S_CTX* ctx, const uint8_t* Msg,
	size_t MsgLen )
{
	if( MsgLen == 0 )
	{
		return;
	}

	ctx -> fb = (uint8_t) ~Msg[ MsgLen - 1 ];

	uint64_t lcg1 = ctx -> lcg[ 0 ];
	uint64_t lcg2 = ctx -> lcg[ 1 ];
	uint64_t lcg3 = ctx -> lcg[ 2 ];
	uint64_t lcg4 = ctx -> lcg[ 3 ];
	uint64_t Seed1 = ctx -> Seed[ 0 ];
	uint64_t Seed2 = ctx -> Seed[ 1 ];
	uint64_t Seed3 = ctx -> Seed[ 2 ];
	uint64_t Seed4 = ctx -> Seed[ 3 ];

	const uint32_t* const HashEnd = (uint32_t*) ( ctx -> Hash + ctx -> hlm );
	uint32_t* hc = (uint32_t*) ( ctx -> Hash + ctx -> HashPos );
	size_t BlockFill = ctx -> BlockFill;

	while( BlockFill + MsgLen >= PRVHASH42S_LEN )
	{
		const uint8_t* MsgBlock = Msg;
		uint64_t msgw, msgw2, ph, hl;

		Seed1 *= lcg1;
		Seed2 *= lcg2;
		Seed3 *= lcg3;
		Seed4 *= lcg4;

		if( BlockFill > 0 )
		{
			const size_t CopyLen = PRVHASH42S_LEN - ctx -> BlockFill;
			MsgBlock = ctx -> Block;
			memcpy( ctx -> Block + ctx -> BlockFill, Msg, CopyLen );

			BlockFill = 0;
			Msg += CopyLen;
			MsgLen -= CopyLen;
		}
		else
		{
			Msg += PRVHASH42S_LEN;
			MsgLen -= PRVHASH42S_LEN;
		}

		Seed1 = ~Seed1;
		Seed2 = ~Seed2;
		Seed3 = ~Seed3;
		Seed4 = ~Seed4;

		msgw = prvhash42_u32ec( MsgBlock + 12 );
		msgw2 = prvhash42_u32ec( MsgBlock + 0 );

		ph = *hc;

		hl = lcg1 >> 32 ^ msgw;
		lcg1 += Seed1;
		lcg1 += msgw2;
		ph ^= Seed1 >> 32;
		Seed1 ^= ph ^ hl;

		msgw = prvhash42_u32ec( MsgBlock + 8 );
		msgw2 = prvhash42_u32ec( MsgBlock + 24 );

		hl = lcg2 >> 32 ^ msgw;
		lcg2 += Seed2;
		lcg2 += msgw2;
		ph ^= Seed2 >> 32;
		Seed2 ^= ph ^ hl;

		msgw = prvhash42_u32ec( MsgBlock + 20 );
		msgw2 = prvhash42_u32ec( MsgBlock + 28 );

		hl = lcg3 >> 32 ^ msgw;
		lcg3 += Seed3;
		lcg3 += msgw2;
		ph ^= Seed3 >> 32;
		Seed3 ^= ph ^ hl;

		msgw = prvhash42_u32ec( MsgBlock + 4 );
		msgw2 = prvhash42_u32ec( MsgBlock + 16 );

		hl = lcg4 >> 32 ^ msgw;
		lcg4 += Seed4;
		lcg4 += msgw2;
		ph ^= Seed4 >> 32;
		Seed4 ^= ph ^ hl;

		*hc = (uint32_t) ph;

		hc++;

		if( hc == HashEnd )
		{
			hc = (uint32_t*) ctx -> Hash;
		}
	}

	ctx -> lcg[ 0 ] = lcg1;
	ctx -> lcg[ 1 ] = lcg2;
	ctx -> lcg[ 2 ] = lcg3;
	ctx -> lcg[ 3 ] = lcg4;
	ctx -> Seed[ 0 ] = Seed1;
	ctx -> Seed[ 1 ] = Seed2;
	ctx -> Seed[ 2 ] = Seed3;
	ctx -> Seed[ 3 ] = Seed4;
	ctx -> HashPos = (int) ( (uint8_t*) hc - ctx -> Hash );

	memcpy( ctx -> Block + BlockFill, Msg, MsgLen );
	ctx -> BlockFill = BlockFill + MsgLen;
}

/**
 * This function finalizes the streamed hashing. This function should be
 * called only after prior prvhash42s_init() function call. This function
 * applies endianness correction automatically (on little- and big-endian
 * processors).
 *
 * @param ctx Context structure.
 */

inline void prvhash42s_final( PRVHASH42S_CTX* ctx )
{
	uint8_t fbytes[ PRVHASH42S_LEN ];
	memset( fbytes, ctx -> fb, PRVHASH42S_LEN );

	if( ctx -> BlockFill > 0 )
	{
		prvhash42s_update( ctx, fbytes, PRVHASH42S_LEN - ctx -> BlockFill );
	}

	int c = ctx -> HashLen + ctx -> hlm - ctx -> HashPos;

	while( c > 0 )
	{
		prvhash42s_update( ctx, fbytes, PRVHASH42S_LEN );
		c -= 4;
	}

	/*if( ctx -> hlm > 4 )
	{
		for( c = 0; c < ctx -> HashLen; c += 4 )
		{
			*(uint32_t*) ( ctx -> Hash + c ) ^=
				*(uint32_t*) ( ctx -> Hash + ctx -> HashLen + c );
		}
        }*/

	prvhash42_ec( ctx -> Hash, ctx -> HashLen );
}

/**
 * This function calculates the "prvhash42s" hash of the specified message in
 * "oneshot" mode, with default seed settings, without using streaming
 * capabilities.
 *
 * @param Msg The message to produce hash from. The alignment of the message
 * is unimportant.
 * @param MsgLen Message's length, in bytes.
 * @param[out] Hash The hash buffer, length = HashLen * 2 (or HashLen for
 * 32-bit hashes). On systems where this is relevant, this address should be
 * aligned to 32 bits.
 * @param HashLen The required hash length, in bytes, should be >= 4, in
 * increments of 4.
 */

inline void prvhash42s_oneshot( const uint8_t* const Msg, const size_t MsgLen,
	uint8_t* const Hash, const int HashLen )
{
	PRVHASH42S_CTX ctx;

	prvhash42s_init( &ctx, Hash, HashLen, 0, 0 );
	prvhash42s_update( &ctx, Msg, MsgLen );
	prvhash42s_final( &ctx );
}

#endif // PRVHASH42S_INCLUDED
